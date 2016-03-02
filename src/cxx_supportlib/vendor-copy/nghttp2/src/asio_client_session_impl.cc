/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2015 Tatsuhiro Tsujikawa
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
#include "asio_client_session_impl.h"

#include <iostream>

#include "asio_client_stream.h"
#include "asio_client_request_impl.h"
#include "asio_client_response_impl.h"
#include "asio_common.h"
#include "template.h"
#include "util.h"
#include "http2.h"

namespace nghttp2 {
namespace asio_http2 {
namespace client {

session_impl::session_impl(boost::asio::io_service &io_service)
    : wblen_(0), io_service_(io_service), resolver_(io_service),
      deadline_(io_service), connect_timeout_(boost::posix_time::seconds(60)),
      read_timeout_(boost::posix_time::seconds(60)), session_(nullptr),
      data_pending_(nullptr), data_pendinglen_(0), writing_(false),
      inside_callback_(false), stopped_(false) {}

session_impl::~session_impl() {
  // finish up all active stream
  for (auto &p : streams_) {
    auto &strm = p.second;
    auto &req = strm->request().impl();
    req.call_on_close(NGHTTP2_INTERNAL_ERROR);
  }

  nghttp2_session_del(session_);
}

void session_impl::start_resolve(const std::string &host,
                                 const std::string &service) {
  deadline_.expires_from_now(connect_timeout_);

  auto self = this->shared_from_this();

  resolver_.async_resolve({host, service},
                          [this, self](const boost::system::error_code &ec,
                                       tcp::resolver::iterator endpoint_it) {
                            if (ec) {
                              not_connected(ec);
                              return;
                            }

                            start_connect(endpoint_it);
                          });

  deadline_.async_wait(std::bind(&session_impl::handle_deadline, self));
}

void session_impl::handle_deadline() {
  if (stopped_) {
    return;
  }

  if (deadline_.expires_at() <=
      boost::asio::deadline_timer::traits_type::now()) {
    call_error_cb(boost::asio::error::timed_out);
    stop();
    deadline_.expires_at(boost::posix_time::pos_infin);
    return;
  }

  deadline_.async_wait(
      std::bind(&session_impl::handle_deadline, this->shared_from_this()));
}

void session_impl::connected(tcp::resolver::iterator endpoint_it) {
  if (!setup_session()) {
    return;
  }

  socket().set_option(boost::asio::ip::tcp::no_delay(true));

  do_write();
  do_read();

  auto &connect_cb = on_connect();
  if (connect_cb) {
    connect_cb(endpoint_it);
  }
}

void session_impl::not_connected(const boost::system::error_code &ec) {
  call_error_cb(ec);
  stop();
}

void session_impl::on_connect(connect_cb cb) { connect_cb_ = std::move(cb); }

void session_impl::on_error(error_cb cb) { error_cb_ = std::move(cb); }

const connect_cb &session_impl::on_connect() const { return connect_cb_; }

const error_cb &session_impl::on_error() const { return error_cb_; }

void session_impl::call_error_cb(const boost::system::error_code &ec) {
  if (stopped_) {
    return;
  }
  auto &error_cb = on_error();
  if (!error_cb) {
    return;
  }
  error_cb(ec);
}

namespace {
int on_begin_headers_callback(nghttp2_session *session,
                              const nghttp2_frame *frame, void *user_data) {
  if (frame->hd.type != NGHTTP2_PUSH_PROMISE) {
    return 0;
  }

  auto sess = static_cast<session_impl *>(user_data);
  sess->create_push_stream(frame->push_promise.promised_stream_id);

  return 0;
}
} // namespace

namespace {
int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                       const uint8_t *name, size_t namelen,
                       const uint8_t *value, size_t valuelen, uint8_t flags,
                       void *user_data) {
  auto sess = static_cast<session_impl *>(user_data);
  stream *strm;

  switch (frame->hd.type) {
  case NGHTTP2_HEADERS: {
    strm = sess->find_stream(frame->hd.stream_id);
    if (!strm) {
      return 0;
    }

    // ignore trailers
    if (frame->headers.cat == NGHTTP2_HCAT_HEADERS &&
        !strm->expect_final_response()) {
      return 0;
    }

    auto token = http2::lookup_token(name, namelen);

    auto &res = strm->response().impl();
    if (token == http2::HD__STATUS) {
      res.status_code(util::parse_uint(value, valuelen));
    } else {
      if (res.header_buffer_size() + namelen + valuelen > 64_k) {
        nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE,
                                  frame->hd.stream_id, NGHTTP2_INTERNAL_ERROR);
        break;
      }
      res.update_header_buffer_size(namelen + valuelen);

      if (token == http2::HD_CONTENT_LENGTH) {
        res.content_length(util::parse_uint(value, valuelen));
      }

      res.header().emplace(
          std::string(name, name + namelen),
          header_value{std::string(value, value + valuelen),
                       (flags & NGHTTP2_NV_FLAG_NO_INDEX) != 0});
    }
    break;
  }
  case NGHTTP2_PUSH_PROMISE: {
    strm = sess->find_stream(frame->push_promise.promised_stream_id);
    if (!strm) {
      return 0;
    }

    auto &req = strm->request().impl();
    auto &uri = req.uri();

    switch (http2::lookup_token(name, namelen)) {
    case http2::HD__METHOD:
      req.method(std::string(value, value + valuelen));
      break;
    case http2::HD__SCHEME:
      uri.scheme.assign(value, value + valuelen);
      break;
    case http2::HD__PATH:
      split_path(uri, value, value + valuelen);
      break;
    case http2::HD__AUTHORITY:
      uri.host.assign(value, value + valuelen);
      break;
    case http2::HD_HOST:
      if (uri.host.empty()) {
        uri.host.assign(value, value + valuelen);
      }
    // fall through
    default:
      if (req.header_buffer_size() + namelen + valuelen > 64_k) {
        nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE,
                                  frame->hd.stream_id, NGHTTP2_INTERNAL_ERROR);
        break;
      }
      req.update_header_buffer_size(namelen + valuelen);

      req.header().emplace(
          std::string(name, name + namelen),
          header_value{std::string(value, value + valuelen),
                       (flags & NGHTTP2_NV_FLAG_NO_INDEX) != 0});
    }

    break;
  }
  default:
    return 0;
  }

