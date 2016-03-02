/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "shrpx_http_downstream_connection.h"

#include "shrpx_client_handler.h"
#include "shrpx_upstream.h"
#include "shrpx_downstream.h"
#include "shrpx_config.h"
#include "shrpx_error.h"
#include "shrpx_http.h"
#include "shrpx_log_config.h"
#include "shrpx_connect_blocker.h"
#include "shrpx_downstream_connection_pool.h"
#include "shrpx_worker.h"
#include "shrpx_http2_session.h"
#include "http2.h"
#include "util.h"

using namespace nghttp2;

namespace shrpx {

namespace {
void timeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto conn = static_cast<Connection *>(w->data);
  auto dconn = static_cast<HttpDownstreamConnection *>(conn->data);

  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, dconn) << "Time out";
  }

  auto downstream = dconn->get_downstream();
  auto upstream = downstream->get_upstream();
  auto handler = upstream->get_client_handler();
  auto &resp = downstream->response();

  // Do this so that dconn is not pooled
  resp.connection_close = true;

  if (upstream->downstream_error(dconn, Downstream::EVENT_TIMEOUT) != 0) {
    delete handler;
  }
}
} // namespace

namespace {
void readcb(struct ev_loop *loop, ev_io *w, int revents) {
  auto conn = static_cast<Connection *>(w->data);
  auto dconn = static_cast<HttpDownstreamConnection *>(conn->data);
  auto downstream = dconn->get_downstream();
  auto upstream = downstream->get_upstream();
  auto handler = upstream->get_client_handler();

  if (upstream->downstream_read(dconn) != 0) {
    delete handler;
  }
}
} // namespace

namespace {
void writecb(struct ev_loop *loop, ev_io *w, int revents) {
  auto conn = static_cast<Connection *>(w->data);
  auto dconn = static_cast<HttpDownstreamConnection *>(conn->data);
  auto downstream = dconn->get_downstream();
  auto upstream = downstream->get_upstream();
  auto handler = upstream->get_client_handler();

  if (upstream->downstream_write(dconn) != 0) {
    delete handler;
  }
}
} // namespace

namespace {
void connectcb(struct ev_loop *loop, ev_io *w, int revents) {
  auto conn = static_cast<Connection *>(w->data);
  auto dconn = static_cast<HttpDownstreamConnection *>(conn->data);
  auto downstream = dconn->get_downstream();
  auto upstream = downstream->get_upstream();
  auto handler = upstream->get_client_handler();
  if (dconn->on_connect() != 0) {
    if (upstream->on_downstream_abort_request(downstream, 503) != 0) {
      delete handler;
    }
    return;
  }
  writecb(loop, w, revents);
}
} // namespace

HttpDownstreamConnection::HttpDownstreamConnection(
    DownstreamConnectionPool *dconn_pool, size_t group, struct ev_loop *loop)
    : DownstreamConnection(dconn_pool),
      conn_(loop, -1, nullptr, nullptr,
            get_config()->conn.downstream.timeout.write,
            get_config()->conn.downstream.timeout.read, {}, {}, connectcb,
            readcb, timeoutcb, this, get_config()->tls.dyn_rec.warmup_threshold,
            get_config()->tls.dyn_rec.idle_timeout),
      ioctrl_(&conn_.rlimit), response_htp_{0}, group_(group), addr_idx_(0),
      connected_(false) {}

HttpDownstreamConnection::~HttpDownstreamConnection() {}

