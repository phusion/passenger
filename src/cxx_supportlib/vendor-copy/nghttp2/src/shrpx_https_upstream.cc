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
#include "shrpx_https_upstream.h"

#include <cassert>
#include <set>
#include <sstream>

#include "shrpx_client_handler.h"
#include "shrpx_downstream.h"
#include "shrpx_downstream_connection.h"
#include "shrpx_http.h"
#include "shrpx_config.h"
#include "shrpx_error.h"
#include "shrpx_log_config.h"
#include "shrpx_worker.h"
#include "shrpx_http2_session.h"
#ifdef HAVE_MRUBY
#include "shrpx_mruby.h"
#endif // HAVE_MRUBY
#include "http2.h"
#include "util.h"
#include "template.h"

using namespace nghttp2;

namespace shrpx {

HttpsUpstream::HttpsUpstream(ClientHandler *handler)
    : handler_(handler), current_header_length_(0),
      ioctrl_(handler->get_rlimit()) {
  http_parser_init(&htp_, HTTP_REQUEST);
  htp_.data = this;
}

HttpsUpstream::~HttpsUpstream() {}

void HttpsUpstream::reset_current_header_length() {
  current_header_length_ = 0;
}

namespace {
int htp_msg_begin(http_parser *htp) {
  auto upstream = static_cast<HttpsUpstream *>(htp->data);
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, upstream) << "HTTP request started";
  }
  upstream->reset_current_header_length();

  auto handler = upstream->get_client_handler();

  auto downstream = make_unique<Downstream>(upstream, handler->get_mcpool(), 0);

  upstream->attach_downstream(std::move(downstream));

  return 0;
}
} // namespace

namespace {
int htp_uricb(http_parser *htp, const char *data, size_t len) {
  auto upstream = static_cast<HttpsUpstream *>(htp->data);
  auto downstream = upstream->get_downstream();
  auto &req = downstream->request();

  // We happen to have the same value for method token.
  req.method = htp->method;

  if (req.fs.buffer_size() + len > get_config()->http.header_field_buffer) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, upstream) << "Too large URI size="
                           << req.fs.buffer_size() + len;
    }
    assert(downstream->get_request_state() == Downstream::INITIAL);
    downstream->set_request_state(Downstream::HTTP1_REQUEST_HEADER_TOO_LARGE);
    return -1;
  }

  req.fs.add_extra_buffer_size(len);

  if (req.method == HTTP_CONNECT) {
    req.authority.append(data, len);
  } else {
    req.path.append(data, len);
  }

  return 0;
}
} // namespace

namespace {
int htp_hdr_keycb(http_parser *htp, const char *data, size_t len) {
  auto upstream = static_cast<HttpsUpstream *>(htp->data);
  auto downstream = upstream->get_downstream();
  auto &req = downstream->request();
  auto &httpconf = get_config()->http;

  if (req.fs.buffer_size() + len > httpconf.header_field_buffer) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, upstream) << "Too large header block size="
                           << req.fs.buffer_size() + len;
    }
    if (downstream->get_request_state() == Downstream::INITIAL) {
      downstream->set_request_state(Downstream::HTTP1_REQUEST_HEADER_TOO_LARGE);
    }
    return -1;
  }
  if (downstream->get_request_state() == Downstream::INITIAL) {
    if (req.fs.header_key_prev()) {
      req.fs.append_last_header_key(data, len);
    } else {
      if (req.fs.num_fields() >= httpconf.max_header_fields) {
        if (LOG_ENABLED(INFO)) {
          ULOG(INFO, upstream)
              << "Too many header field num=" << req.fs.num_fields() + 1;
        }
        downstream->set_request_state(
            Downstream::HTTP1_REQUEST_HEADER_TOO_LARGE);
        return -1;
      }
      req.fs.add_header(std::string(data, len), "");
    }
  } else {
    // trailer part
    if (req.fs.trailer_key_prev()) {
      req.fs.append_last_trailer_key(data, len);
    } else {
      if (req.fs.num_fields() >= httpconf.max_header_fields) {
        if (LOG_ENABLED(INFO)) {
          ULOG(INFO, upstream)
              << "Too many header field num=" << req.fs.num_fields() + 1;
        }
        return -1;
      }
      req.fs.add_trailer(std::string(data, len), "");
    }
  }
  return 0;
}
} // namespace

