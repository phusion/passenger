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
#include "asio_client_session_tcp_impl.h"

namespace nghttp2 {
namespace asio_http2 {
namespace client {

session_tcp_impl::session_tcp_impl(boost::asio::io_service &io_service,
                                   const std::string &host,
                                   const std::string &service)
    : session_impl(io_service), socket_(io_service) {}

session_tcp_impl::~session_tcp_impl() {}

void session_tcp_impl::start_connect(tcp::resolver::iterator endpoint_it) {
  boost::asio::async_connect(socket_, endpoint_it,
                             [this](const boost::system::error_code &ec,
                                    tcp::resolver::iterator endpoint_it) {
                               if (ec) {
                                 not_connected(ec);
                                 return;
                               }

                               connected(endpoint_it);
                             });
}

tcp::socket &session_tcp_impl::socket() { return socket_; }

void session_tcp_impl::read_socket(
    std::function<void(const boost::system::error_code &ec, std::size_t n)> h) {
  socket_.async_read_some(boost::asio::buffer(rb_), h);
}

void session_tcp_impl::write_socket(
    std::function<void(const boost::system::error_code &ec, std::size_t n)> h) {
  boost::asio::async_write(socket_, boost::asio::buffer(wb_, wblen_), h);
}

void session_tcp_impl::shutdown_socket() {
  boost::system::error_code ignored_ec;
  socket_.close(ignored_ec);
}

} // namespace client
} // namespace asio_http2
} // namespace nghttp2
