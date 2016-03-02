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

#include "asio_client_session_tcp_impl.h"
#include "asio_client_session_tls_impl.h"
#include "asio_common.h"
#include "template.h"

namespace nghttp2 {
namespace asio_http2 {
namespace client {

using boost::asio::ip::tcp;

session::session(boost::asio::io_service &io_service, const std::string &host,
                 const std::string &service)
    : impl_(std::make_shared<session_tcp_impl>(io_service, host, service)) {
  impl_->start_resolve(host, service);
}

session::session(boost::asio::io_service &io_service,
                 boost::asio::ssl::context &tls_ctx, const std::string &host,
                 const std::string &service)
    : impl_(std::make_shared<session_tls_impl>(io_service, tls_ctx, host,
                                               service)) {
  impl_->start_resolve(host, service);
}

session::~session() {}

session::session(session &&other) noexcept : impl_(std::move(other.impl_)) {}

session &session::operator=(session &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  impl_ = std::move(other.impl_);
  return *this;
}

void session::on_connect(connect_cb cb) const {
  impl_->on_connect(std::move(cb));
}

void session::on_error(error_cb cb) const { impl_->on_error(std::move(cb)); }

void session::shutdown() const { impl_->shutdown(); }

boost::asio::io_service &session::io_service() const {
  return impl_->io_service();
}

const request *session::submit(boost::system::error_code &ec,
                               const std::string &method,
                               const std::string &uri, header_map h) const {
  return impl_->submit(ec, method, uri, generator_cb(), std::move(h));
}

const request *session::submit(boost::system::error_code &ec,
                               const std::string &method,
                               const std::string &uri, std::string data,
                               header_map h) const {
  return impl_->submit(ec, method, uri, string_generator(std::move(data)),
                       std::move(h));
}

const request *session::submit(boost::system::error_code &ec,
                               const std::string &method,
                               const std::string &uri, generator_cb cb,
                               header_map h) const {
  return impl_->submit(ec, method, uri, std::move(cb), std::move(h));
}

void session::connect_timeout(const boost::posix_time::time_duration &t) {
  impl_->connect_timeout(t);
}

void session::read_timeout(const boost::posix_time::time_duration &t) {
  impl_->read_timeout(t);
}

} // namespace client
} // namespace asio_http2
} // nghttp2
