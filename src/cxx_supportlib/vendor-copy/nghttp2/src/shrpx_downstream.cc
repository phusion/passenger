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
#include "shrpx_downstream.h"

#include <cassert>

#include "http-parser/http_parser.h"

#include "shrpx_upstream.h"
#include "shrpx_client_handler.h"
#include "shrpx_config.h"
#include "shrpx_error.h"
#include "shrpx_downstream_connection.h"
#include "shrpx_downstream_queue.h"
#include "shrpx_worker.h"
#include "shrpx_http2_session.h"
#ifdef HAVE_MRUBY
#include "shrpx_mruby.h"
#endif // HAVE_MRUBY
#include "util.h"
#include "http2.h"

namespace shrpx {

namespace {
void upstream_timeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto downstream = static_cast<Downstream *>(w->data);
  auto upstream = downstream->get_upstream();

  auto which = revents == EV_READ ? "read" : "write";

  if (LOG_ENABLED(INFO)) {
    DLOG(INFO, downstream) << "upstream timeout stream_id="
                           << downstream->get_stream_id() << " event=" << which;
  }

  downstream->disable_upstream_rtimer();
  downstream->disable_upstream_wtimer();

  upstream->on_timeout(downstream);
}
} // namespace

namespace {
void upstream_rtimeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  upstream_timeoutcb(loop, w, EV_READ);
}
} // namespace

namespace {
void upstream_wtimeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  upstream_timeoutcb(loop, w, EV_WRITE);
}
} // namespace

namespace {
void downstream_timeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto downstream = static_cast<Downstream *>(w->data);

  auto which = revents == EV_READ ? "read" : "write";

  if (LOG_ENABLED(INFO)) {
    DLOG(INFO, downstream) << "downstream timeout stream_id="
                           << downstream->get_downstream_stream_id()
                           << " event=" << which;
  }

  downstream->disable_downstream_rtimer();
  downstream->disable_downstream_wtimer();

  auto dconn = downstream->get_downstream_connection();

  if (dconn) {
    dconn->on_timeout();
  }
}
} // namespace

namespace {
void downstream_rtimeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  downstream_timeoutcb(loop, w, EV_READ);
}
} // namespace

namespace {
void downstream_wtimeoutcb(struct ev_loop *loop, ev_timer *w, int revents) {
  downstream_timeoutcb(loop, w, EV_WRITE);
}
} // namespace

// upstream could be nullptr for unittests
Downstream::Downstream(Upstream *upstream, MemchunkPool *mcpool,
                       int32_t stream_id)
    : dlnext(nullptr), dlprev(nullptr), response_sent_body_length(0),
      request_start_time_(std::chrono::high_resolution_clock::now()),
      request_buf_(mcpool), response_buf_(mcpool), upstream_(upstream),
      blocked_link_(nullptr), num_retry_(0), stream_id_(stream_id),
      assoc_stream_id_(-1), downstream_stream_id_(-1),
      response_rst_stream_error_code_(NGHTTP2_NO_ERROR),
      request_state_(INITIAL), response_state_(INITIAL),
      dispatch_state_(DISPATCH_NONE), upgraded_(false), chunked_request_(false),
      chunked_response_(false), expect_final_response_(false),
      request_pending_(false) {

  auto &timeoutconf = get_config()->http2.timeout;

  ev_timer_init(&upstream_rtimer_, &upstream_rtimeoutcb, 0.,
                timeoutconf.stream_read);
  ev_timer_init(&upstream_wtimer_, &upstream_wtimeoutcb, 0.,
                timeoutconf.stream_write);
  ev_timer_init(&downstream_rtimer_, &downstream_rtimeoutcb, 0.,
                timeoutconf.stream_read);
  ev_timer_init(&downstream_wtimer_, &downstream_wtimeoutcb, 0.,
                timeoutconf.stream_write);

  upstream_rtimer_.data = this;
  upstream_wtimer_.data = this;
  downstream_rtimer_.data = this;
  downstream_wtimer_.data = this;
}