namespace {
int htp_hdr_valcb(http_parser *htp, const char *data, size_t len) {
  auto upstream = static_cast<HttpsUpstream *>(htp->data);
  auto downstream = upstream->get_downstream();
  auto &req = downstream->request();

  if (req.fs.buffer_size() + len > get_config()->http.header_field_buffer) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, upstream) << "Too large header block size="
                           << req.fs.buffer_size() + len;
    }
    if (downstream->get_request_state() == Downstream::INITIAL) {
      downstream->set_request_state(Downstream::HTTP1_REQUEST_HEADER_TOO_LARGE);
    }
    return -1;
  }
  if (downstream->get_request_state() == Downstream::INITIAL) {
    req.fs.append_last_header_value(data, len);
  } else {
    req.fs.append_last_trailer_value(data, len);
  }
  return 0;
}
} // namespace

namespace {
void rewrite_request_host_path_from_uri(Request &req, const char *uri,
                                        http_parser_url &u) {
  assert(u.field_set & (1 << UF_HOST));

  auto &authority = req.authority;
  authority.clear();
  // As per https://tools.ietf.org/html/rfc7230#section-5.4, we
  // rewrite host header field with authority component.
  http2::copy_url_component(authority, &u, UF_HOST, uri);
  // TODO properly check IPv6 numeric address
  if (authority.find(':') != std::string::npos) {
    authority = '[' + authority;
    authority += ']';
  }
  if (u.field_set & (1 << UF_PORT)) {
    authority += ':';
    authority += util::utos(u.port);
  }

  http2::copy_url_component(req.scheme, &u, UF_SCHEMA, uri);

  std::string path;
  if (u.field_set & (1 << UF_PATH)) {
    http2::copy_url_component(path, &u, UF_PATH, uri);
  } else if (req.method == HTTP_OPTIONS) {
    // Server-wide OPTIONS takes following form in proxy request:
    //
    // OPTIONS http://example.org HTTP/1.1
    //
    // Notice that no slash after authority. See
    // http://tools.ietf.org/html/rfc7230#section-5.3.4
    req.path = "";
    // we ignore query component here
    return;
  } else {
    path = "/";
  }
  if (u.field_set & (1 << UF_QUERY)) {
    auto &fdata = u.field_data[UF_QUERY];
    path += '?';
    path.append(uri + fdata.off, fdata.len);
  }
  if (get_config()->http2_proxy || get_config()->client_proxy) {
    req.path = std::move(path);
  } else {
    req.path = http2::rewrite_clean_path(std::begin(path), std::end(path));
  }
}
} // namespace

