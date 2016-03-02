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
#include "shrpx_http2_downstream_connection.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H

#include "http-parser/http_parser.h"

#include "shrpx_client_handler.h"
#include "shrpx_upstream.h"
#include "shrpx_downstream.h"
#include "shrpx_config.h"
#include "shrpx_error.h"
#include "shrpx_http.h"
#include "shrpx_http2_session.h"
#include "http2.h"
#include "util.h"

using namespace nghttp2;

namespace shrpx {

Http2DownstreamConnection::Http2DownstreamConnection(
    DownstreamConnectionPool *dconn_pool, Http2Session *http2session)
    : DownstreamConnection(dconn_pool), dlnext(nullptr), dlprev(nullptr),
      http2session_(http2session), sd_(nullptr) {}

Http2DownstreamConnection::~Http2DownstreamConnection() {
  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, this) << "Deleting";
  }
  if (downstream_) {
    downstream_->disable_downstream_rtimer();
    downstream_->disable_downstream_wtimer();

    uint32_t error_code;
    if (downstream_->get_request_state() == Downstream::STREAM_CLOSED &&
        downstream_->get_upgraded()) {
      // For upgraded connection, send NO_ERROR.  Should we consider
      // request states other than Downstream::STREAM_CLOSED ?
      error_code = NGHTTP2_NO_ERROR;
    } else {
      error_code = NGHTTP2_INTERNAL_ERROR;
    }

    if (http2session_->get_state() == Http2Session::CONNECTED &&
        downstream_->get_downstream_stream_id() != -1) {
      submit_rst_stream(downstream_, error_code);

      auto &resp = downstream_->response();

      http2session_->consume(downstream_->get_downstream_stream_id(),
                             resp.unconsumed_body_length);

      resp.unconsumed_body_length = 0;

      http2session_->signal_write();
    }
  }
  http2session_->remove_downstream_connection(this);

  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, this) << "Deleted";
  }
}

int Http2DownstreamConnection::attach_downstream(Downstream *downstream) {
  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, this) << "Attaching to DOWNSTREAM:" << downstream;
  }
  http2session_->add_downstream_connection(this);
  if (http2session_->get_state() == Http2Session::DISCONNECTED) {
    http2session_->signal_write();
  }

  downstream_ = downstream;
  downstream_->reset_downstream_rtimer();

  return 0;
}

void Http2DownstreamConnection::detach_downstream(Downstream *downstream) {
  if (LOG_ENABLED(INFO)) {
    DCLOG(INFO, this) << "Detaching from DOWNSTREAM:" << downstream;
  }

  auto &resp = downstream_->response();

  if (submit_rst_stream(downstream) == 0) {
    http2session_->signal_write();
  }

  if (downstream_->get_downstream_stream_id() != -1) {
    http2session_->consume(downstream_->get_downstream_stream_id(),
                           resp.unconsumed_body_length);

    resp.unconsumed_body_length = 0;

    http2session_->signal_write();
  }

  downstream->disable_downstream_rtimer();
  downstream->disable_downstream_wtimer();
  downstream_ = nullptr;
}

int Http2DownstreamConnection::submit_rst_stream(Downstream *downstream,
                                                 uint32_t error_code) {
  int rv = -1;
  if (http2session_->get_state() == Http2Session::CONNECTED &&
      downstream->get_downstream_stream_id() != -1) {
    switch (downstream->get_response_state()) {
    case Downstream::MSG_RESET:
    case Downstream::MSG_BAD_HEADER:
    case Downstream::MSG_COMPLETE:
      break;
    default:
      if (LOG_ENABLED(INFO)) {
        DCLOG(INFO, this) << "Submit RST_STREAM for DOWNSTREAM:" << downstream
                          << ", stream_id="
                          << downstream->get_downstream_stream_id()
                          << ", error_code=" << error_code;
      }
      rv = http2session_->submit_rst_stream(
          downstream->get_downstream_stream_id(), error_code);
    }
  }
  return rv;
}