Downstream::~Downstream() {
  if (LOG_ENABLED(INFO)) {
    DLOG(INFO, this) << "Deleting";
  }

  // check nullptr for unittest
  if (upstream_) {
    auto loop = upstream_->get_client_handler()->get_loop();

    ev_timer_stop(loop, &upstream_rtimer_);
    ev_timer_stop(loop, &upstream_wtimer_);
    ev_timer_stop(loop, &downstream_rtimer_);
    ev_timer_stop(loop, &downstream_wtimer_);

#ifdef HAVE_MRUBY
    auto handler = upstream_->get_client_handler();
    auto worker = handler->get_worker();
    auto mruby_ctx = worker->get_mruby_context();

    mruby_ctx->delete_downstream(this);
#endif // HAVE_MRUBY
  }

  // DownstreamConnection may refer to this object.  Delete it now
  // explicitly.
  dconn_.reset();

  if (LOG_ENABLED(INFO)) {
    DLOG(INFO, this) << "Deleted";
  }
}

int Downstream::attach_downstream_connection(
    std::unique_ptr<DownstreamConnection> dconn) {
  if (dconn->attach_downstream(this) != 0) {
    return -1;
  }

  dconn_ = std::move(dconn);

  return 0;
}

void Downstream::detach_downstream_connection() {
  if (!dconn_) {
    return;
  }

  dconn_->detach_downstream(this);

  auto handler = dconn_->get_client_handler();

  handler->pool_downstream_connection(
      std::unique_ptr<DownstreamConnection>(dconn_.release()));
}

DownstreamConnection *Downstream::get_downstream_connection() {
  return dconn_.get();
}

std::unique_ptr<DownstreamConnection> Downstream::pop_downstream_connection() {
  return std::unique_ptr<DownstreamConnection>(dconn_.release());
}

void Downstream::pause_read(IOCtrlReason reason) {
  if (dconn_) {
    dconn_->pause_read(reason);
  }
}

int Downstream::resume_read(IOCtrlReason reason, size_t consumed) {
  if (dconn_) {
    return dconn_->resume_read(reason, consumed);
  }

  return 0;
}

void Downstream::force_resume_read() {
  if (dconn_) {
    dconn_->force_resume_read();
  }
}

namespace {
const Headers::value_type *search_header_linear(const Headers &headers,
                                                const StringRef &name) {
  const Headers::value_type *res = nullptr;
  for (auto &kv : headers) {
    if (kv.name == name) {
      res = &kv;
    }
  }
  return res;
}
} // namespace

std::string Downstream::assemble_request_cookie() const {
  std::string cookie;
  cookie = "";
  for (auto &kv : req_.fs.headers()) {
    if (kv.name.size() != 6 || kv.name[5] != 'e' ||
        !util::streq_l("cooki", kv.name.c_str(), 5)) {
      continue;
    }

    auto end = kv.value.find_last_not_of(" ;");
    if (end == std::string::npos) {
      cookie += kv.value;
    } else {
      cookie.append(std::begin(kv.value), std::begin(kv.value) + end + 1);
    }
    cookie += "; ";
  }
  if (cookie.size() >= 2) {
    cookie.erase(cookie.size() - 2);
  }

  return cookie;
}

size_t Downstream::count_crumble_request_cookie() {
  size_t n = 0;
  for (auto &kv : req_.fs.headers()) {
    if (kv.name.size() != 6 || kv.name[5] != 'e' ||
        !util::streq_l("cooki", kv.name.c_str(), 5)) {
      continue;
    }
    size_t last = kv.value.size();

    for (size_t j = 0; j < last;) {
      j = kv.value.find_first_not_of("\t ;", j);
      if (j == std::string::npos) {
        break;
      }

      j = kv.value.find(';', j);
      if (j == std::string::npos) {
        j = last;
      }

      ++n;
    }
  }
  return n;
}

void Downstream::crumble_request_cookie(std::vector<nghttp2_nv> &nva) {
  for (auto &kv : req_.fs.headers()) {
    if (kv.name.size() != 6 || kv.name[5] != 'e' ||
        !util::streq_l("cooki", kv.name.c_str(), 5)) {
      continue;
    }
    size_t last = kv.value.size();

    for (size_t j = 0; j < last;) {
      j = kv.value.find_first_not_of("\t ;", j);
      if (j == std::string::npos) {
        break;
      }
      auto first = j;

      j = kv.value.find(';', j);
      if (j == std::string::npos) {
        j = last;
      }

      nva.push_back({(uint8_t *)"cookie", (uint8_t *)kv.value.c_str() + first,
                     str_size("cookie"), j - first,
                     (uint8_t)(NGHTTP2_NV_FLAG_NO_COPY_NAME |
                               NGHTTP2_NV_FLAG_NO_COPY_VALUE |
                               (kv.no_index ? NGHTTP2_NV_FLAG_NO_INDEX : 0))});
    }
  }
}