namespace {
int htp_hdrs_completecb(http_parser *htp) {
  int rv;
  auto upstream = static_cast<HttpsUpstream *>(htp->data);
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, upstream) << "HTTP request headers completed";
  }
  auto downstream = upstream->get_downstream();
  auto &req = downstream->request();

  req.http_major = htp->http_major;
  req.http_minor = htp->http_minor;

  req.connection_close = !http_should_keep_alive(htp);

  auto method = req.method;

  if (LOG_ENABLED(INFO)) {
    std::stringstream ss;
    ss << http2::to_method_string(method) << " "
       << (method == HTTP_CONNECT ? req.authority : req.path) << " "
       << "HTTP/" << req.http_major << "." << req.http_minor << "\n";

    for (const auto &kv : req.fs.headers()) {
      ss << TTY_HTTP_HD << kv.name << TTY_RST << ": " << kv.value << "\n";
    }

    ULOG(INFO, upstream) << "HTTP request headers\n" << ss.str();
  }

  if (req.fs.index_headers() != 0) {
    return -1;
  }

  auto host = req.fs.header(http2::HD_HOST);

  if (req.http_major == 1 && req.http_minor == 1 && !host) {
    return -1;
  }

  if (host) {
    const auto &value = host->value;
    // Not allow at least '"' or '\' in host.  They are illegal in
    // authority component, also they cause headaches when we put them
    // in quoted-string.
    if (std::find_if(std::begin(value), std::end(value), [](char c) {
          return c == '"' || c == '\\';
        }) != std::end(value)) {
      return -1;
    }
  }

  downstream->inspect_http1_request();

  if (method != HTTP_CONNECT) {
    http_parser_url u{};
    // make a copy of request path, since we may set request path
    // while we are refering to original request path.
    auto path = req.path;
    rv = http_parser_parse_url(path.c_str(), path.size(), 0, &u);
    if (rv != 0) {
      // Expect to respond with 400 bad request
      return -1;
    }
    // checking UF_HOST could be redundant, but just in case ...
    if (!(u.field_set & (1 << UF_SCHEMA)) || !(u.field_set & (1 << UF_HOST))) {
      if (get_config()->http2_proxy || get_config()->client_proxy) {
        // Request URI should be absolute-form for client proxy mode
        return -1;
      }

      req.no_authority = true;

      if (method == HTTP_OPTIONS && path == "*") {
        req.path = "";
      } else {
        req.path = http2::rewrite_clean_path(std::begin(path), std::end(path));
      }

      if (host) {
        req.authority = host->value;
      }

      if (upstream->get_client_handler()->get_ssl()) {
        req.scheme = "https";
      } else {
        req.scheme = "http";
      }
    } else {
      rewrite_request_host_path_from_uri(req, path.c_str(), u);
    }
  }

  downstream->set_request_state(Downstream::HEADER_COMPLETE);

#ifdef HAVE_MRUBY
  auto handler = upstream->get_client_handler();
  auto worker = handler->get_worker();
  auto mruby_ctx = worker->get_mruby_context();

  auto &resp = downstream->response();

  if (mruby_ctx->run_on_request_proc(downstream) != 0) {
    resp.http_status = 500;
    return -1;
  }
#endif // HAVE_MRUBY

  // mruby hook may change method value

  if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
    return 0;
  }

  rv = downstream->attach_downstream_connection(
      upstream->get_client_handler()->get_downstream_connection(downstream));

  if (rv != 0) {
    downstream->set_request_state(Downstream::CONNECT_FAIL);

    return -1;
  }

  rv = downstream->push_request_headers();

  if (rv != 0) {
    return -1;
  }

  return 0;
}
} // namespace

namespace {
int htp_bodycb(http_parser *htp, const char *data, size_t len) {
  int rv;
  auto upstream = static_cast<HttpsUpstream *>(htp->data);
  auto downstream = upstream->get_downstream();
  rv = downstream->push_upload_data_chunk(
      reinterpret_cast<const uint8_t *>(data), len);
  if (rv != 0) {
    return -1;
  }
  return 0;
}
} // namespace

namespace {
int htp_msg_completecb(http_parser *htp) {
  int rv;
  auto upstream = static_cast<HttpsUpstream *>(htp->data);
  if (LOG_ENABLED(INFO)) {
    ULOG(INFO, upstream) << "HTTP request completed";
  }
  auto handler = upstream->get_client_handler();
  auto downstream = upstream->get_downstream();
  downstream->set_request_state(Downstream::MSG_COMPLETE);
  rv = downstream->end_upload_data();
  if (rv != 0) {
    if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
      // Here both response and request were completed.  One of the
      // reason why end_upload_data() failed is when we sent response
      // in request phase hook.  We only delete and proceed to the
      // next request handling (if we don't close the connection).  We
      // first pause parser here jsut as we normally do, and call
      // signal_write() to run on_write().
      http_parser_pause(htp, 1);

      return 0;
    }
    return -1;
  }

  if (handler->get_http2_upgrade_allowed() &&
      downstream->get_http2_upgrade_request() &&
      handler->perform_http2_upgrade(upstream) != 0) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, upstream) << "HTTP Upgrade to HTTP/2 failed";
    }
  }

  // Stop further processing to complete this request
  http_parser_pause(htp, 1);
  return 0;
}
} // namespace

