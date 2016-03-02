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
#include "asio_client_request_impl.h"

#include "asio_client_stream.h"
#include "asio_client_session_impl.h"
#include "template.h"

namespace nghttp2 {
namespace asio_http2 {
namespace client {

request_impl::request_impl() : strm_(nullptr), header_buffer_size_(0) {}

void request_impl::write_trailer(header_map h) {
  auto sess = strm_->session();
  sess->write_trailer(*strm_, std::move(h));
}

void request_impl::cancel(uint32_t error_code) {
  auto sess = strm_->session();
  sess->cancel(*strm_, error_code);
}

void request_impl::on_response(response_cb cb) { response_cb_ = std::move(cb); }

void request_impl::call_on_response(response &res) {
  if (response_cb_) {
    response_cb_(res);
  }
}

void request_impl::on_push(request_cb cb) { push_request_cb_ = std::move(cb); }

void request_impl::call_on_push(request &push_req) {
  if (push_request_cb_) {
    push_request_cb_(push_req);
  }
};

void request_impl::on_close(close_cb cb) { close_cb_ = std::move(cb); }

void request_impl::call_on_close(uint32_t error_code) {
  if (close_cb_) {
    close_cb_(error_code);
  }
}

void request_impl::on_read(generator_cb cb) { generator_cb_ = std::move(cb); }

generator_cb::result_type request_impl::call_on_read(uint8_t *buf,
                                                     std::size_t len,
                                                     uint32_t *data_flags) {
  if (generator_cb_) {
    return generator_cb_(buf, len, data_flags);
  }

  *data_flags |= NGHTTP2_DATA_FLAG_EOF;

  return 0;
}

void request_impl::resume() {
  auto sess = strm_->session();
  sess->resume(*strm_);
}

void request_impl::header(header_map h) { header_ = std::move(h); }

header_map &request_impl::header() { return header_; }

const header_map &request_impl::header() const { return header_; }

void request_impl::stream(class stream *strm) { strm_ = strm; }

void request_impl::uri(uri_ref uri) { uri_ = std::move(uri); }

const uri_ref &request_impl::uri() const { return uri_; }

uri_ref &request_impl::uri() { return uri_; }

void request_impl::method(std::string s) { method_ = std::move(s); }

const std::string &request_impl::method() const { return method_; }

size_t request_impl::header_buffer_size() const { return header_buffer_size_; }

void request_impl::update_header_buffer_size(size_t len) {
  header_buffer_size_ += len;
}

} // namespace client
} // namespace asio_http2
} // namespace nghttp2
