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
#include "shrpx_spdy_upstream.h"

#include <netinet/tcp.h>
#include <assert.h>
#include <cerrno>
#include <sstream>

#include <nghttp2/nghttp2.h>

#include "shrpx_client_handler.h"
#include "shrpx_downstream.h"
#include "shrpx_downstream_connection.h"
#include "shrpx_config.h"
#include "shrpx_http.h"
#ifdef HAVE_MRUBY
#include "shrpx_mruby.h"
#endif // HAVE_MRUBY
#include "shrpx_worker.h"
#include "shrpx_http2_session.h"
#include "http2.h"
#include "util.h"
#include "template.h"

using namespace nghttp2;

namespace shrpx {

namespace {
ssize_t send_callback(spdylay_session *session, const uint8_t *data, size_t len,
                      int flags, void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  auto wb = upstream->get_response_buf();

  if (wb->wleft() == 0) {
    return SPDYLAY_ERR_WOULDBLOCK;
  }

  auto nread = wb->write(data, len);

  return nread;
}
} // namespace

namespace {
ssize_t recv_callback(spdylay_session *session, uint8_t *buf, size_t len,
                      int flags, void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  auto handler = upstream->get_client_handler();
  auto rb = handler->get_rb();
  auto rlimit = handler->get_rlimit();

  if (rb->rleft() == 0) {
    return SPDYLAY_ERR_WOULDBLOCK;
  }

  auto nread = std::min(rb->rleft(), len);

  memcpy(buf, rb->pos, nread);
  rb->drain(nread);
  rlimit->startw();

  return nread;
}
} // namespace

namespace {
void on_stream_close_callback(spdylay_session *session, int32_t stream_id,
                              spdylay_status_code status_code,
                              void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, upstream) << "Stream stream_id=" << stream_id
                         << " is being closed";
  }
  auto downstream = static_cast<Downstream *>(
      spdylay_session_get_stream_user_data(session, stream_id));
  if (!downstream) {
    return;
  }

  auto &req = downstream->request();

  upstream->consume(stream_id, req.unconsumed_body_length);

  req.unconsumed_body_length = 0;

  if (downstream->get_request_state() == Downstream::CONNECT_FAIL) {
    upstream->remove_downstream(downstream);
    // downstrea was deleted

    return;
  }

  if (downstream->can_detach_downstream_connection()) {
    // Keep-alive
    downstream->detach_downstream_connection();
  }

  downstream->set_request_state(Downstream::STREAM_CLOSED);

  // At this point, downstream read may be paused.

  // If shrpx_downstream::push_request_headers() failed, the
  // error is handled here.
  upstream->remove_downstream(downstream);
  // downstrea was deleted

  // How to test this case? Request sufficient large download
  // and make client send RST_STREAM after it gets first DATA
  // frame chunk.
}
} // namespace

namespace {
void on_ctrl_recv_callback(spdylay_session *session, spdylay_frame_type type,
                           spdylay_frame *frame, void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  switch (type) {
  case SPDYLAY_SYN_STREAM: {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, upstream) << "Received upstream SYN_STREAM stream_id="
                           << frame->syn_stream.stream_id;
    }

    auto downstream =
        upstream->add_pending_downstream(frame->syn_stream.stream_id);

    auto &req = downstream->request();

    downstream->reset_upstream_rtimer();

    auto nv = frame->syn_stream.nv;

    if (LOG_ENABLED(INFO)) {
      std::stringstream ss;
      for (size_t i = 0; nv[i]; i += 2) {
        ss << TTY_HTTP_HD << nv[i] << TTY_RST << ": " << nv[i + 1] << "\n";
      }
      ULOG(INFO, upstream) << "HTTP request headers. stream_id="
                           << downstream->get_stream_id() << "\n" << ss.str();
    }

    size_t num_headers = 0;
    size_t header_buffer = 0;
    for (size_t i = 0; nv[i]; i += 2) {
      ++num_headers;
      // shut up scan-build
      assert(nv[i + 1]);
      header_buffer += strlen(nv[i]) + strlen(nv[i + 1]);
    }

    auto &httpconf = get_config()->http;

    // spdy does not define usage of trailer fields, and we ignores
    // them.
    if (header_buffer > httpconf.header_field_buffer ||
        num_headers > httpconf.max_header_fields) {
      upstream->rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);
      return;
    }

    for (size_t i = 0; nv[i]; i += 2) {
      req.fs.add_header(nv[i], nv[i + 1]);
    }

    if (req.fs.index_headers() != 0) {
      if (upstream->error_reply(downstream, 400) != 0) {
        ULOG(FATAL, upstream) << "error_reply failed";
      }
      return;
    }

    auto path = req.fs.header(http2::HD__PATH);
    auto scheme = req.fs.header(http2::HD__SCHEME);
    auto host = req.fs.header(http2::HD__HOST);
    auto method = req.fs.header(http2::HD__METHOD);