int HttpDownstreamConnection::attach_downstream(Downstream *downstream) {
  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, this) << "Attaching to DOWNSTREAM:" << downstream;
  }

  auto &downstreamconf = get_config()->conn.downstream;

  if (conn_.fd == -1) {
    auto connect_blocker = client_handler_->get_connect_blocker();

    if (connect_blocker->blocked()) {
      if (LOG_ENABLED(INFO)) {
        DCLOG(INFO, this)
            << "Downstream connection was blocked by connect_blocker";
      }
      return -1;
    }

    auto worker = client_handler_->get_worker();
    auto &next_downstream = worker->get_dgrp(group_)->next;
    auto end = next_downstream;
    auto &addrs = downstreamconf.addr_groups[group_].addrs;
    for (;;) {
      auto &addr = addrs[next_downstream];
      auto i = next_downstream;
      if (++next_downstream >= addrs.size()) {
        next_downstream = 0;
      }

      conn_.fd = util::create_nonblock_socket(addr.addr.su.storage.ss_family);

      if (conn_.fd == -1) {
        auto error = errno;
        DCLOG(WARN, this) << "socket() failed; errno=" << error;

        connect_blocker->on_failure();

        return SHRPX_ERR_NETWORK;
      }

      int rv;
      rv = connect(conn_.fd, &addr.addr.su.sa, addr.addr.len);
      if (rv != 0 && errno != EINPROGRESS) {
        auto error = errno;
        DCLOG(WARN, this) << "connect() failed; errno=" << error;

        connect_blocker->on_failure();
        close(conn_.fd);
        conn_.fd = -1;

        if (end == next_downstream) {
          return SHRPX_ERR_NETWORK;
        }

        // Try again with the next downstream server
        continue;
      }

      if (LOG_ENABLED(INFO)) {
        DCLOG(INFO, this) << "Connecting to downstream server";
      }

      addr_idx_ = i;

      ev_io_set(&conn_.wev, conn_.fd, EV_WRITE);
      ev_io_set(&conn_.rev, conn_.fd, EV_READ);

      conn_.wlimit.startw();

      break;
    }

    // TODO we should have timeout for connection establishment
    ev_timer_again(conn_.loop, &conn_.wt);
  } else {
    // we may set read timer cb to idle_timeoutcb.  Reset again.
    conn_.rt.repeat = downstreamconf.timeout.read;
    ev_set_cb(&conn_.rt, timeoutcb);
    ev_timer_again(conn_.loop, &conn_.rt);
    ev_set_cb(&conn_.rev, readcb);
  }

  downstream_ = downstream;

  http_parser_init(&response_htp_, HTTP_RESPONSE);
  response_htp_.data = downstream_;

  return 0;
}