namespace {
void add_header(bool &key_prev, size_t &sum, Headers &headers, std::string name,
                std::string value) {
  key_prev = true;
  sum += name.size() + value.size();
  headers.emplace_back(std::move(name), std::move(value));
}
} // namespace

namespace {
void add_header(size_t &sum, Headers &headers, const uint8_t *name,
                size_t namelen, const uint8_t *value, size_t valuelen,
                bool no_index, int16_t token) {
  sum += namelen + valuelen;
  headers.emplace_back(
      std::string(reinterpret_cast<const char *>(name), namelen),
      std::string(reinterpret_cast<const char *>(value), valuelen), no_index,
      token);
}
} // namespace

namespace {
void append_last_header_key(bool &key_prev, size_t &sum, Headers &headers,
                            const char *data, size_t len) {
  assert(key_prev);
  sum += len;
  auto &item = headers.back();
  item.name.append(data, len);
}
} // namespace

namespace {
void append_last_header_value(bool &key_prev, size_t &sum, Headers &headers,
                              const char *data, size_t len) {
  key_prev = false;
  sum += len;
  auto &item = headers.back();
  item.value.append(data, len);
}
} // namespace

int FieldStore::index_headers() {
  http2::init_hdidx(hdidx_);
  content_length = -1;

  for (size_t i = 0; i < headers_.size(); ++i) {
    auto &kv = headers_[i];
    util::inp_strlower(kv.name);

    auto token = http2::lookup_token(
        reinterpret_cast<const uint8_t *>(kv.name.c_str()), kv.name.size());
    if (token < 0) {
      continue;
    }

    kv.token = token;
    http2::index_header(hdidx_, token, i);

    if (token == http2::HD_CONTENT_LENGTH) {
      auto len = util::parse_uint(kv.value);
      if (len == -1) {
        return -1;
      }
      if (content_length != -1) {
        return -1;
      }
      content_length = len;
    }
  }
  return 0;
}

const Headers::value_type *FieldStore::header(int16_t token) const {
  return http2::get_header(hdidx_, token, headers_);
}

Headers::value_type *FieldStore::header(int16_t token) {
  return http2::get_header(hdidx_, token, headers_);
}

const Headers::value_type *FieldStore::header(const StringRef &name) const {
  return search_header_linear(headers_, name);
}

void FieldStore::add_header(std::string name, std::string value) {
  shrpx::add_header(header_key_prev_, buffer_size_, headers_, std::move(name),
                    std::move(value));
}

void FieldStore::add_header(std::string name, std::string value,
                            int16_t token) {
  http2::index_header(hdidx_, token, headers_.size());
  buffer_size_ += name.size() + value.size();
  headers_.emplace_back(std::move(name), std::move(value), false, token);
}

void FieldStore::add_header(const uint8_t *name, size_t namelen,
                            const uint8_t *value, size_t valuelen,
                            bool no_index, int16_t token) {
  http2::index_header(hdidx_, token, headers_.size());
  shrpx::add_header(buffer_size_, headers_, name, namelen, value, valuelen,
                    no_index, token);
}

void FieldStore::append_last_header_key(const char *data, size_t len) {
  shrpx::append_last_header_key(header_key_prev_, buffer_size_, headers_, data,
                                len);
}

void FieldStore::append_last_header_value(const char *data, size_t len) {
  shrpx::append_last_header_value(header_key_prev_, buffer_size_, headers_,
                                  data, len);
}

void FieldStore::clear_headers() {
  headers_.clear();
  http2::init_hdidx(hdidx_);
}

void FieldStore::add_trailer(const uint8_t *name, size_t namelen,
                             const uint8_t *value, size_t valuelen,
                             bool no_index, int16_t token) {
  // we never index trailer fields.  Header size limit should be
  // applied to all header and trailer fields combined.
  shrpx::add_header(buffer_size_, trailers_, name, namelen, value, valuelen,
                    no_index, -1);
}

void FieldStore::add_trailer(std::string name, std::string value) {
  shrpx::add_header(trailer_key_prev_, buffer_size_, trailers_, std::move(name),
                    std::move(value));
}