    if (!method) {
      upstream->rst_stream(downstream, SPDYLAY_PROTOCOL_ERROR);
      return;
    }

    auto method_token = http2::lookup_method_token(method->value);
    if (method_token == -1) {
      if (upstream->error_reply(downstream, 501) != 0) {
        ULOG(FATAL, upstream) << "error_reply failed";
      }
      return;
    }

    auto is_connect = method_token == HTTP_CONNECT;
    if (!path || !host || !http2::non_empty_value(host) ||
        !http2::non_empty_value(path) ||
        (!is_connect && (!scheme || !http2::non_empty_value(scheme)))) {
      upstream->rst_stream(downstream, SPDYLAY_PROTOCOL_ERROR);
      return;
    }

    if (std::find_if(std::begin(host->value), std::end(host->value),
                     [](char c) { return c == '"' || c == '\\'; }) !=
        std::end(host->value)) {
      if (upstream->error_reply(downstream, 400) != 0) {
        ULOG(FATAL, upstream) << "error_reply failed";
      }
      return;
    }

    if (scheme) {
      for (auto c : scheme->value) {
        if (!(util::is_alpha(c) || util::is_digit(c) || c == '+' || c == '-' ||
              c == '.')) {
          if (upstream->error_reply(downstream, 400) != 0) {
            ULOG(FATAL, upstream) << "error_reply failed";
          }
          return;
        }
      }
    }

    // For other than CONNECT method, path must start with "/", except
    // for OPTIONS method, which can take "*" as path.
    if (!is_connect && path->value[0] != '/' &&
        (method_token != HTTP_OPTIONS || path->value != "*")) {
      upstream->rst_stream(downstream, SPDYLAY_PROTOCOL_ERROR);
      return;
    }

    req.method = method_token;
    if (is_connect) {
      req.authority = path->value;
    } else {
      req.scheme = scheme->value;
      req.authority = host->value;
      if (get_config()->http2_proxy || get_config()->client_proxy) {
        req.path = path->value;
      } else if (method_token == HTTP_OPTIONS && path->value == "*") {
        // Server-wide OPTIONS request.  Path is empty.
      } else {
        req.path = http2::rewrite_clean_path(std::begin(path->value),
                                             std::end(path->value));
      }
    }

    if (!(frame->syn_stream.hd.flags & SPDYLAY_CTRL_FLAG_FIN)) {
      req.http2_expect_body = true;
    }

    downstream->inspect_http2_request();

    downstream->set_request_state(Downstream::HEADER_COMPLETE);

#ifdef HAVE_MRUBY
    auto handler = upstream->get_client_handler();
    auto worker = handler->get_worker();
    auto mruby_ctx = worker->get_mruby_context();

    if (mruby_ctx->run_on_request_proc(downstream) != 0) {
      if (upstream->error_reply(downstream, 500) != 0) {
        ULOG(FATAL, upstream) << "error_reply failed";
        return;
      }
      return;
    }
#endif // HAVE_MRUBY

    if (frame->syn_stream.hd.flags & SPDYLAY_CTRL_FLAG_FIN) {
      if (!downstream->validate_request_recv_body_length()) {
        upstream->rst_stream(downstream, SPDYLAY_PROTOCOL_ERROR);
        return;
      }

      downstream->disable_upstream_rtimer();
      downstream->set_request_state(Downstream::MSG_COMPLETE);
    }

    if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
      return;
    }

    upstream->start_downstream(downstream);

    break;
  }
  default:
    break;
  }
}
} // namespace

void SpdyUpstream::start_downstream(Downstream *downstream) {
  if (downstream_queue_.can_activate(downstream->request().authority)) {
    initiate_downstream(downstream);
    return;
  }

  downstream_queue_.mark_blocked(downstream);
}

void SpdyUpstream::initiate_downstream(Downstream *downstream) {
  int rv = downstream->attach_downstream_connection(
      handler_->get_downstream_connection(downstream));
  if (rv != 0) {
    // If downstream connection fails, issue RST_STREAM.
    rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);
    downstream->set_request_state(Downstream::CONNECT_FAIL);

    downstream_queue_.mark_failure(downstream);

    return;
  }
  rv = downstream->push_request_headers();
  if (rv != 0) {
    rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);

    downstream_queue_.mark_failure(downstream);

    return;
  }

  downstream_queue_.mark_active(downstream);
}

namespace {
void on_data_chunk_recv_callback(spdylay_session *session, uint8_t flags,
                                 int32_t stream_id, const uint8_t *data,
                                 size_t len, void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  auto downstream = static_cast<Downstream *>(
      spdylay_session_get_stream_user_data(session, stream_id));

  if (!downstream) {
    upstream->consume(stream_id, len);

    return;
  }

  downstream->reset_upstream_rtimer();

  if (downstream->push_upload_data_chunk(data, len) != 0) {
    upstream->rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);

    upstream->consume(stream_id, len);

    return;
  }

