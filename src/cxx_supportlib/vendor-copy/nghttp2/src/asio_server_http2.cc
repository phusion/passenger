/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
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

#include "asio_server_http2_impl.h"
#include "asio_server.h"
#include "template.h"

namespace nghttp2 {

namespace asio_http2 {

namespace server {

http2::http2() : impl_(make_unique<http2_impl>()) {}

http2::~http2() {}

http2::http2(http2 &&other) noexcept : impl_(std::move(other.impl_)) {}

http2 &http2::operator=(http2 &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  impl_ = std::move(other.impl_);

  return *this;
}

boost::system::error_code http2::listen_and_serve(boost::system::error_code &ec,
                                                  const std::string &address,
                                                  const std::string &port,
                                                  bool asynchronous) {
  return impl_->listen_and_serve(ec, nullptr, address, port, asynchronous);
}

boost::system::error_code http2::listen_and_serve(
    boost::system::error_code &ec, boost::asio::ssl::context &tls_context,
    const std::string &address, const std::string &port, bool asynchronous) {
  return impl_->listen_and_serve(ec, &tls_context, address, port, asynchronous);
}

void http2::num_threads(size_t num_threads) { impl_->num_threads(num_threads); }

void http2::backlog(int backlog) { impl_->backlog(backlog); }

void http2::tls_handshake_timeout(const boost::posix_time::time_duration &t) {
  impl_->tls_handshake_timeout(t);
}

void http2::read_timeout(const boost::posix_time::time_duration &t) {
  impl_->read_timeout(t);
}

bool http2::handle(std::string pattern, request_cb cb) {
  return impl_->handle(std::move(pattern), std::move(cb));
}

void http2::stop() { impl_->stop(); }

void http2::join() { return impl_->join(); }

const std::vector<std::shared_ptr<boost::asio::io_service>> &
http2::io_services() const {
  return impl_->io_services();
}

} // namespace server

} // namespace asio_http2

} // namespace nghttp2