int HttpDownstreamConnection::push_request_headers() {
  const auto &downstream_hostport = get_config()
                                        ->conn.downstream.addr_groups[group_]
                                        .addrs[addr_idx_]
                                        .hostport;
  const auto &req = downstream_->request();

  auto connect_method = req.method == HTTP_CONNECT;

  auto &httpconf = get_config()->http;

  // For HTTP/1.0 request, there is no authority in request.  In that
  // case, we use backend server's host nonetheless.
  auto authority = StringRef(downstream_hostport);
  auto no_host_rewrite = httpconf.no_host_rewrite ||
                         get_config()->http2_proxy ||
                         get_config()->client_proxy || connect_method;

  if (no_host_rewrite && !req.authority.empty()) {
    authority = StringRef(req.authority);
  }

  downstream_->set_request_downstream_host(authority.str());

  auto buf = downstream_->get_request_buf();

  // Assume that method and request path do not contain \r\n.
  auto meth = http2::to_method_string(req.method);
  buf->append(meth, strlen(meth));
  buf->append(" ");

  if (connect_method) {
    buf->append(authority);
  } else if (get_config()->http2_proxy || get_config()->client_proxy) {
    // Construct absolute-form request target because we are going to
    // send a request to a HTTP/1 proxy.
    assert(!req.scheme.empty());
    buf->append(req.scheme);
    buf->append("://");
    buf->append(authority);
    buf->append(req.path);
  } else if (req.method == HTTP_OPTIONS && req.path.empty()) {
    // Server-wide OPTIONS
    buf->append("*");
  } else {
    buf->append(req.path);
  }
  buf->append(" HTTP/1.1\r\nHost: ");
  buf->append(authority);
  buf->append("\r\n");

  http2::build_http1_headers_from_headers(buf, req.fs.headers());

  auto cookie = downstream_->assemble_request_cookie();
  if (!cookie.empty()) {
    buf->append("Cookie: ");
    buf->append(cookie);
    buf->append("\r\n");
  }

  if (!connect_method && req.http2_expect_body &&
      !req.fs.header(http2::HD_CONTENT_LENGTH)) {

    downstream_->set_chunked_request(true);
    buf->append("Transfer-Encoding: chunked\r\n");
  }

  if (req.connection_close) {
    buf->append("Connection: close\r\n");
  }

  if (!connect_method && req.upgrade_request) {
    auto connection = req.fs.header(http2::HD_CONNECTION);
    if (connection) {
      buf->append("Connection: ");
      buf->append((*connection).value);
      buf->append("\r\n");
    }

    auto upgrade = req.fs.header(http2::HD_UPGRADE);
    if (upgrade) {
      buf->append("Upgrade: ");
      buf->append((*upgrade).value);
      buf->append("\r\n");
    }
  }

  auto upstream = downstream_->get_upstream();
  auto handler = upstream->get_client_handler();

  auto &fwdconf = httpconf.forwarded;

  auto fwd =
      fwdconf.strip_incoming ? nullptr : req.fs.header(http2::HD_FORWARDED);

  if (fwdconf.params) {
    auto params = fwdconf.params;

    if (get_config()->http2_proxy || get_config()->client_proxy ||
        connect_method) {
      params &= ~FORWARDED_PROTO;
    }

    auto value = http::create_forwarded(params, handler->get_forwarded_by(),
                                        handler->get_forwarded_for(),
                                        req.authority, req.scheme);
    if (fwd || !value.empty()) {
      buf->append("Forwarded: ");
      if (fwd) {
        buf->append(fwd->value);

        if (!value.empty()) {
          buf->append(", ");
        }
      }
      buf->append(value);
      buf->append("\r\n");
    }
  } else if (fwd) {
    buf->append("Forwarded: ");
    buf->append(fwd->value);
    buf->append("\r\n");
  }

  auto &xffconf = httpconf.xff;

  auto xff = xffconf.strip_incoming ? nullptr
                                    : req.fs.header(http2::HD_X_FORWARDED_FOR);

  if (xffconf.add) {
    buf->append("X-Forwarded-For: ");
    if (xff) {
      buf->append((*xff).value);
      buf->append(", ");
    }
    buf->append(client_handler_->get_ipaddr());
    buf->append("\r\n");
  } else if (xff) {
    buf->append("X-Forwarded-For: ");
    buf->append((*xff).value);
    buf->append("\r\n");
  }
  if (!get_config()->http2_proxy && !get_config()->client_proxy &&
      !connect_method) {
    buf->append("X-Forwarded-Proto: ");
    assert(!req.scheme.empty());
    buf->append(req.scheme);
    buf->append("\r\n");
  }
  auto via = req.fs.header(http2::HD_VIA);
  if (httpconf.no_via) {
    if (via) {
      buf->append("Via: ");
      buf->append((*via).value);
      buf->append("\r\n");
    }
  } else {
    buf->append("Via: ");
    if (via) {
      buf->append((*via).value);
      buf->append(", ");
    }
    buf->append(http::create_via_header_value(req.http_major, req.http_minor));
    buf->append("\r\n");
  }

  for (auto &p : httpconf.add_request_headers) {
    buf->append(p.first);
    buf->append(": ");
    buf->append(p.second);
    buf->append("\r\n");
  }

  buf->append("\r\n");

  if (LOG_ENABLED(INFO)) {
    std::string nhdrs;
    for (auto chunk = buf->head; chunk; chunk = chunk->next) {
      nhdrs.append(chunk->pos, chunk->last);
    }
    if (log_config()->errorlog_tty) {
      nhdrs = http::colorizeHeaders(nhdrs.c_str());
    }
    DCLOG(INFO, this) << "HTTP request headers. stream_id="
                      << downstream_->get_stream_id() << "\n" << nhdrs;
  }

  signal_write();

  return 0;
}

int HttpDownstreamConnection::push_upload_data_chunk(const uint8_t *data,
                                                     size_t datalen) {
  auto chunked = downstream_->get_chunked_request();
  auto output = downstream_->get_request_buf();

  if (chunked) {
    auto chunk_size_hex = util::utox(datalen);
    output->append(chunk_size_hex);
    output->append("\r\n");
  }

  output->append(data, datalen);

  if (chunked) {
    output->append("\r\n");
  }

  signal_write();

  return 0;
}