  if (!upstream->get_flow_control()) {
    return;
  }

  auto &http2conf = get_config()->http2;

  // If connection-level window control is not enabled (e.g,
  // spdy/3), spdylay_session_get_recv_data_length() is always
  // returns 0.
  if (spdylay_session_get_recv_data_length(session) >
      std::max(SPDYLAY_INITIAL_WINDOW_SIZE,
               1 << http2conf.upstream.connection_window_bits)) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, upstream) << "Flow control error on connection: "
                           << "recv_window_size="
                           << spdylay_session_get_recv_data_length(session)
                           << ", window_size="
                           << (1 << http2conf.upstream.connection_window_bits);
    }
    spdylay_session_fail_session(session, SPDYLAY_GOAWAY_PROTOCOL_ERROR);
    return;
  }
  if (spdylay_session_get_stream_recv_data_length(session, stream_id) >
      std::max(SPDYLAY_INITIAL_WINDOW_SIZE,
               1 << http2conf.upstream.window_bits)) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, upstream) << "Flow control error: recv_window_size="
                           << spdylay_session_get_stream_recv_data_length(
                                  session, stream_id)
                           << ", initial_window_size="
                           << (1 << http2conf.upstream.window_bits);
    }
    upstream->rst_stream(downstream, SPDYLAY_FLOW_CONTROL_ERROR);
    return;
  }
}
} // namespace

namespace {
void on_data_recv_callback(spdylay_session *session, uint8_t flags,
                           int32_t stream_id, int32_t length, void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  auto downstream = static_cast<Downstream *>(
      spdylay_session_get_stream_user_data(session, stream_id));
  if (downstream && (flags & SPDYLAY_DATA_FLAG_FIN)) {
    if (!downstream->validate_request_recv_body_length()) {
      upstream->rst_stream(downstream, SPDYLAY_PROTOCOL_ERROR);
      return;
    }

    downstream->disable_upstream_rtimer();
    downstream->end_upload_data();
    downstream->set_request_state(Downstream::MSG_COMPLETE);
  }
}
} // namespace

namespace {
void on_ctrl_not_send_callback(spdylay_session *session,
                               spdylay_frame_type type, spdylay_frame *frame,
                               int error_code, void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, upstream) << "Failed to send control frame type=" << type
                         << ", error_code=" << error_code << ":"
                         << spdylay_strerror(error_code);
  }
  if (type == SPDYLAY_SYN_REPLY && error_code != SPDYLAY_ERR_STREAM_CLOSED &&
      error_code != SPDYLAY_ERR_STREAM_CLOSING) {
    // To avoid stream hanging around, issue RST_STREAM.
    auto stream_id = frame->syn_reply.stream_id;
    // TODO Could be always nullptr
    auto downstream = static_cast<Downstream *>(
        spdylay_session_get_stream_user_data(session, stream_id));
    if (downstream) {
      upstream->rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);
    }
  }
}
} // namespace

namespace {
void on_ctrl_recv_parse_error_callback(spdylay_session *session,
                                       spdylay_frame_type type,
                                       const uint8_t *head, size_t headlen,
                                       const uint8_t *payload,
                                       size_t payloadlen, int error_code,
                                       void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, upstream) << "Failed to parse received control frame. type="
                         << type << ", error_code=" << error_code << ":"
                         << spdylay_strerror(error_code);
  }
}
} // namespace

namespace {
void on_unknown_ctrl_recv_callback(spdylay_session *session,
                                   const uint8_t *head, size_t headlen,
                                   const uint8_t *payload, size_t payloadlen,
                                   void *user_data) {
  auto upstream = static_cast<SpdyUpstream *>(user_data);
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, upstream) << "Received unknown control frame.";
  }
}
} // namespace

namespace {
// Infer upstream RST_STREAM status code from downstream HTTP/2
// error code.
uint32_t infer_upstream_rst_stream_status_code(uint32_t downstream_error_code) {
  // Only propagate *_REFUSED_STREAM so that upstream client can
  // resend request.
  if (downstream_error_code == NGHTTP2_REFUSED_STREAM) {
    return SPDYLAY_REFUSED_STREAM;
  } else {
    return SPDYLAY_INTERNAL_ERROR;
  }
}
} // namespace