void FieldStore::append_last_trailer_key(const char *data, size_t len) {
  shrpx::append_last_header_key(trailer_key_prev_, buffer_size_, trailers_,
                                data, len);
}

void FieldStore::append_last_trailer_value(const char *data, size_t len) {
  shrpx::append_last_header_value(trailer_key_prev_, buffer_size_, trailers_,
                                  data, len);
}

void Downstream::set_request_start_time(
    std::chrono::high_resolution_clock::time_point time) {
  request_start_time_ = std::move(time);
}

const std::chrono::high_resolution_clock::time_point &
Downstream::get_request_start_time() const {
  return request_start_time_;
}

void Downstream::reset_upstream(Upstream *upstream) {
  upstream_ = upstream;
  if (dconn_) {
    dconn_->on_upstream_change(upstream);
  }
}

Upstream *Downstream::get_upstream() const { return upstream_; }

void Downstream::set_stream_id(int32_t stream_id) { stream_id_ = stream_id; }

int32_t Downstream::get_stream_id() const { return stream_id_; }

void Downstream::set_request_state(int state) { request_state_ = state; }

int Downstream::get_request_state() const { return request_state_; }

bool Downstream::get_chunked_request() const { return chunked_request_; }

void Downstream::set_chunked_request(bool f) { chunked_request_ = f; }

bool Downstream::request_buf_full() {
  if (dconn_) {
    return request_buf_.rleft() >=
           get_config()->conn.downstream.request_buffer_size;
  } else {
    return false;
  }
}

DefaultMemchunks *Downstream::get_request_buf() { return &request_buf_; }

// Call this function after this object is attached to
// Downstream. Otherwise, the program will crash.
int Downstream::push_request_headers() {
  if (!dconn_) {
    DLOG(INFO, this) << "dconn_ is NULL";
    return -1;
  }
  return dconn_->push_request_headers();
}

int Downstream::push_upload_data_chunk(const uint8_t *data, size_t datalen) {
  // Assumes that request headers have already been pushed to output
  // buffer using push_request_headers().
  if (!dconn_) {
    DLOG(INFO, this) << "dconn_ is NULL";
    return -1;
  }
  req_.recv_body_length += datalen;
  if (dconn_->push_upload_data_chunk(data, datalen) != 0) {
    return -1;
  }

  req_.unconsumed_body_length += datalen;

  return 0;
}

int Downstream::end_upload_data() {
  if (!dconn_) {
    DLOG(INFO, this) << "dconn_ is NULL";
    return -1;
  }
  return dconn_->end_upload_data();
}

void Downstream::rewrite_location_response_header(
    const std::string &upstream_scheme) {
  auto hd = resp_.fs.header(http2::HD_LOCATION);
  if (!hd) {
    return;
  }

  if (request_downstream_host_.empty() || req_.authority.empty()) {
    return;
  }

  http_parser_url u{};
  auto rv = http_parser_parse_url(hd->value.c_str(), hd->value.size(), 0, &u);
  if (rv != 0) {
    return;
  }

  auto new_uri = http2::rewrite_location_uri(
      hd->value, u, request_downstream_host_, req_.authority, upstream_scheme);

  if (new_uri.empty()) {
    return;
  }

  hd->value = std::move(new_uri);
}

bool Downstream::get_chunked_response() const { return chunked_response_; }

void Downstream::set_chunked_response(bool f) { chunked_response_ = f; }

int Downstream::on_read() {
  if (!dconn_) {
    DLOG(INFO, this) << "dconn_ is NULL";
    return -1;
  }
  return dconn_->on_read();
}

void Downstream::set_response_state(int state) { response_state_ = state; }

int Downstream::get_response_state() const { return response_state_; }

DefaultMemchunks *Downstream::get_response_buf() { return &response_buf_; }

bool Downstream::response_buf_full() {
  if (dconn_) {
    return response_buf_.rleft() >=
           get_config()->conn.downstream.response_buffer_size;
  } else {
    return false;
  }
}

bool Downstream::validate_request_recv_body_length() const {
  if (req_.fs.content_length == -1) {
    return true;
  }

  if (req_.fs.content_length != req_.recv_body_length) {
    if (LOG_ENABLED(INFO)) {
      DLOG(INFO, this) << "request invalid bodylen: content-length="
                       << req_.fs.content_length
                       << ", received=" << req_.recv_body_length;
    }
    return false;
  }

  return true;
}