namespace {
ssize_t http2_data_read_callback(nghttp2_session *session, int32_t stream_id,
                                 uint8_t *buf, size_t length,
                                 uint32_t *data_flags,
                                 nghttp2_data_source *source, void *user_data) {
  int rv;
  auto sd = static_cast<StreamData *>(
      nghttp2_session_get_stream_user_data(session, stream_id));
  if (!sd || !sd->dconn) {
    return NGHTTP2_ERR_DEFERRED;
  }
  auto dconn = static_cast<Http2DownstreamConnection *>(source->ptr);
  auto downstream = dconn->get_downstream();
  if (!downstream) {
    // In this case, RST_STREAM should have been issued. But depending
    // on the priority, DATA frame may come first.
    return NGHTTP2_ERR_DEFERRED;
  }
  const auto &req = downstream->request();
  auto input = downstream->get_request_buf();
  auto nread = input->remove(buf, length);
  auto input_empty = input->rleft() == 0;

  if (nread > 0) {
    // This is important because it will handle flow control
    // stuff.
    if (downstream->get_upstream()->resume_read(SHRPX_NO_BUFFER, downstream,
                                                nread) != 0) {
      // In this case, downstream may be deleted.
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    // Check dconn is still alive because Upstream::resume_read()
    // may delete downstream which will delete dconn.
    if (sd->dconn == nullptr) {
      return NGHTTP2_ERR_DEFERRED;
    }
  }

  if (input_empty &&
      downstream->get_request_state() == Downstream::MSG_COMPLETE &&
      // If connection is upgraded, don't set EOF flag, since HTTP/1
      // will set MSG_COMPLETE to request state after upgrade response
      // header is seen.
      (!req.upgrade_request ||
       (downstream->get_response_state() == Downstream::HEADER_COMPLETE &&
        !downstream->get_upgraded()))) {

    *data_flags |= NGHTTP2_DATA_FLAG_EOF;

    const auto &trailers = req.fs.trailers();
    if (!trailers.empty()) {
      std::vector<nghttp2_nv> nva;
      nva.reserve(trailers.size());
      // We cannot use nocopy version, since nva may be touched after
      // Downstream object is deleted.
      http2::copy_headers_to_nva(nva, trailers);
      if (!nva.empty()) {
        rv = nghttp2_submit_trailer(session, stream_id, nva.data(), nva.size());
        if (rv != 0) {
          if (nghttp2_is_fatal(rv)) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
          }
        } else {
          *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
        }
      }
    }
  }

  if (!input_empty) {
    downstream->reset_downstream_wtimer();
  } else {
    downstream->disable_downstream_wtimer();
  }

  if (nread == 0 && (*data_flags & NGHTTP2_DATA_FLAG_EOF) == 0) {
    downstream->disable_downstream_wtimer();

    return NGHTTP2_ERR_DEFERRED;
  }

  return nread;
}
} // namespace

