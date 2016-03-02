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
#include "asio_server_request_impl.h"

namespace nghttp2 {
namespace asio_http2 {
namespace server {

request_impl::request_impl() : strm_(nullptr), header_buffer_size_(0) {}

const header_map &request_impl::header() const { return header_; }

const std::string &request_impl::method() const { return method_; }

const uri_ref &request_impl::uri() const { return uri_; }

uri_ref &request_impl::uri() { return uri_; }

void request_impl::header(header_map h) { header_ = std::move(h); }

header_map &request_impl::header() { return header_; }

void request_impl::method(std::string arg) { method_ = std::move(arg); }

void request_impl::on_data(data_cb cb) { on_data_cb_ = std::move(cb); }

void request_impl::stream(class stream *s) { strm_ = s; }

void request_impl::call_on_data(const uint8_t *data, std::size_t len) {
  if (on_data_cb_) {
    on_data_cb_(data, len);
  }
}

const boost::asio::ip::tcp::endpoint &request_impl::remote_endpoint() const {
  return remote_ep_;
}

void request_impl::remote_endpoint(boost::asio::ip::tcp::endpoint ep) {
  remote_ep_ = std::move(ep);
}

size_t request_impl::header_buffer_size() const { return header_buffer_size_; }

void request_impl::update_header_buffer_size(size_t len) {
  header_buffer_size_ += len;
}

} // namespace server
} // namespace asio_http2
} // namespace nghttp2