namespace {
http_parser_settings htp_hooks = {
    htp_msg_begin,       // http_cb      on_message_begin;
    htp_uricb,           // http_data_cb on_url;
    nullptr,             // http_data_cb on_status;
    htp_hdr_keycb,       // http_data_cb on_header_field;
    htp_hdr_valcb,       // http_data_cb on_header_value;
    htp_hdrs_completecb, // http_cb      on_headers_complete;
    htp_bodycb,          // http_data_cb on_body;
    htp_msg_completecb   // http_cb      on_message_complete;
};
} // namespace

// on_read() does not consume all available data in input buffer if
// one http request is fully received.
int HttpsUpstream::on_read() {
  auto rb = handler_->get_rb();
  auto rlimit = handler_->get_rlimit();
  auto downstream = get_downstream();

  if (rb->rleft() == 0) {
    return 0;
  }

  // downstream can be nullptr here, because it is initialized in the
  // callback chain called by http_parser_execute()
  if (downstream && downstream->get_upgraded()) {

    auto rv = downstream->push_upload_data_chunk(rb->pos, rb->rleft());

    if (rv != 0) {
      return -1;
    }

    rb->reset();
    rlimit->startw();

    if (downstream->request_buf_full()) {
      if (LOG_ENABLED(INFO)) {
        ULOG(INFO, this) << "Downstream request buf is full";
      }
      pause_read(SHRPX_NO_BUFFER);

      return 0;
    }

    return 0;
  }

  if (downstream) {
    // To avoid reading next pipelined request
    switch (downstream->get_request_state()) {
    case Downstream::INITIAL:
    case Downstream::HEADER_COMPLETE:
      break;
    default:
      return 0;
    }
  }

  // http_parser_execute() does nothing once it entered error state.
  auto nread = http_parser_execute(
      &htp_, &htp_hooks, reinterpret_cast<const char *>(rb->pos), rb->rleft());

  rb->drain(nread);
  rlimit->startw();

  // Well, actually header length + some body bytes
  current_header_length_ += nread;

  // Get downstream again because it may be initialized in http parser
  // execution
  downstream = get_downstream();

  auto htperr = HTTP_PARSER_ERRNO(&htp_);

  if (htperr == HPE_PAUSED) {
    // We may pause parser in htp_msg_completecb when both side are
    // completed.  Signal write, so that we can run on_write().
    if (downstream &&
        downstream->get_request_state() == Downstream::MSG_COMPLETE &&
        downstream->get_response_state() == Downstream::MSG_COMPLETE) {
      handler_->signal_write();
    }
    return 0;
  }

  if (htperr != HPE_OK) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, this) << "HTTP parse failure: "
                       << "(" << http_errno_name(htperr) << ") "
                       << http_errno_description(htperr);
    }

    if (downstream && downstream->get_response_state() != Downstream::INITIAL) {
      handler_->set_should_close_after_write(true);
      handler_->signal_write();
      return 0;
    }

    unsigned int status_code;

    if (htperr == HPE_INVALID_METHOD) {
      status_code = 501;
    } else if (downstream) {
      status_code = downstream->response().http_status;
      if (status_code == 0) {
        if (downstream->get_request_state() == Downstream::CONNECT_FAIL) {
          status_code = 503;
        } else if (downstream->get_request_state() ==
                   Downstream::HTTP1_REQUEST_HEADER_TOO_LARGE) {
          status_code = 431;
        } else {
          status_code = 400;
        }
      }
    } else {
      status_code = 400;
    }

    error_reply(status_code);

    handler_->signal_write();

    return 0;
  }

  // downstream can be NULL here.
  if (downstream && downstream->request_buf_full()) {
    if (LOG_ENABLED(INFO)) {
      ULOG(INFO, this) << "Downstream request buffer is full";
    }

    pause_read(SHRPX_NO_BUFFER);

    return 0;
  }

  return 0;
}