  return 0;
}
} // namespace

namespace {
int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                           void *user_data) {
  auto sess = static_cast<session_impl *>(user_data);
  auto strm = sess->find_stream(frame->hd.stream_id);

  switch (frame->hd.type) {
  case NGHTTP2_DATA: {
    if (!strm) {
      return 0;
    }
    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
      strm->response().impl().call_on_data(nullptr, 0);
    }
    break;
  }
  case NGHTTP2_HEADERS: {
    if (!strm) {
      return 0;
    }

    // ignore trailers
    if (frame->headers.cat == NGHTTP2_HCAT_HEADERS &&
        !strm->expect_final_response()) {
      return 0;
    }

    if (strm->expect_final_response()) {
      // wait for final response
      return 0;
    }

    auto &req = strm->request().impl();
    req.call_on_response(strm->response());
    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
      strm->response().impl().call_on_data(nullptr, 0);
    }
    break;
  }
  case NGHTTP2_PUSH_PROMISE: {
    if (!strm) {
      return 0;
    }

    auto push_strm = sess->find_stream(frame->push_promise.promised_stream_id);
    if (!push_strm) {
      return 0;
    }

    strm->request().impl().call_on_push(push_strm->request());

    break;
  }
  }
  return 0;
}
} // namespace

namespace {
int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                int32_t stream_id, const uint8_t *data,
                                size_t len, void *user_data) {
  auto sess = static_cast<session_impl *>(user_data);
  auto strm = sess->find_stream(stream_id);
  if (!strm) {
    return 0;
  }

  auto &res = strm->response().impl();
  res.call_on_data(data, len);

  return 0;
}
} // namespace

namespace {
int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                             uint32_t error_code, void *user_data) {
  auto sess = static_cast<session_impl *>(user_data);
  auto strm = sess->pop_stream(stream_id);
  if (!strm) {
    return 0;
  }

  strm->request().impl().call_on_close(error_code);

  return 0;
}
} // namespace

bool session_impl::setup_session() {
  nghttp2_session_callbacks *callbacks;
  nghttp2_session_callbacks_new(&callbacks);
  auto cb_del = defer(nghttp2_session_callbacks_del, callbacks);

  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks, on_begin_headers_callback);
  nghttp2_session_callbacks_set_on_header_callback(callbacks,
                                                   on_header_callback);
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
                                                       on_frame_recv_callback);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks, on_data_chunk_recv_callback);
  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks, on_stream_close_callback);

  auto rv = nghttp2_session_client_new(&session_, callbacks, this);
  if (rv != 0) {
    call_error_cb(make_error_code(static_cast<nghttp2_error>(rv)));
    return false;
  }

  const uint32_t window_size = 256_m;

  std::array<nghttp2_settings_entry, 2> iv{
      {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
       // typically client is just a *sink* and just process data as
       // much as possible.  Use large window size by default.
       {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, window_size}}};
  nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv.data(), iv.size());
  // increase connection window size up to window_size
  nghttp2_submit_window_update(session_, NGHTTP2_FLAG_NONE, 0,
                               window_size -
                                   NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE);
  return true;
}