SpdyUpstream::SpdyUpstream(uint16_t version, ClientHandler *handler)
    : downstream_queue_(
          get_config()->http2_proxy
              ? get_config()->conn.downstream.connections_per_host
              : get_config()->conn.downstream.proto == PROTO_HTTP
                    ? get_config()->conn.downstream.connections_per_frontend
                    : 0,
          !get_config()->http2_proxy),
      handler_(handler), session_(nullptr) {
  spdylay_session_callbacks callbacks{};
  callbacks.send_callback = send_callback;
  callbacks.recv_callback = recv_callback;
  callbacks.on_stream_close_callback = on_stream_close_callback;
  callbacks.on_ctrl_recv_callback = on_ctrl_recv_callback;
  callbacks.on_data_chunk_recv_callback = on_data_chunk_recv_callback;
  callbacks.on_data_recv_callback = on_data_recv_callback;
  callbacks.on_ctrl_not_send_callback = on_ctrl_not_send_callback;
  callbacks.on_ctrl_recv_parse_error_callback =
      on_ctrl_recv_parse_error_callback;
  callbacks.on_unknown_ctrl_recv_callback = on_unknown_ctrl_recv_callback;

  int rv;
  rv = spdylay_session_server_new(&session_, version, &callbacks, this);
  assert(rv == 0);

  uint32_t max_buffer = 64_k;
  rv = spdylay_session_set_option(session_,
                                  SPDYLAY_OPT_MAX_RECV_CTRL_FRAME_BUFFER,
                                  &max_buffer, sizeof(max_buffer));
  assert(rv == 0);

  auto &http2conf = get_config()->http2;

  if (version >= SPDYLAY_PROTO_SPDY3) {
    int val = 1;
    flow_control_ = true;
    initial_window_size_ = 1 << http2conf.upstream.window_bits;
    rv = spdylay_session_set_option(
        session_, SPDYLAY_OPT_NO_AUTO_WINDOW_UPDATE2, &val, sizeof(val));
    assert(rv == 0);
  } else {
    flow_control_ = false;
    initial_window_size_ = 0;
  }
  // TODO Maybe call from outside?
  std::array<spdylay_settings_entry, 2> entry;
  entry[0].settings_id = SPDYLAY_SETTINGS_MAX_CONCURRENT_STREAMS;
  entry[0].value = http2conf.max_concurrent_streams;
  entry[0].flags = SPDYLAY_ID_FLAG_SETTINGS_NONE;

  entry[1].settings_id = SPDYLAY_SETTINGS_INITIAL_WINDOW_SIZE;
  entry[1].value = initial_window_size_;
  entry[1].flags = SPDYLAY_ID_FLAG_SETTINGS_NONE;

  rv = spdylay_submit_settings(session_, SPDYLAY_FLAG_SETTINGS_NONE,
                               entry.data(), entry.size());
  assert(rv == 0);

  if (version >= SPDYLAY_PROTO_SPDY3_1 &&
      http2conf.upstream.connection_window_bits > 16) {
    int32_t delta = (1 << http2conf.upstream.connection_window_bits) -
                    SPDYLAY_INITIAL_WINDOW_SIZE;
    rv = spdylay_submit_window_update(session_, 0, delta);
    assert(rv == 0);
  }

  handler_->reset_upstream_read_timeout(
      get_config()->conn.upstream.timeout.http2_read);

  handler_->signal_write();
}

SpdyUpstream::~SpdyUpstream() { spdylay_session_del(session_); }

int SpdyUpstream::on_read() {
  int rv = 0;

  rv = spdylay_session_recv(session_);
  if (rv < 0) {
    if (rv != SPDYLAY_ERR_EOF) {
      ULOG(ERROR, this) << "spdylay_session_recv() returned error: "
                        << spdylay_strerror(rv);
    }
    return rv;
  }

  handler_->signal_write();

  return 0;
}

// After this function call, downstream may be deleted.
int SpdyUpstream::on_write() {
  int rv = 0;

  if (wb_.rleft() == 0) {
    wb_.reset();
  }

  rv = spdylay_session_send(session_);
  if (rv != 0) {
    ULOG(ERROR, this) << "spdylay_session_send() returned error: "
                      << spdylay_strerror(rv);
    return rv;
  }

  if (spdylay_session_want_read(session_) == 0 &&
      spdylay_session_want_write(session_) == 0 && wb_.rleft() == 0) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, this) << "No more read/write for this SPDY session";
    }
    return -1;
  }
  return 0;
}

ClientHandler *SpdyUpstream::get_client_handler() const { return handler_; }

int SpdyUpstream::downstream_read(DownstreamConnection *dconn) {
  auto downstream = dconn->get_downstream();

  if (downstream->get_response_state() == Downstream::MSG_RESET) {
    // The downstream stream was reset (canceled). In this case,
    // RST_STREAM to the upstream and delete downstream connection
    // here. Deleting downstream will be taken place at
    // on_stream_close_callback.
    rst_stream(downstream,
               infer_upstream_rst_stream_status_code(
                   downstream->get_response_rst_stream_error_code()));
    downstream->pop_downstream_connection();
    dconn = nullptr;
  } else if (downstream->get_response_state() == Downstream::MSG_BAD_HEADER) {
    if (error_reply(downstream, 502) != 0) {
      return -1;
    }
    downstream->pop_downstream_connection();
    // dconn was deleted
    dconn = nullptr;
  } else {
    auto rv = downstream->on_read();
    if (rv == SHRPX_ERR_EOF) {
      return downstream_eof(dconn);
    }
    if (rv == SHRPX_ERR_DCONN_CANCELED) {
      downstream->pop_downstream_connection();
      handler_->signal_write();
      return 0;
    }
    if (rv != 0) {
      if (rv != SHRPX_ERR_NETWORK) {
        if (LOG_ENABLED(INFO)) {
          DCLOG(INFO, dconn) << "HTTP parser failure";
        }
      }
      return downstream_error(dconn, Downstream::EVENT_ERROR);
    }
    if (downstream->can_detach_downstream_connection()) {
      // Keep-alive
      downstream->detach_downstream_connection();
    }
  }

  handler_->signal_write();
  // At this point, downstream may be deleted.

  return 0;
}