int HttpDownstreamConnection::end_upload_data() {
  if (!downstream_->get_chunked_request()) {
    return 0;
  }

  const auto &req = downstream_->request();

  auto output = downstream_->get_request_buf();
  const auto &trailers = req.fs.trailers();
  if (trailers.empty()) {
    output->append("0\r\n\r\n");
  } else {
    output->append("0\r\n");
    http2::build_http1_headers_from_headers(output, trailers);
    output->append("\r\n");
  }

  signal_write();

  return 0;
}

namespace {
void idle_readcb(struct ev_loop *loop, ev_io *w, int revents) {
  auto conn = static_cast<Connection *>(w->data);
  auto dconn = static_cast<HttpDownstreamConnection *>(conn->data);
  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, dconn) << "Idle connection EOF";
  }
  auto dconn_pool = dconn->get_dconn_pool();
  dconn_pool->remove_downstream_connection(dconn);
  // dconn was deleted
}
} // namespace

namespace {
void idle_timeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto conn = static_cast<Connection *>(w->data);
  auto dconn = static_cast<HttpDownstreamConnection *>(conn->data);
  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, dconn) << "Idle connection timeout";
  }
  auto dconn_pool = dconn->get_dconn_pool();
  dconn_pool->remove_downstream_connection(dconn);
  // dconn was deleted
}
} // namespace

void HttpDownstreamConnection::detach_downstream(Downstream *downstream) {
  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, this) << "Detaching from DOWNSTREAM:" << downstream;
  }
  downstream_ = nullptr;

  ev_set_cb(&conn_.rev, idle_readcb);
  ioctrl_.force_resume_read();

  conn_.rt.repeat = get_config()->conn.downstream.timeout.idle_read;
  ev_set_cb(&conn_.rt, idle_timeoutcb);
  ev_timer_again(conn_.loop, &conn_.rt);

  conn_.wlimit.stopw();
  ev_timer_stop(conn_.loop, &conn_.wt);
}

void HttpDownstreamConnection::pause_read(IOCtrlReason reason) {
  ioctrl_.pause_read(reason);
}

int HttpDownstreamConnection::resume_read(IOCtrlReason reason,
                                          size_t consumed) {
  if (downstream_->get_response_buf()->rleft() <=
      get_config()->conn.downstream.request_buffer_size / 2) {
    ioctrl_.resume_read(reason);
  }

  return 0;
}

void HttpDownstreamConnection::force_resume_read() {
  ioctrl_.force_resume_read();
}

namespace {
int htp_msg_begincb(http_parser *htp) {
  auto downstream = static_cast<Downstream *>(htp->data);

  if (downstream->get_response_state() != Downstream::INITIAL) {
    return -1;
  }

  return 0;
}
} // namespace

namespace {
int htp_hdrs_completecb(http_parser *htp) {
  auto downstream = static_cast<Downstream *>(htp->data);
  auto upstream = downstream->get_upstream();
  const auto &req = downstream->request();
  auto &resp = downstream->response();
  int rv;

  resp.http_status = htp->status_code;
  resp.http_major = htp->http_major;
  resp.http_minor = htp->http_minor;

  if (resp.fs.index_headers() != 0) {
    downstream->set_response_state(Downstream::MSG_BAD_HEADER);
    return -1;
  }

  // Check upgrade before processing non-final response, since if
  // upgrade succeeded, 101 response is treated as final in nghttpx.
  downstream->check_upgrade_fulfilled();

  if (downstream->get_non_final_response()) {
    // Reset content-length because we reuse same Downstream for the
    // next response.
    resp.fs.content_length = -1;
    // For non-final response code, we just call
    // on_downstream_header_complete() without changing response
    // state.
    rv = upstream->on_downstream_header_complete(downstream);

    if (rv != 0) {
      return -1;
    }

    // Ignore response body for non-final response.
    return 1;
  }

  resp.connection_close = !http_should_keep_alive(htp);
  downstream->set_response_state(Downstream::HEADER_COMPLETE);
  downstream->inspect_http1_response();
  if (downstream->get_upgraded()) {
    // content-length must be ignored for upgraded connection.
    resp.fs.content_length = -1;
    resp.connection_close = true;
    // transfer-encoding not applied to upgraded connection
    downstream->set_chunked_response(false);
  }
  if (upstream->on_downstream_header_complete(downstream) != 0) {
    return -1;
  }

  if (downstream->get_upgraded()) {
    // Upgrade complete, read until EOF in both ends
    if (upstream->resume_read(SHRPX_NO_BUFFER, downstream, 0) != 0) {
      return -1;
    }
    downstream->set_request_state(Downstream::HEADER_COMPLETE);
    if (LOG_ENABLED(INFO)) {
      LOG(INFO) << "HTTP upgrade success. stream_id="
                << downstream->get_stream_id();
    }
  }

  auto status = resp.http_status;
  // Ignore the response body. HEAD response may contain
  // Content-Length or Transfer-Encoding: chunked.  Some server send
  // 304 status code with nonzero Content-Length, but without response
  // body. See
  // https://tools.ietf.org/html/rfc7230#section-3.3

  // TODO It seems that the cases other than HEAD are handled by
  // http-parser.  Need test.
  return req.method == HTTP_HEAD || (100 <= status && status <= 199) ||
                 status == 204 || status == 304
             ? 1
             : 0;
}
} // namespace