bool Downstream::validate_response_recv_body_length() const {
  if (!expect_response_body() || resp_.fs.content_length == -1) {
    return true;
  }

  if (resp_.fs.content_length != resp_.recv_body_length) {
    if (LOG_ENABLED(INFO)) {
      DLOG(INFO, this) << "response invalid bodylen: content-length="
                       << resp_.fs.content_length
                       << ", received=" << resp_.recv_body_length;
    }
    return false;
  }

  return true;
}

void Downstream::check_upgrade_fulfilled() {
  if (req_.method == HTTP_CONNECT) {
    upgraded_ = 200 <= resp_.http_status && resp_.http_status < 300;

    return;
  }

  if (resp_.http_status == 101) {
    // TODO Do more strict checking for upgrade headers
    upgraded_ = req_.upgrade_request;

    return;
  }
}

void Downstream::inspect_http2_request() {
  if (req_.method == HTTP_CONNECT) {
    req_.upgrade_request = true;
  }
}

void Downstream::inspect_http1_request() {
  if (req_.method == HTTP_CONNECT) {
    req_.upgrade_request = true;
  } else {
    auto upgrade = req_.fs.header(http2::HD_UPGRADE);
    if (upgrade) {
      const auto &val = upgrade->value;
      // TODO Perform more strict checking for upgrade headers
      if (util::streq_l(NGHTTP2_CLEARTEXT_PROTO_VERSION_ID, val.c_str(),
                        val.size())) {
        req_.http2_upgrade_seen = true;
      } else {
        req_.upgrade_request = true;
      }
    }
  }
  auto transfer_encoding = req_.fs.header(http2::HD_TRANSFER_ENCODING);
  if (transfer_encoding) {
    req_.fs.content_length = -1;
    if (util::iends_with_l(transfer_encoding->value, "chunked")) {
      chunked_request_ = true;
    }
  }
}

void Downstream::inspect_http1_response() {
  auto transfer_encoding = resp_.fs.header(http2::HD_TRANSFER_ENCODING);
  if (transfer_encoding) {
    resp_.fs.content_length = -1;
    if (util::iends_with_l(transfer_encoding->value, "chunked")) {
      chunked_response_ = true;
    }
  }
}

void Downstream::reset_response() {
  resp_.http_status = 0;
  resp_.http_major = 1;
  resp_.http_minor = 1;
}

bool Downstream::get_non_final_response() const {
  return !upgraded_ && resp_.http_status / 100 == 1;
}

bool Downstream::get_upgraded() const { return upgraded_; }

bool Downstream::get_http2_upgrade_request() const {
  return req_.http2_upgrade_seen && req_.fs.header(http2::HD_HTTP2_SETTINGS) &&
         response_state_ == INITIAL;
}

const std::string &Downstream::get_http2_settings() const {
  auto http2_settings = req_.fs.header(http2::HD_HTTP2_SETTINGS);
  if (!http2_settings) {
    return EMPTY_STRING;
  }
  return http2_settings->value;
}

void Downstream::set_downstream_stream_id(int32_t stream_id) {
  downstream_stream_id_ = stream_id;
}

int32_t Downstream::get_downstream_stream_id() const {
  return downstream_stream_id_;
}

uint32_t Downstream::get_response_rst_stream_error_code() const {
  return response_rst_stream_error_code_;
}

void Downstream::set_response_rst_stream_error_code(uint32_t error_code) {
  response_rst_stream_error_code_ = error_code;
}

void Downstream::set_expect_final_response(bool f) {
  expect_final_response_ = f;
}

bool Downstream::get_expect_final_response() const {
  return expect_final_response_;
}

bool Downstream::expect_response_body() const {
  return http2::expect_response_body(req_.method, resp_.http_status);
}

namespace {
void reset_timer(struct ev_loop *loop, ev_timer *w) { ev_timer_again(loop, w); }
} // namespace

namespace {
void try_reset_timer(struct ev_loop *loop, ev_timer *w) {
  if (!ev_is_active(w)) {
    return;
  }
  ev_timer_again(loop, w);
}
} // namespace