int SpdyUpstream::downstream_write(DownstreamConnection *dconn) {
  int rv;
  rv = dconn->on_write();
  if (rv == SHRPX_ERR_NETWORK) {
    return downstream_error(dconn, Downstream::EVENT_ERROR);
  }
  if (rv != 0) {
    return -1;
  }
  return 0;
}

int SpdyUpstream::downstream_eof(DownstreamConnection *dconn) {
  auto downstream = dconn->get_downstream();

  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, dconn) << "EOF. stream_id=" << downstream->get_stream_id();
  }

  // Delete downstream connection. If we don't delete it here, it will
  // be pooled in on_stream_close_callback.
  downstream->pop_downstream_connection();
  // dconn was deleted
  dconn = nullptr;
  // downstream wil be deleted in on_stream_close_callback.
  if (downstream->get_response_state() == Downstream::HEADER_COMPLETE) {
    // Server may indicate the end of the request by EOF
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, this) << "Downstream body was ended by EOF";
    }
    downstream->set_response_state(Downstream::MSG_COMPLETE);

    // For tunneled connection, MSG_COMPLETE signals
    // downstream_data_read_callback to send RST_STREAM after pending
    // response body is sent. This is needed to ensure that RST_STREAM
    // is sent after all pending data are sent.
    on_downstream_body_complete(downstream);
  } else if (downstream->get_response_state() != Downstream::MSG_COMPLETE) {
    // If stream was not closed, then we set MSG_COMPLETE and let
    // on_stream_close_callback delete downstream.
    if (error_reply(downstream, 502) != 0) {
      return -1;
    }
  }
  handler_->signal_write();
  // At this point, downstream may be deleted.
  return 0;
}

int SpdyUpstream::downstream_error(DownstreamConnection *dconn, int events) {
  auto downstream = dconn->get_downstream();

  if (LOG_ENABLED(INFO)) {
    if (events & Downstream::EVENT_ERROR) {
      DCLOG(INFO, dconn) << "Downstream network/general error";
    } else {
      DCLOG(INFO, dconn) << "Timeout";
    }
    if (downstream->get_upgraded()) {
      DCLOG(INFO, dconn) << "Note: this is tunnel connection";
    }
  }

  // Delete downstream connection. If we don't delete it here, it will
  // be pooled in on_stream_close_callback.
  downstream->pop_downstream_connection();
  // dconn was deleted
  dconn = nullptr;

  if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
    // For SSL tunneling, we issue RST_STREAM. For other types of
    // stream, we don't have to do anything since response was
    // complete.
    if (downstream->get_upgraded()) {
      // We want "NO_ERROR" error code but SPDY does not have such
      // code for RST_STREAM.
      rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);
    }
  } else {
    if (downstream->get_response_state() == Downstream::HEADER_COMPLETE) {
      if (downstream->get_upgraded()) {
        on_downstream_body_complete(downstream);
      } else {
        rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);
      }
    } else {
      unsigned int status;
      if (events & Downstream::EVENT_TIMEOUT) {
        status = 504;
      } else {
        status = 502;
      }
      if (error_reply(downstream, status) != 0) {
        return -1;
      }
    }
    downstream->set_response_state(Downstream::MSG_COMPLETE);
  }
  handler_->signal_write();
  // At this point, downstream may be deleted.
  return 0;
}

int SpdyUpstream::rst_stream(Downstream *downstream, int status_code) {
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, this) << "RST_STREAM stream_id=" << downstream->get_stream_id();
  }
  int rv;
  rv = spdylay_submit_rst_stream(session_, downstream->get_stream_id(),
                                 status_code);
  if (rv < SPDYLAY_ERR_FATAL) {
    ULOG(FATAL, this) << "spdylay_submit_rst_stream() failed: "
                      << spdylay_strerror(rv);
    DIE();
  }
  return 0;
}