namespace {
int htp_hdr_keycb(http_parser *htp, const char *data, size_t len) {
  auto downstream = static_cast<Downstream *>(htp->data);
  auto &resp = downstream->response();

  if (downstream->get_response_state() == Downstream::INITIAL) {
    if (resp.fs.header_key_prev()) {
      resp.fs.append_last_header_key(data, len);
    } else {
      resp.fs.add_header(std::string(data, len), "");
    }
  } else {
    // trailer part
    if (resp.fs.trailer_key_prev()) {
      resp.fs.append_last_trailer_key(data, len);
    } else {
      resp.fs.add_trailer(std::string(data, len), "");
    }
  }
  return 0;
}
} // namespace

namespace {
int htp_hdr_valcb(http_parser *htp, const char *data, size_t len) {
  auto downstream = static_cast<Downstream *>(htp->data);
  auto &resp = downstream->response();

  if (downstream->get_response_state() == Downstream::INITIAL) {
    resp.fs.append_last_header_value(data, len);
  } else {
    resp.fs.append_last_trailer_value(data, len);
  }
  return 0;
}
} // namespace

namespace {
int htp_bodycb(http_parser *htp, const char *data, size_t len) {
  auto downstream = static_cast<Downstream *>(htp->data);
  auto &resp = downstream->response();

  resp.recv_body_length += len;

  return downstream->get_upstream()->on_downstream_body(
      downstream, reinterpret_cast<const uint8_t *>(data), len, true);
}
} // namespace

namespace {
int htp_msg_completecb(http_parser *htp) {
  auto downstream = static_cast<Downstream *>(htp->data);

  // http-parser does not treat "200 connection established" response
  // against CONNECT request, and in that case, this function is not
  // called.  But if HTTP Upgrade is made (e.g., WebSocket), this
  // function is called, and http_parser_execute() returns just after
  // that.
  if (downstream->get_upgraded()) {
    return 0;
  }

  if (downstream->get_non_final_response()) {
    downstream->reset_response();

    return 0;
  }

  downstream->set_response_state(Downstream::MSG_COMPLETE);
  // Block reading another response message from (broken?)
  // server. This callback is not called if the connection is
  // tunneled.
  downstream->pause_read(SHRPX_MSG_BLOCK);
  return downstream->get_upstream()->on_downstream_body_complete(downstream);
}
} // namespace

namespace {
http_parser_settings htp_hooks = {
    htp_msg_begincb,     // http_cb on_message_begin;
    nullptr,             // http_data_cb on_url;
    nullptr,             // http_data_cb on_status;
    htp_hdr_keycb,       // http_data_cb on_header_field;
    htp_hdr_valcb,       // http_data_cb on_header_value;
    htp_hdrs_completecb, // http_cb      on_headers_complete;
    htp_bodycb,          // http_data_cb on_body;
    htp_msg_completecb   // http_cb      on_message_complete;
};
} // namespace