int HttpsUpstream::on_write() {
  auto downstream = get_downstream();
  if (!downstream) {
    return 0;
  }

  auto output = downstream->get_response_buf();
  const auto &resp = downstream->response();

  if (output->rleft() > 0) {
    return 0;
  }

  // We need to postpone detachment until all data are sent so that
  // we can notify nghttp2 library all data consumed.
  if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
    if (resp.connection_close ||
        downstream->get_request_state() != Downstream::MSG_COMPLETE) {
      // Connection close
      downstream->pop_downstream_connection();
      // dconn was deleted
    } else {
      // Keep-alive
      downstream->detach_downstream_connection();
    }
    // We need this if response ends before request.
    if (downstream->get_request_state() == Downstream::MSG_COMPLETE) {
      delete_downstream();

      if (handler_->get_should_close_after_write()) {
        return 0;
      }

      return resume_read(SHRPX_NO_BUFFER, nullptr, 0);
    }
  }

  return downstream->resume_read(SHRPX_NO_BUFFER, resp.unconsumed_body_length);
}

int HttpsUpstream::on_event() { return 0; }

ClientHandler *HttpsUpstream::get_client_handler() const { return handler_; }

void HttpsUpstream::pause_read(IOCtrlReason reason) {
  ioctrl_.pause_read(reason);
}

int HttpsUpstream::resume_read(IOCtrlReason reason, Downstream *downstream,
                               size_t consumed) {
  // downstream could be nullptr
  if (downstream && downstream->request_buf_full()) {
    return 0;
  }
  if (ioctrl_.resume_read(reason)) {
    // Process remaining data in input buffer here because these bytes
    // are not notified by readcb until new data arrive.
    http_parser_pause(&htp_, 0);
    return on_read();
  }

  return 0;
}

int HttpsUpstream::downstream_read(DownstreamConnection *dconn) {
  auto downstream = dconn->get_downstream();
  int rv;

  rv = downstream->on_read();

  if (rv == SHRPX_ERR_EOF) {
    return downstream_eof(dconn);
  }

  if (rv == SHRPX_ERR_DCONN_CANCELED) {
    downstream->pop_downstream_connection();
    goto end;
  }

  if (rv < 0) {
    return downstream_error(dconn, Downstream::EVENT_ERROR);
  }

  if (downstream->get_response_state() == Downstream::MSG_RESET) {
    return -1;
  }

  if (downstream->get_response_state() == Downstream::MSG_BAD_HEADER) {
    error_reply(502);
    downstream->pop_downstream_connection();
    goto end;
  }

  if (downstream->can_detach_downstream_connection()) {
    // Keep-alive
    downstream->detach_downstream_connection();
  }

end:
  handler_->signal_write();

  return 0;
}

int HttpsUpstream::downstream_write(DownstreamConnection *dconn) {
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

int HttpsUpstream::downstream_eof(DownstreamConnection *dconn) {
  auto downstream = dconn->get_downstream();

  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, dconn) << "EOF";
  }

  if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
    goto end;
  }

  if (downstream->get_response_state() == Downstream::HEADER_COMPLETE) {
    // Server may indicate the end of the request by EOF
    if (LOG_ENABLED(INFO)) {
      DCLOG(INFO, dconn) << "The end of the response body was indicated by "
                         << "EOF";
    }
    on_downstream_body_complete(downstream);
    downstream->set_response_state(Downstream::MSG_COMPLETE);
    downstream->pop_downstream_connection();
    goto end;
  }

  if (downstream->get_response_state() == Downstream::INITIAL) {
    // we did not send any response headers, so we can reply error
    // message.
    if (LOG_ENABLED(INFO)) {
      DCLOG(INFO, dconn) << "Return error reply";
    }
    error_reply(502);
    downstream->pop_downstream_connection();
    goto end;
  }

  // Otherwise, we don't know how to recover from this situation. Just
  // drop connection.
  return -1;
end:
  handler_->signal_write();

  return 0;
}

int HttpsUpstream::downstream_error(DownstreamConnection *dconn, int events) {
  auto downstream = dconn->get_downstream();
  if (LOG_ENABLED(INFO)) {
    if (events & Downstream::EVENT_ERROR) {
      DCLOG(INFO, dconn) << "Network error/general error";
    } else {
      DCLOG(INFO, dconn) << "Timeout";
    }
  }
  if (downstream->get_response_state() != Downstream::INITIAL) {
    return -1;
  }

  unsigned int status;
  if (events & Downstream::EVENT_TIMEOUT) {
    status = 504;
  } else {
    status = 502;
  }
  error_reply(status);

  downstream->pop_downstream_connection();

  handler_->signal_write();
  return 0;
}