int session_impl::write_trailer(stream &strm, header_map h) {
  int rv;
  auto nva = std::vector<nghttp2_nv>();
  nva.reserve(h.size());
  for (auto &hd : h) {
    nva.push_back(nghttp2::http2::make_nv(hd.first, hd.second.value,
                                          hd.second.sensitive));
  }

  rv = nghttp2_submit_trailer(session_, strm.stream_id(), nva.data(),
                              nva.size());

  if (rv != 0) {
    return -1;
  }

  signal_write();

  return 0;
}

void session_impl::cancel(stream &strm, uint32_t error_code) {
  if (stopped_) {
    return;
  }

  nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, strm.stream_id(),
                            error_code);
  signal_write();
}

void session_impl::resume(stream &strm) {
  if (stopped_) {
    return;
  }

  nghttp2_session_resume_data(session_, strm.stream_id());
  signal_write();
}

stream *session_impl::find_stream(int32_t stream_id) {
  auto it = streams_.find(stream_id);
  if (it == std::end(streams_)) {
    return nullptr;
  }
  return (*it).second.get();
}

std::unique_ptr<stream> session_impl::pop_stream(int32_t stream_id) {
  auto it = streams_.find(stream_id);
  if (it == std::end(streams_)) {
    return nullptr;
  }
  auto strm = std::move((*it).second);
  streams_.erase(it);
  return strm;
}

stream *session_impl::create_push_stream(int32_t stream_id) {
  auto strm = create_stream();
  strm->stream_id(stream_id);
  auto p = streams_.emplace(stream_id, std::move(strm));
  assert(p.second);
  return (*p.first).second.get();
}

std::unique_ptr<stream> session_impl::create_stream() {
  return make_unique<stream>(this);
}

const request *session_impl::submit(boost::system::error_code &ec,
                                    const std::string &method,
                                    const std::string &uri, generator_cb cb,
                                    header_map h) {
  ec.clear();

  if (stopped_) {
    ec = make_error_code(static_cast<nghttp2_error>(NGHTTP2_INTERNAL_ERROR));
    return nullptr;
  }

  http_parser_url u{};
  // TODO Handle CONNECT method
  if (http_parser_parse_url(uri.c_str(), uri.size(), 0, &u) != 0) {
    ec = make_error_code(boost::system::errc::invalid_argument);
    return nullptr;
  }

  if ((u.field_set & (1 << UF_SCHEMA)) == 0 ||
      (u.field_set & (1 << UF_HOST)) == 0) {
    ec = make_error_code(boost::system::errc::invalid_argument);
    return nullptr;
  }

  auto strm = create_stream();
  auto &req = strm->request().impl();
  auto &uref = req.uri();

  http2::copy_url_component(uref.scheme, &u, UF_SCHEMA, uri.c_str());
  http2::copy_url_component(uref.host, &u, UF_HOST, uri.c_str());
  http2::copy_url_component(uref.raw_path, &u, UF_PATH, uri.c_str());
  http2::copy_url_component(uref.raw_query, &u, UF_QUERY, uri.c_str());

  if (util::ipv6_numeric_addr(uref.host.c_str())) {
    uref.host = "[" + uref.host;
    uref.host += ']';
  }
  if (u.field_set & (1 << UF_PORT)) {
    uref.host += ':';
    uref.host += util::utos(u.port);
  }

  if (uref.raw_path.empty()) {
    uref.raw_path = "/";
  }

  uref.path = percent_decode(uref.raw_path);

  auto path = uref.raw_path;
  if (u.field_set & (1 << UF_QUERY)) {
    path += '?';
    path += uref.raw_query;
  }

  auto nva = std::vector<nghttp2_nv>();
  nva.reserve(3 + h.size());
  nva.push_back(http2::make_nv_ls(":method", method));
  nva.push_back(http2::make_nv_ls(":scheme", uref.scheme));
  nva.push_back(http2::make_nv_ls(":path", path));
  nva.push_back(http2::make_nv_ls(":authority", uref.host));
  for (auto &kv : h) {
    nva.push_back(
        http2::make_nv(kv.first, kv.second.value, kv.second.sensitive));
  }

  req.header(std::move(h));

  nghttp2_data_provider *prdptr = nullptr;
  nghttp2_data_provider prd;

  if (cb) {
    strm->request().impl().on_read(std::move(cb));
    prd.source.ptr = strm.get();
    prd.read_callback =
        [](nghttp2_session *session, int32_t stream_id, uint8_t *buf,
           size_t length, uint32_t *data_flags, nghttp2_data_source *source,
           void *user_data) -> ssize_t {
          auto strm = static_cast<stream *>(source->ptr);
          return strm->request().impl().call_on_read(buf, length, data_flags);
        };
    prdptr = &prd;
  }

  auto stream_id = nghttp2_submit_request(session_, nullptr, nva.data(),
                                          nva.size(), prdptr, strm.get());
  if (stream_id < 0) {
    ec = make_error_code(static_cast<nghttp2_error>(stream_id));
    return nullptr;
  }

  signal_write();

  strm->stream_id(stream_id);

  auto p = streams_.emplace(stream_id, std::move(strm));
  assert(p.second);
  return &(*p.first).second->request();
}

