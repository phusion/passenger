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

#include <nghttp2/asio_http2_client.h>

#include "asio_client_request_impl.h"

#include "template.h"

namespace nghttp2 {
namespace asio_http2 {
namespace client {

request::request() : impl_(make_unique<request_impl>()) {}

request::~request() {}

void request::write_trailer(header_map h) const {
  impl_->write_trailer(std::move(h));
}

void request::cancel(uint32_t error_code) const { impl_->cancel(error_code); }

void request::on_response(response_cb cb) const {
  impl_->on_response(std::move(cb));
}

void request::on_push(request_cb cb) const { impl_->on_push(std::move(cb)); }

void request::on_close(close_cb cb) const { impl_->on_close(std::move(cb)); }

const uri_ref &request::uri() const { return impl_->uri(); }

const std::string &request::method() const { return impl_->method(); }

const header_map &request::header() const { return impl_->header(); }

void request::resume() const { impl_->resume(); }

request_impl &request::impl() const { return *impl_; }

} // namespace client
} // namespace asio_http2
} // namespace nghttp2