int HttpsUpstream::send_reply(Downstream *downstream, const uint8_t *body,
                              size_t bodylen) {
  const auto &req = downstream->request();
  auto &resp = downstream->response();

  auto connection_close = false;
  if (req.http_major <= 0 || (req.http_major == 1 && req.http_minor == 0)) {
    connection_close = true;
  } else {
    auto c = resp.fs.header(http2::HD_CONNECTION);
    if (c && util::strieq_l("close", c->value)) {
      connection_close = true;
    }
  }

  if (connection_close) {
    resp.connection_close = true;
    handler_->set_should_close_after_write(true);
  }

  auto output = downstream->get_response_buf();

  output->append("HTTP/1.1 ");
  output->append(http2::get_status_string(resp.http_status));
  output->append("\r\n");

  for (auto &kv : resp.fs.headers()) {
    if (kv.name.empty() || kv.name[0] == ':') {
      continue;
    }
    http2::capitalize(output, kv.name);
    output->append(": ");
    output->append(kv.value);
    output->append("\r\n");
  }

  if (!resp.fs.header(http2::HD_SERVER)) {
    output->append("Server: ");
    output->append(get_config()->http.server_name);
    output->append("\r\n");
  }

  output->append("\r\n");

  output->append(body, bodylen);

  downstream->response_sent_body_length += bodylen;
  downstream->set_response_state(Downstream::MSG_COMPLETE);

  return 0;
}

void HttpsUpstream::error_reply(unsigned int status_code) {
  auto html = http::create_error_html(status_code);
  auto downstream = get_downstream();

  if (!downstream) {
    attach_downstream(make_unique<Downstream>(this, handler_->get_mcpool(), 1));
    downstream = get_downstream();
  }

  auto &resp = downstream->response();

  resp.http_status = status_code;
  // we are going to close connection for both frontend and backend in
  // error condition.  This is safest option.
  resp.connection_close = true;
  handler_->set_should_close_after_write(true);

  auto output = downstream->get_response_buf();

  output->append("HTTP/1.1 ");
  auto status_str = http2::get_status_string(status_code);
  output->append(status_str);
  output->append("\r\nServer: ");
  output->append(get_config()->http.server_name);
  output->append("\r\nContent-Length: ");
  auto cl = util::utos(html.size());
  output->append(cl);
  output->append("\r\nDate: ");
  auto lgconf = log_config();
  lgconf->update_tstamp(std::chrono::system_clock::now());
  auto &date = lgconf->time_http_str;
  output->append(date);
  output->append("\r\nContent-Type: text/html; "
                 "charset=UTF-8\r\nConnection: close\r\n\r\n");
  output->append(html);

  downstream->response_sent_body_length += html.size();
  downstream->set_response_state(Downstream::MSG_COMPLETE);
}

void HttpsUpstream::attach_downstream(std::unique_ptr<Downstream> downstream) {
  assert(!downstream_);
  downstream_ = std::move(downstream);
}

void HttpsUpstream::delete_downstream() {
  if (downstream_ && downstream_->accesslog_ready()) {
    handler_->write_accesslog(downstream_.get());
  }

  downstream_.reset();
}

Downstream *HttpsUpstream::get_downstream() const { return downstream_.get(); }

std::unique_ptr<Downstream> HttpsUpstream::pop_downstream() {
  return std::unique_ptr<Downstream>(downstream_.release());
}

namespace {
void write_altsvc(DefaultMemchunks *buf, const AltSvc &altsvc) {
  buf->append(util::percent_encode_token(altsvc.protocol_id));
  buf->append("=\"");
  buf->append(util::quote_string(altsvc.host));
  buf->append(":");
  buf->append(altsvc.service);
  buf->append("\"");
}
} // namespace