namespace {
ssize_t spdy_data_read_callback(spdylay_session *session, int32_t stream_id,
                                uint8_t *buf, size_t length, int *eof,
                                spdylay_data_source *source, void *user_data) {
  auto downstream = static_cast<Downstream *>(source->ptr);
  auto upstream = static_cast<SpdyUpstream *>(downstream->get_upstream());
  auto body = downstream->get_response_buf();
  assert(body);

  auto nread = body->remove(buf, length);
  auto body_empty = body->rleft() == 0;

  if (nread == 0 &&
      downstream->get_response_state() == Downstream::MSG_COMPLETE) {
    if (!downstream->get_upgraded()) {
      *eof = 1;
    } else {
      // For tunneling, issue RST_STREAM to finish the stream.
      if (LOG_ENABLED(INFO)) {
        ULOG(INFO, upstream)
            << "RST_STREAM to tunneled stream stream_id=" << stream_id;
      }
      upstream->rst_stream(
          downstream, infer_upstream_rst_stream_status_code(
                          downstream->get_response_rst_stream_error_code()));
    }
  }

  if (body_empty) {
    downstream->disable_upstream_wtimer();
  } else {
    downstream->reset_upstream_wtimer();
  }

  if (nread > 0 && downstream->resume_read(SHRPX_NO_BUFFER, nread) != 0) {
    return SPDYLAY_ERR_CALLBACK_FAILURE;
  }

  if (nread == 0 && *eof != 1) {
    return SPDYLAY_ERR_DEFERRED;
  }

  if (nread > 0) {
    downstream->response_sent_body_length += nread;
  }

  return nread;
}
} // namespace

int SpdyUpstream::send_reply(Downstream *downstream, const uint8_t *body,
                             size_t bodylen) {
  int rv;

  spdylay_data_provider data_prd, *data_prd_ptr = nullptr;
  if (bodylen) {
    data_prd.source.ptr = downstream;
    data_prd.read_callback = spdy_data_read_callback;
    data_prd_ptr = &data_prd;
  }

  const auto &resp = downstream->response();

  auto status_string = http2::get_status_string(resp.http_status);

  const auto &headers = resp.fs.headers();

  auto nva = std::vector<const char *>();
  // 3 for :status, :version and server
  nva.reserve(3 + headers.size());

  nva.push_back(":status");
  nva.push_back(status_string.c_str());
  nva.push_back(":version");
  nva.push_back("HTTP/1.1");

  for (auto &kv : headers) {
    if (kv.name.empty() || kv.name[0] == ':') {
      continue;
    }
    switch (kv.token) {
    case http2::HD_CONNECTION:
    case http2::HD_KEEP_ALIVE:
    case http2::HD_PROXY_CONNECTION:
    case http2::HD_TRANSFER_ENCODING:
      continue;
    }
    nva.push_back(kv.name.c_str());
    nva.push_back(kv.value.c_str());
  }

  if (!resp.fs.header(http2::HD_SERVER)) {
    nva.push_back("server");
    nva.push_back(get_config()->http.server_name.c_str());
  }

  nva.push_back(nullptr);

  rv = spdylay_submit_response(session_, downstream->get_stream_id(),
                               nva.data(), data_prd_ptr);
  if (rv < SPDYLAY_ERR_FATAL) {
    ULOG(FATAL, this) << "spdylay_submit_response() failed: "
                      << spdylay_strerror(rv);
    return -1;
  }

  auto buf = downstream->get_response_buf();

  buf->append(body, bodylen);

  downstream->set_response_state(Downstream::MSG_COMPLETE);

  return 0;
}

int SpdyUpstream::error_reply(Downstream *downstream,
                              unsigned int status_code) {
  int rv;
  auto &resp = downstream->response();

  auto html = http::create_error_html(status_code);
  resp.http_status = status_code;
  auto body = downstream->get_response_buf();
  body->append(html);
  downstream->set_response_state(Downstream::MSG_COMPLETE);

  spdylay_data_provider data_prd;
  data_prd.source.ptr = downstream;
  data_prd.read_callback = spdy_data_read_callback;

  auto lgconf = log_config();
  lgconf->update_tstamp(std::chrono::system_clock::now());

  std::string content_length = util::utos(html.size());
  std::string status_string = http2::get_status_string(status_code);
  const char *nv[] = {":status", status_string.c_str(), ":version", "http/1.1",
                      "content-type", "text/html; charset=UTF-8", "server",
                      get_config()->http.server_name.c_str(), "content-length",
                      content_length.c_str(), "date",
                      lgconf->time_http_str.c_str(), nullptr};

  rv = spdylay_submit_response(session_, downstream->get_stream_id(), nv,
                               &data_prd);
  if (rv < SPDYLAY_ERR_FATAL) {
    ULOG(FATAL, this) << "spdylay_submit_response() failed: "
                      << spdylay_strerror(rv);
    return -1;
  }

  return 0;
}

Downstream *SpdyUpstream::add_pending_downstream(int32_t stream_id) {
  auto downstream =
      make_unique<Downstream>(this, handler_->get_mcpool(), stream_id);
  spdylay_session_set_stream_user_data(session_, stream_id, downstream.get());
  auto res = downstream.get();

  downstream_queue_.add_pending(std::move(downstream));

  return res;
}