int HttpDownstreamConnection::on_read() {
  if (!connected_) {
    return 0;
  }

  ev_timer_again(conn_.loop, &conn_.rt);
  std::array<uint8_t, 8_k> buf;
  int rv;

  if (downstream_->get_upgraded()) {
    // For upgraded connection, just pass data to the upstream.
    for (;;) {
      auto nread = conn_.read_clear(buf.data(), buf.size());

      if (nread == 0) {
        return 0;
      }

      if (nread < 0) {
        return nread;
      }

      rv = downstream_->get_upstream()->on_downstream_body(
          downstream_, buf.data(), nread, true);
      if (rv != 0) {
        return rv;
      }

      if (downstream_->response_buf_full()) {
        downstream_->pause_read(SHRPX_NO_BUFFER);
        return 0;
      }
    }
  }

  for (;;) {
    auto nread = conn_.read_clear(buf.data(), buf.size());

    if (nread == 0) {
      return 0;
    }

    if (nread < 0) {
      return nread;
    }

    auto nproc =
        http_parser_execute(&response_htp_, &htp_hooks,
                            reinterpret_cast<char *>(buf.data()), nread);

    auto htperr = HTTP_PARSER_ERRNO(&response_htp_);

    if (htperr != HPE_OK) {
      // Handling early return (in other words, response was hijacked
      // by mruby scripting).
      if (downstream_->get_response_state() == Downstream::MSG_COMPLETE) {
        return SHRPX_ERR_DCONN_CANCELED;
      }

      if (LOG_ENABLED(INFO)) {
        DCLOG(INFO, this) << "HTTP parser failure: "
                          << "(" << http_errno_name(htperr) << ") "
                          << http_errno_description(htperr);
      }

      return -1;
    }

    if (downstream_->response_buf_full()) {
      downstream_->pause_read(SHRPX_NO_BUFFER);
      return 0;
    }

    if (downstream_->get_upgraded()) {
      if (nproc < static_cast<size_t>(nread)) {
        // Data from buf.data() + nproc are for upgraded protocol.
        rv = downstream_->get_upstream()->on_downstream_body(
            downstream_, buf.data() + nproc, nread - nproc, true);
        if (rv != 0) {
          return rv;
        }

        if (downstream_->response_buf_full()) {
          downstream_->pause_read(SHRPX_NO_BUFFER);
          return 0;
        }
      }
      // call on_read(), so that we can process data left in buffer as
      // upgrade.
      return on_read();
    }
  }
}

int HttpDownstreamConnection::on_write() {
  if (!connected_) {
    return 0;
  }

  ev_timer_again(conn_.loop, &conn_.rt);

  auto upstream = downstream_->get_upstream();
  auto input = downstream_->get_request_buf();

  std::array<struct iovec, MAX_WR_IOVCNT> iov;

  while (input->rleft() > 0) {
    auto iovcnt = input->riovec(iov.data(), iov.size());

    auto nwrite = conn_.writev_clear(iov.data(), iovcnt);

    if (nwrite == 0) {
      return 0;
    }

    if (nwrite < 0) {
      return nwrite;
    }

    input->drain(nwrite);
  }

  conn_.wlimit.stopw();
  ev_timer_stop(conn_.loop, &conn_.wt);

  if (input->rleft() == 0) {
    auto &req = downstream_->request();

    upstream->resume_read(SHRPX_NO_BUFFER, downstream_,
                          req.unconsumed_body_length);
  }

  return 0;
}

int HttpDownstreamConnection::on_connect() {
  auto connect_blocker = client_handler_->get_connect_blocker();

  if (!util::check_socket_connected(conn_.fd)) {
    conn_.wlimit.stopw();

    if (LOG_ENABLED(INFO)) {
      DLOG(INFO, this) << "downstream connect failed";
    }

    downstream_->set_request_state(Downstream::CONNECT_FAIL);

    return -1;
  }

  connected_ = true;

  connect_blocker->on_success();

  conn_.rlimit.startw();
  ev_timer_again(conn_.loop, &conn_.rt);

  ev_set_cb(&conn_.wev, writecb);

  return 0;
}

void HttpDownstreamConnection::on_upstream_change(Upstream *upstream) {}

void HttpDownstreamConnection::signal_write() {
  ev_feed_event(conn_.loop, &conn_.wev, EV_WRITE);
}

size_t HttpDownstreamConnection::get_group() const { return group_; }

} // namespace shrpx