int HttpsUpstream::on_downstream_header_complete(Downstream *downstream) {
  if (LOG_ENABLED(INFO)) {
    if (downstream->get_non_final_response()) {
      DLOG(INFO, downstream) << "HTTP non-final response header";
    } else {
      DLOG(INFO, downstream) << "HTTP response header completed";
    }
  }

  const auto &req = downstream->request();
  auto &resp = downstream->response();

#ifdef HAVE_MRUBY
  if (!downstream->get_non_final_response()) {
    auto worker = handler_->get_worker();
    auto mruby_ctx = worker->get_mruby_context();

    if (mruby_ctx->run_on_response_proc(downstream) != 0) {
      error_reply(500);
      return -1;
    }

    if (downstream->get_response_state() == Downstream::MSG_COMPLETE) {
      return -1;
    }
  }
#endif // HAVE_MRUBY

  auto connect_method = req.method == HTTP_CONNECT;

  auto buf = downstream->get_response_buf();

  buf->append("HTTP/");
  buf->append(util::utos(req.http_major));
  buf->append(".");
  buf->append(util::utos(req.http_minor));
  buf->append(" ");
  buf->append(http2::get_status_string(resp.http_status));
  buf->append("\r\n");

  auto &httpconf = get_config()->http;

  if (!get_config()->http2_proxy && !get_config()->client_proxy &&
      !httpconf.no_location_rewrite) {
    downstream->rewrite_location_response_header(
        get_client_handler()->get_upstream_scheme());
  }

  http2::build_http1_headers_from_headers(buf, resp.fs.headers());

  if (downstream->get_non_final_response()) {
    buf->append("\r\n");

    if (LOG_ENABLED(INFO)) {
      log_response_headers(buf);
    }

    resp.fs.clear_headers();

    return 0;
  }

  auto worker = handler_->get_worker();

  // after graceful shutdown commenced, add connection: close header
  // field.
  if (worker->get_graceful_shutdown()) {
    resp.connection_close = true;
  }

  // We check downstream->get_response_connection_close() in case when
  // the Content-Length is not available.
  if (!req.connection_close && !resp.connection_close) {
    if (req.http_major <= 0 || req.http_minor <= 0) {
      // We add this header for HTTP/1.0 or HTTP/0.9 clients
      buf->append("Connection: Keep-Alive\r\n");
    }
  } else if (!downstream->get_upgraded()) {
    buf->append("Connection: close\r\n");
  }

  if (!connect_method && downstream->get_upgraded()) {
    auto connection = resp.fs.header(http2::HD_CONNECTION);
    if (connection) {
      buf->append("Connection: ");
      buf->append((*connection).value);
      buf->append("\r\n");
    }

    auto upgrade = resp.fs.header(http2::HD_UPGRADE);
    if (upgrade) {
      buf->append("Upgrade: ");
      buf->append((*upgrade).value);
      buf->append("\r\n");
    }
  }

  if (!resp.fs.header(http2::HD_ALT_SVC)) {
    // We won't change or alter alt-svc from backend for now
    if (!httpconf.altsvcs.empty()) {
      buf->append("Alt-Svc: ");

      auto &altsvcs = httpconf.altsvcs;
      write_altsvc(buf, altsvcs[0]);
      for (size_t i = 1; i < altsvcs.size(); ++i) {
        buf->append(", ");
        write_altsvc(buf, altsvcs[i]);
      }
      buf->append("\r\n");
    }
  }

  if (!get_config()->http2_proxy && !get_config()->client_proxy) {
    buf->append("Server: ");
    buf->append(httpconf.server_name);
    buf->append("\r\n");
  } else {
    auto server = resp.fs.header(http2::HD_SERVER);
    if (server) {
      buf->append("Server: ");
      buf->append((*server).value);
      buf->append("\r\n");
    }
  }

  auto via = resp.fs.header(http2::HD_VIA);
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
    buf->append(
        http::create_via_header_value(resp.http_major, resp.http_minor));
    buf->append("\r\n");
  }

  for (auto &p : httpconf.add_response_headers) {
    buf->append(p.first);
    buf->append(": ");
    buf->append(p.second);
    buf->append("\r\n");
  }

  buf->append("\r\n");

  if (LOG_ENABLED(INFO)) {
    log_response_headers(buf);
  }

  return 0;
}