void session_impl::shutdown() {
  if (stopped_) {
    return;
  }

  nghttp2_session_terminate_session(session_, NGHTTP2_NO_ERROR);
  signal_write();
}

boost::asio::io_service &session_impl::io_service() { return io_service_; }

void session_impl::signal_write() {
  if (!inside_callback_) {
    do_write();
  }
}

bool session_impl::should_stop() const {
  return !writing_ && !nghttp2_session_want_read(session_) &&
         !nghttp2_session_want_write(session_);
}

namespace {
struct callback_guard {
  callback_guard(session_impl &sess) : sess(sess) { sess.enter_callback(); }
  ~callback_guard() { sess.leave_callback(); }

  session_impl &sess;
};
} // namespace

void session_impl::enter_callback() {
  assert(!inside_callback_);
  inside_callback_ = true;
}

void session_impl::leave_callback() {
  assert(inside_callback_);
  inside_callback_ = false;
}

void session_impl::do_read() {
  if (stopped_) {
    return;
  }

  deadline_.expires_from_now(read_timeout_);

  auto self = this->shared_from_this();

  read_socket([this, self](const boost::system::error_code &ec,
                           std::size_t bytes_transferred) {
    if (ec) {
      if (!should_stop()) {
        call_error_cb(ec);
      }
      stop();
      return;
    }

    {
      callback_guard cg(*this);

      auto rv =
          nghttp2_session_mem_recv(session_, rb_.data(), bytes_transferred);

      if (rv != static_cast<ssize_t>(bytes_transferred)) {
        call_error_cb(make_error_code(
            static_cast<nghttp2_error>(rv < 0 ? rv : NGHTTP2_ERR_PROTO)));
        stop();
        return;
      }
    }

    do_write();

    if (should_stop()) {
      stop();
      return;
    }

    do_read();
  });
}

void session_impl::do_write() {
  if (stopped_) {
    return;
  }

  if (writing_) {
    return;
  }

  if (data_pending_) {
    std::copy_n(data_pending_, data_pendinglen_, std::begin(wb_) + wblen_);

    wblen_ += data_pendinglen_;

    data_pending_ = nullptr;
    data_pendinglen_ = 0;
  }

  {
    callback_guard cg(*this);

    for (;;) {
      const uint8_t *data;
      auto n = nghttp2_session_mem_send(session_, &data);
      if (n < 0) {
        call_error_cb(make_error_code(static_cast<nghttp2_error>(n)));
        stop();
        return;
      }

      if (n == 0) {
        break;
      }

      if (wblen_ + n > wb_.size()) {
        data_pending_ = data;
        data_pendinglen_ = n;

        break;
      }

      std::copy_n(data, n, std::begin(wb_) + wblen_);

      wblen_ += n;
    }
  }

  if (wblen_ == 0) {
    if (should_stop()) {
      stop();
    }
    return;
  }

  writing_ = true;

  // Reset read deadline here, because normally client is sending
  // something, it does not expect timeout while doing it.
  deadline_.expires_from_now(read_timeout_);

  auto self = this->shared_from_this();

  write_socket(
      [this, self](const boost::system::error_code &ec, std::size_t n) {
        if (ec) {
          call_error_cb(ec);
          stop();
          return;
        }

        wblen_ = 0;
        writing_ = false;

        do_write();
      });
}

void session_impl::stop() {
  if (stopped_) {
    return;
  }

  shutdown_socket();
  deadline_.cancel();
  stopped_ = true;
}

void session_impl::connect_timeout(const boost::posix_time::time_duration &t) {
  connect_timeout_ = t;
}

void session_impl::read_timeout(const boost::posix_time::time_duration &t) {
  read_timeout_ = t;
}

} // namespace client
} // namespace asio_http2
} // nghttp2