namespace {
void ensure_timer(struct ev_loop *loop, ev_timer *w) {
  if (ev_is_active(w)) {
    return;
  }
  ev_timer_again(loop, w);
}
} // namespace

namespace {
void disable_timer(struct ev_loop *loop, ev_timer *w) {
  ev_timer_stop(loop, w);
}
} // namespace

void Downstream::reset_upstream_rtimer() {
  if (get_config()->http2.timeout.stream_read == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  reset_timer(loop, &upstream_rtimer_);
}

void Downstream::reset_upstream_wtimer() {
  auto loop = upstream_->get_client_handler()->get_loop();
  auto &timeoutconf = get_config()->http2.timeout;

  if (timeoutconf.stream_write != 0.) {
    reset_timer(loop, &upstream_wtimer_);
  }
  if (timeoutconf.stream_read != 0.) {
    try_reset_timer(loop, &upstream_rtimer_);
  }
}

void Downstream::ensure_upstream_wtimer() {
  if (get_config()->http2.timeout.stream_write == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  ensure_timer(loop, &upstream_wtimer_);
}

void Downstream::disable_upstream_rtimer() {
  if (get_config()->http2.timeout.stream_read == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  disable_timer(loop, &upstream_rtimer_);
}

void Downstream::disable_upstream_wtimer() {
  if (get_config()->http2.timeout.stream_write == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  disable_timer(loop, &upstream_wtimer_);
}

void Downstream::reset_downstream_rtimer() {
  if (get_config()->http2.timeout.stream_read == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  reset_timer(loop, &downstream_rtimer_);
}

void Downstream::reset_downstream_wtimer() {
  auto loop = upstream_->get_client_handler()->get_loop();
  auto &timeoutconf = get_config()->http2.timeout;

  if (timeoutconf.stream_write != 0.) {
    reset_timer(loop, &downstream_wtimer_);
  }
  if (timeoutconf.stream_read != 0.) {
    try_reset_timer(loop, &downstream_rtimer_);
  }
}

void Downstream::ensure_downstream_wtimer() {
  if (get_config()->http2.timeout.stream_write == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  ensure_timer(loop, &downstream_wtimer_);
}

void Downstream::disable_downstream_rtimer() {
  if (get_config()->http2.timeout.stream_read == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  disable_timer(loop, &downstream_rtimer_);
}

void Downstream::disable_downstream_wtimer() {
  if (get_config()->http2.timeout.stream_write == 0.) {
    return;
  }
  auto loop = upstream_->get_client_handler()->get_loop();
  disable_timer(loop, &downstream_wtimer_);
}

bool Downstream::accesslog_ready() const { return resp_.http_status > 0; }

void Downstream::add_retry() { ++num_retry_; }

bool Downstream::no_more_retry() const { return num_retry_ > 5; }

void Downstream::set_request_downstream_host(std::string host) {
  request_downstream_host_ = std::move(host);
}

void Downstream::set_request_pending(bool f) { request_pending_ = f; }

bool Downstream::get_request_pending() const { return request_pending_; }

bool Downstream::request_submission_ready() const {
  return (request_state_ == Downstream::HEADER_COMPLETE ||
          request_state_ == Downstream::MSG_COMPLETE) &&
         request_pending_ && response_state_ == Downstream::INITIAL;
}

int Downstream::get_dispatch_state() const { return dispatch_state_; }

void Downstream::set_dispatch_state(int s) { dispatch_state_ = s; }

void Downstream::attach_blocked_link(BlockedLink *l) {
  assert(!blocked_link_);

  l->downstream = this;
  blocked_link_ = l;
}

BlockedLink *Downstream::detach_blocked_link() {
  auto link = blocked_link_;
  blocked_link_ = nullptr;
  return link;
}

bool Downstream::can_detach_downstream_connection() const {
  return dconn_ && response_state_ == Downstream::MSG_COMPLETE &&
         request_state_ == Downstream::MSG_COMPLETE && !upgraded_ &&
         !resp_.connection_close;
}

DefaultMemchunks Downstream::pop_response_buf() {
  return std::move(response_buf_);
}

void Downstream::set_assoc_stream_id(int32_t stream_id) {
  assoc_stream_id_ = stream_id;
}

int32_t Downstream::get_assoc_stream_id() const { return assoc_stream_id_; }

} // namespace shrpx