int HttpsUpstream::on_downstream_body(Downstream *downstream,
                                      const uint8_t *data, size_t len,
                                      bool flush) {
  if (len == 0) {
    return 0;
  }
  auto output = downstream->get_response_buf();
  if (downstream->get_chunked_response()) {
    output->append(util::utox(len));
    output->append("\r\n");
  }
  output->append(data, len);

  downstream->response_sent_body_length += len;

  if (downstream->get_chunked_response()) {
    output->append("\r\n");
  }
  return 0;
}

int HttpsUpstream::on_downstream_body_complete(Downstream *downstream) {
  const auto &req = downstream->request();
  auto &resp = downstream->response();

  if (downstream->get_chunked_response()) {
    auto output = downstream->get_response_buf();
    const auto &trailers = resp.fs.trailers();
    if (trailers.empty()) {
      output->append("0\r\n\r\n");
    } else {
      output->append("0\r\n");
      http2::build_http1_headers_from_headers(output, trailers);
      output->append("\r\n");
    }
  }
  if (LOG_ENABLED(INFO)) {
    DLOG(INFO, downstream) << "HTTP response completed";
  }

  if (!downstream->validate_response_recv_body_length()) {
    resp.connection_close = true;
  }

  if (req.connection_close || resp.connection_close) {
    auto handler = get_client_handler();
    handler->set_should_close_after_write(true);
  }
  return 0;
}

int HttpsUpstream::on_downstream_abort_request(Downstream *downstream,
                                               unsigned int status_code) {
  error_reply(status_code);
  handler_->signal_write();
  return 0;
}

void HttpsUpstream::log_response_headers(DefaultMemchunks *buf) const {
  std::string nhdrs;
  for (auto chunk = buf->head; chunk; chunk = chunk->next) {
    nhdrs.append(chunk->pos, chunk->last);
  }
  if (log_config()->errorlog_tty) {
    nhdrs = http::colorizeHeaders(nhdrs.c_str());
  }
  ULOG(INFO, this) << "HTTP response headers\n" << nhdrs;
}

void HttpsUpstream::on_handler_delete() {
  if (downstream_ && downstream_->accesslog_ready()) {
    handler_->write_accesslog(downstream_.get());
  }
}

int HttpsUpstream::on_downstream_reset(bool no_retry) {
  int rv;

  if (!downstream_->request_submission_ready()) {
    // Return error so that caller can delete handler
    return -1;
  }

  downstream_->pop_downstream_connection();

  downstream_->add_retry();

  if (no_retry || downstream_->no_more_retry()) {
    goto fail;
  }

  rv = downstream_->attach_downstream_connection(
      handler_->get_downstream_connection(downstream_.get()));
  if (rv != 0) {
    goto fail;
  }

  return 0;

fail:
  if (on_downstream_abort_request(downstream_.get(), 503) != 0) {
    return -1;
  }
  downstream_->pop_downstream_connection();

  return 0;
}

int HttpsUpstream::initiate_push(Downstream *downstream, const char *uri,
                                 size_t len) {
  return 0;
}

int HttpsUpstream::response_riovec(struct iovec *iov, int iovcnt) const {
  if (!downstream_) {
    return 0;
  }

  auto buf = downstream_->get_response_buf();

  return buf->riovec(iov, iovcnt);
}

void HttpsUpstream::response_drain(size_t n) {
  if (!downstream_) {
    return;
  }

  auto buf = downstream_->get_response_buf();

  buf->drain(n);
}

bool HttpsUpstream::response_empty() const {
  if (!downstream_) {
    return true;
  }

  auto buf = downstream_->get_response_buf();

  return buf->rleft() == 0;
}

Downstream *
HttpsUpstream::on_downstream_push_promise(Downstream *downstream,
                                          int32_t promised_stream_id) {
  return nullptr;
}

int HttpsUpstream::on_downstream_push_promise_complete(
    Downstream *downstream, Downstream *promised_downstream) {
  return -1;
}

bool HttpsUpstream::push_enabled() const { return false; }

void HttpsUpstream::cancel_premature_downstream(
    Downstream *promised_downstream) {}

} // namespace shrpx