void SpdyUpstream::remove_downstream(Downstream *downstream) {
  if (downstream->accesslog_ready()) {
    handler_->write_accesslog(downstream);
  }

  spdylay_session_set_stream_user_data(session_, downstream->get_stream_id(),
                                       nullptr);

  auto next_downstream = downstream_queue_.remove_and_get_blocked(downstream);

  if (next_downstream) {
    initiate_downstream(next_downstream);
  }
}

// WARNING: Never call directly or indirectly spdylay_session_send or
// spdylay_session_recv. These calls may delete downstream.
int SpdyUpstream::on_downstream_header_complete(Downstream *downstream) {
  auto &resp = downstream->response();

  if (downstream->get_non_final_response()) {
    // SPDY does not support non-final response.  We could send it
    // with HEADERS and final response in SYN_REPLY, but it is not
    // official way.
    resp.fs.clear_headers();

    return 0;
  }

  const auto &req = downstream->request();

#ifdef HAVE_MRUBY
  auto worker = handler_->get_worker();
  auto mruby_ctx = worker->get_mruby_context();

  if (mruby_ctx->run_on_response_proc(downstream) != 0) {
    if (error_reply(downstream, 500) != 0) {
      return -1;
    }
    // Returning -1 will signal deletion of dconn.
    return -1;
  }

  if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
    return -1;
  }
#endif // HAVE_MRUBY

  if (LOG_ENABLED(INFO)) {
    DLOG(INFO, downstream) << "HTTP response header completed";
  }

  auto &httpconf = get_config()->http;

  if (!get_config()->http2_proxy && !get_config()->client_proxy &&
      !httpconf.no_location_rewrite) {
    downstream->rewrite_location_response_header(req.scheme);
  }

  // 8 means server, :status, :version and possible via header field.
  auto nv =
      make_unique<const char *[]>(resp.fs.headers().size() * 2 + 8 +
                                  httpconf.add_response_headers.size() * 2 + 1);

  size_t hdidx = 0;
  std::string via_value;
  auto status_string = http2::get_status_string(resp.http_status);
  nv[hdidx++] = ":status";
  nv[hdidx++] = status_string.c_str();
  nv[hdidx++] = ":version";
  nv[hdidx++] = "HTTP/1.1";
  for (auto &hd : resp.fs.headers()) {
    if (hd.name.empty() || hd.name.c_str()[0] == ':') {
      continue;
    }
    switch (hd.token) {
    case http2::HD_CONNECTION:
    case http2::HD_KEEP_ALIVE:
    case http2::HD_PROXY_CONNECTION:
    case http2::HD_TRANSFER_ENCODING:
    case http2::HD_VIA:
    case http2::HD_SERVER:
      continue;
    }

    nv[hdidx++] = hd.name.c_str();
    nv[hdidx++] = hd.value.c_str();
  }

  if (!get_config()->http2_proxy && !get_config()->client_proxy) {
    nv[hdidx++] = "server";
    nv[hdidx++] = httpconf.server_name.c_str();
  } else {
    auto server = resp.fs.header(http2::HD_SERVER);
    if (server) {
      nv[hdidx++] = "server";
      nv[hdidx++] = server->value.c_str();
    }
  }

  auto via = resp.fs.header(http2::HD_VIA);
  if (httpconf.no_via) {
    if (via) {
      nv[hdidx++] = "via";
      nv[hdidx++] = via->value.c_str();
    }
  } else {
    if (via) {
      via_value = via->value;
      via_value += ", ";
    }
    via_value +=
        http::create_via_header_value(resp.http_major, resp.http_minor);
    nv[hdidx++] = "via";
    nv[hdidx++] = via_value.c_str();
  }

  for (auto &p : httpconf.add_response_headers) {
    nv[hdidx++] = p.first.c_str();
    nv[hdidx++] = p.second.c_str();
  }

  nv[hdidx++] = 0;
  if (LOG_ENABLED(INFO)) {
    std::stringstream ss;
    for (size_t i = 0; nv[i]; i += 2) {
      ss << TTY_HTTP_HD << nv[i] << TTY_RST << ": " << nv[i + 1] << "\n";
    }
    ULOG(INFO, this) << "HTTP response headers. stream_id="
                     << downstream->get_stream_id() << "\n" << ss.str();
  }
  spdylay_data_provider data_prd;
  data_prd.source.ptr = downstream;
  data_prd.read_callback = spdy_data_read_callback;

  int rv;
  rv = spdylay_submit_response(session_, downstream->get_stream_id(), nv.get(),
                               &data_prd);
  if (rv != 0) {
    ULOG(FATAL, this) << "spdylay_submit_response() failed";
    return -1;
  }

  return 0;
}