int Http2DownstreamConnection::push_request_headers() {
  int rv;
  if (!downstream_) {
    return 0;
  }
  if (!http2session_->can_push_request()) {
    // The HTTP2 session to the backend has not been established or
    // connection is now being checked.  This function will be called
    // again just after it is established.
    downstream_->set_request_pending(true);
    http2session_->start_checking_connection();
    return 0;
  }

  downstream_->set_request_pending(false);

  const auto &req = downstream_->request();

  auto &httpconf = get_config()->http;
  auto &http2conf = get_config()->http2;

  auto no_host_rewrite =
      httpconf.no_host_rewrite || get_config()->http2_proxy ||
      get_config()->client_proxy || req.method == HTTP_CONNECT;

  // http2session_ has already in CONNECTED state, so we can get
  // addr_idx here.
  auto addr_idx = http2session_->get_addr_idx();
  auto group = http2session_->get_group();
  const auto &downstream_hostport =
      get_config()->conn.downstream.addr_groups[group].addrs[addr_idx].hostport;

  // For HTTP/1.0 request, there is no authority in request.  In that
  // case, we use backend server's host nonetheless.
  auto authority = StringRef(downstream_hostport);

  if (no_host_rewrite && !req.authority.empty()) {
    authority = StringRef(req.authority);
  }

  downstream_->set_request_downstream_host(authority.str());

  size_t num_cookies = 0;
  if (!http2conf.no_cookie_crumbling) {
    num_cookies = downstream_->count_crumble_request_cookie();
  }

  // 9 means:
  // 1. :method
  // 2. :scheme
  // 3. :path
  // 4. :authority (or host)
  // 5. via (optional)
  // 6. x-forwarded-for (optional)
  // 7. x-forwarded-proto (optional)
  // 8. te (optional)
  // 9. forwarded (optional)
  auto nva = std::vector<nghttp2_nv>();
  nva.reserve(req.fs.headers().size() + 9 + num_cookies +
              httpconf.add_request_headers.size());

  nva.push_back(
      http2::make_nv_lc_nocopy(":method", http2::to_method_string(req.method)));

  if (req.method != HTTP_CONNECT) {
    assert(!req.scheme.empty());

    nva.push_back(http2::make_nv_ls_nocopy(":scheme", req.scheme));

    if (req.method == HTTP_OPTIONS && req.path.empty()) {
      nva.push_back(http2::make_nv_ll(":path", "*"));
    } else {
      nva.push_back(http2::make_nv_ls_nocopy(":path", req.path));
    }

    if (!req.no_authority) {
      nva.push_back(http2::make_nv_ls_nocopy(":authority", authority));
    } else {
      nva.push_back(http2::make_nv_ls_nocopy("host", authority));
    }
  } else {
    nva.push_back(http2::make_nv_ls_nocopy(":authority", authority));
  }

  http2::copy_headers_to_nva_nocopy(nva, req.fs.headers());

  bool chunked_encoding = false;
  auto transfer_encoding = req.fs.header(http2::HD_TRANSFER_ENCODING);
  if (transfer_encoding &&
      util::strieq_l("chunked", (*transfer_encoding).value)) {
    chunked_encoding = true;
  }

  if (!http2conf.no_cookie_crumbling) {
    downstream_->crumble_request_cookie(nva);
  }

  auto upstream = downstream_->get_upstream();
  auto handler = upstream->get_client_handler();

  std::string forwarded_value;

  auto &fwdconf = httpconf.forwarded;

  auto fwd =
      fwdconf.strip_incoming ? nullptr : req.fs.header(http2::HD_FORWARDED);

  if (fwdconf.params) {
    auto params = fwdconf.params;

    if (get_config()->http2_proxy || get_config()->client_proxy ||
        req.method == HTTP_CONNECT) {
      params &= ~FORWARDED_PROTO;
    }

    auto value = http::create_forwarded(params, handler->get_forwarded_by(),
                                        handler->get_forwarded_for(),
                                        req.authority, req.scheme);
    if (fwd || !value.empty()) {
      if (fwd) {
        forwarded_value = fwd->value;

        if (!value.empty()) {
          forwarded_value += ", ";
        }
      }

      forwarded_value += value;

      nva.push_back(http2::make_nv_ls("forwarded", forwarded_value));
    }
  } else if (fwd) {
    nva.push_back(http2::make_nv_ls_nocopy("forwarded", fwd->value));
    forwarded_value = fwd->value;
  }

  auto &xffconf = httpconf.xff;

  auto xff = xffconf.strip_incoming ? nullptr
                                    : req.fs.header(http2::HD_X_FORWARDED_FOR);

  std::string xff_value;

  if (xffconf.add) {
    if (xff) {
      xff_value = (*xff).value;
      xff_value += ", ";
    }
    xff_value += upstream->get_client_handler()->get_ipaddr();
    nva.push_back(http2::make_nv_ls("x-forwarded-for", xff_value));
  } else if (xff) {
    nva.push_back(http2::make_nv_ls_nocopy("x-forwarded-for", (*xff).value));
  }

  if (!get_config()->http2_proxy && !get_config()->client_proxy &&
      req.method != HTTP_CONNECT) {
    // We use same protocol with :scheme header field
    nva.push_back(http2::make_nv_ls_nocopy("x-forwarded-proto", req.scheme));
  }

  std::string via_value;
  auto via = req.fs.header(http2::HD_VIA);
  if (httpconf.no_via) {
    if (via) {
      nva.push_back(http2::make_nv_ls_nocopy("via", (*via).value));
    }
  } else {
    if (via) {
      via_value = (*via).value;
      via_value += ", ";
    }
    via_value += http::create_via_header_value(req.http_major, req.http_minor);
    nva.push_back(http2::make_nv_ls("via", via_value));
  }

  auto te = req.fs.header(http2::HD_TE);
  // HTTP/1 upstream request can contain keyword other than
  // "trailers".  We just forward "trailers".
  // TODO more strict handling required here.
  if (te && util::strifind(te->value.c_str(), "trailers")) {
    nva.push_back(http2::make_nv_ll("te", "trailers"));
  }

  for (auto &p : httpconf.add_request_headers) {
    nva.push_back(http2::make_nv_nocopy(p.first, p.second));
  }

  if (LOG_ENABLED(INFO)) {
    std::stringstream ss;
    for (auto &nv : nva) {
      ss << TTY_HTTP_HD << nv.name << TTY_RST << ": " << nv.value << "\n";
    }
    DCLOG(INFO, this) << "HTTP request headers\n" << ss.str();
  }

  auto content_length = req.fs.header(http2::HD_CONTENT_LENGTH);
  // TODO check content-length: 0 case

  if (req.method == HTTP_CONNECT || chunked_encoding || content_length ||
      req.http2_expect_body) {
    // Request-body is expected.
    nghttp2_data_provider data_prd;
    data_prd.source.ptr = this;
    data_prd.read_callback = http2_data_read_callback;
    rv = http2session_->submit_request(this, nva.data(), nva.size(), &data_prd);
  } else {
    rv = http2session_->submit_request(this, nva.data(), nva.size(), nullptr);
  }
  if (rv != 0) {
    DCLOG(FATAL, this) << "nghttp2_submit_request() failed";
    return -1;
  }

  downstream_->reset_downstream_wtimer();

  http2session_->signal_write();
  return 0;
}

