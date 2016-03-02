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
#include "nghttp2_config.h"

#include <nghttp2/asio_http2_server.h>

#include "asio_server_response_impl.h"

#include "template.h"

namespace nghttp2 {
namespace asio_http2 {
namespace server {

response::response() : impl_(make_unique<response_impl>()) {}

response::~response() {}

void response::write_head(unsigned int status_code, header_map h) const {
  impl_->write_head(status_code, std::move(h));
}

void response::end(std::string data) const { impl_->end(std::move(data)); }

void response::end(generator_cb cb) const { impl_->end(std::move(cb)); }

void response::write_trailer(header_map h) const {
  impl_->write_trailer(std::move(h));
}

void response::on_close(close_cb cb) const { impl_->on_close(std::move(cb)); }

void response::cancel(uint32_t error_code) const { impl_->cancel(error_code); }

const response *response::push(boost::system::error_code &ec,
                               std::string method, std::string path,
                               header_map h) const {
  return impl_->push(ec, std::move(method), std::move(path), std::move(h));
}

void response::resume() const { impl_->resume(); }

unsigned int response::status_code() const { return impl_->status_code(); }

boost::asio::io_service &response::io_service() const {
  return impl_->io_service();
}

response_impl &response::impl() const { return *impl_; }

} // namespace server
} // namespace asio_http2
} // namespace nghttp2