// WARNING: Never call directly or indirectly spdylay_session_send or
// spdylay_session_recv. These calls may delete downstream.
int SpdyUpstream::on_downstream_body(Downstream *downstream,
                                     const uint8_t *data, size_t len,
                                     bool flush) {
  auto body = downstream->get_response_buf();
  body->append(data, len);

  if (flush) {
    spdylay_session_resume_data(session_, downstream->get_stream_id());

    downstream->ensure_upstream_wtimer();
  }

  return 0;
}

// WARNING: Never call directly or indirectly spdylay_session_send or
// spdylay_session_recv. These calls may delete downstream.
int SpdyUpstream::on_downstream_body_complete(Downstream *downstream) {
  if (LOG_ENABLED(INFO)) {
    DLOG(INFO, downstream) << "HTTP response completed";
  }

  auto &resp = downstream->response();

  if (!downstream->validate_response_recv_body_length()) {
    rst_stream(downstream, SPDYLAY_PROTOCOL_ERROR);
    resp.connection_close = true;
    return 0;
  }

  spdylay_session_resume_data(session_, downstream->get_stream_id());
  downstream->ensure_upstream_wtimer();

  return 0;
}

bool SpdyUpstream::get_flow_control() const { return flow_control_; }

void SpdyUpstream::pause_read(IOCtrlReason reason) {}

int SpdyUpstream::resume_read(IOCtrlReason reason, Downstream *downstream,
                              size_t consumed) {
  if (get_flow_control()) {
    if (consume(downstream->get_stream_id(), consumed) != 0) {
      return -1;
    }

    auto &req = downstream->request();

    req.consume(consumed);
  }

  handler_->signal_write();
  return 0;
}

int SpdyUpstream::on_downstream_abort_request(Downstream *downstream,
                                              unsigned int status_code) {
  int rv;

  rv = error_reply(downstream, status_code);

  if (rv != 0) {
    return -1;
  }

  handler_->signal_write();
  return 0;
}

int SpdyUpstream::consume(int32_t stream_id, size_t len) {
  int rv;

  rv = spdylay_session_consume(session_, stream_id, len);

  if (rv != 0) {
    ULOG(WARN, this) << "spdylay_session_consume() returned error: "
                     << spdylay_strerror(rv);
    return -1;
  }

  return 0;
}

int SpdyUpstream::on_timeout(Downstream *downstream) {
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, this) << "Stream timeout stream_id="
                     << downstream->get_stream_id();
  }

  rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);

  return 0;
}

void SpdyUpstream::on_handler_delete() {
  for (auto d = downstream_queue_.get_downstreams(); d; d = d->dlnext) {
    if (d->get_dispatch_state() == Downstream::DISPATCH_ACTIVE &&
        d->accesslog_ready()) {
      handler_->write_accesslog(d);
    }
  }
}

int SpdyUpstream::on_downstream_reset(bool no_retry) {
  int rv;

  for (auto downstream = downstream_queue_.get_downstreams(); downstream;
       downstream = downstream->dlnext) {
    if (downstream->get_dispatch_state() != Downstream::DISPATCH_ACTIVE) {
      continue;
    }

    if (!downstream->request_submission_ready()) {
      rst_stream(downstream, SPDYLAY_INTERNAL_ERROR);
      downstream->pop_downstream_connection();
      continue;
    }

    downstream->pop_downstream_connection();

    downstream->add_retry();

    if (no_retry || downstream->no_more_retry()) {
      goto fail;
    }

    // downstream connection is clean; we can retry with new
    // downstream connection.

    rv = downstream->attach_downstream_connection(
        handler_->get_downstream_connection(downstream));
    if (rv != 0) {
      goto fail;
    }

    continue;

  fail:
    if (on_downstream_abort_request(downstream, 503) != 0) {
      return -1;
    }
    downstream->pop_downstream_connection();
  }

  handler_->signal_write();

  return 0;
}

int SpdyUpstream::initiate_push(Downstream *downstream, const char *uri,
                                size_t len) {
  return 0;
}

int SpdyUpstream::response_riovec(struct iovec *iov, int iovcnt) const {
  if (iovcnt == 0 || wb_.rleft() == 0) {
    return 0;
  }

  iov->iov_base = wb_.pos;
  iov->iov_len = wb_.rleft();

  return 1;
}

void SpdyUpstream::response_drain(size_t n) { wb_.drain(n); }

bool SpdyUpstream::response_empty() const { return wb_.rleft() == 0; }

SpdyUpstream::WriteBuffer *SpdyUpstream::get_response_buf() { return &wb_; }

Downstream *
SpdyUpstream::on_downstream_push_promise(Downstream *downstream,
                                         int32_t promised_stream_id) {
  return nullptr;
}

int SpdyUpstream::on_downstream_push_promise_complete(
    Downstream *downstream, Downstream *promised_downstream) {
  return -1;
}

bool SpdyUpstream::push_enabled() const { return false; }

void SpdyUpstream::cancel_premature_downstream(
    Downstream *promised_downstream) {}

} // namespace shrpx