int Http2DownstreamConnection::push_upload_data_chunk(const uint8_t *data,
                                                      size_t datalen) {
  int rv;
  auto output = downstream_->get_request_buf();
  output->append(data, datalen);
  if (downstream_->get_downstream_stream_id() != -1) {
    rv = http2session_->resume_data(this);
    if (rv != 0) {
      return -1;
    }

    downstream_->ensure_downstream_wtimer();

    http2session_->signal_write();
  }
  return 0;
}

int Http2DownstreamConnection::end_upload_data() {
  int rv;
  if (downstream_->get_downstream_stream_id() != -1) {
    rv = http2session_->resume_data(this);
    if (rv != 0) {
      return -1;
    }

    downstream_->ensure_downstream_wtimer();

    http2session_->signal_write();
  }
  return 0;
}

int Http2DownstreamConnection::resume_read(IOCtrlReason reason,
                                           size_t consumed) {
  int rv;

  if (http2session_->get_state() != Http2Session::CONNECTED ||
      !http2session_->get_flow_control()) {
    return 0;
  }

  if (!downstream_ || downstream_->get_downstream_stream_id() == -1) {
    return 0;
  }

  if (consumed > 0) {
    rv = http2session_->consume(downstream_->get_downstream_stream_id(),
                                consumed);

    if (rv != 0) {
      return -1;
    }

    auto &resp = downstream_->response();

    resp.unconsumed_body_length -= consumed;

    http2session_->signal_write();
  }

  return 0;
}

int Http2DownstreamConnection::on_read() { return 0; }

int Http2DownstreamConnection::on_write() { return 0; }

void Http2DownstreamConnection::attach_stream_data(StreamData *sd) {
  // It is possible sd->dconn is not NULL. sd is detached when
  // on_stream_close_callback. Before that, after MSG_COMPLETE is set
  // to Downstream::set_response_state(), upstream's readcb is called
  // and execution path eventually could reach here. Since the
  // response was already handled, we just detach sd.
  detach_stream_data();
  sd_ = sd;
  sd_->dconn = this;
}

StreamData *Http2DownstreamConnection::detach_stream_data() {
  if (sd_) {
    auto sd = sd_;
    sd_ = nullptr;
    sd->dconn = nullptr;
    return sd;
  }
  return nullptr;
}

int Http2DownstreamConnection::on_timeout() {
  if (!downstream_) {
    return 0;
  }

  return submit_rst_stream(downstream_, NGHTTP2_NO_ERROR);
}

size_t Http2DownstreamConnection::get_group() const {
  // HTTP/2 backend connections are managed by Http2Session object,
  // and it stores group index.
  return http2session_->get_group();
}

} // namespace shrpx
