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
#ifndef ASIO_CLIENT_SESSION_TCP_IMPL_H
#define ASIO_CLIENT_SESSION_TCP_IMPL_H

#include "asio_client_session_impl.h"

#include <nghttp2/asio_http2_client.h>

namespace nghttp2 {
namespace asio_http2 {
namespace client {

using boost::asio::ip::tcp;

class session_tcp_impl : public session_impl {
public:
  session_tcp_impl(boost::asio::io_service &io_service, const std::string &host,
                   const std::string &service);
  virtual ~session_tcp_impl();

  virtual void start_connect(tcp::resolver::iterator endpoint_it);
  virtual tcp::socket &socket();
  virtual void read_socket(std::function<
      void(const boost::system::error_code &ec, std::size_t n)> h);
  virtual void write_socket(std::function<
      void(const boost::system::error_code &ec, std::size_t n)> h);
  virtual void shutdown_socket();

private:
  tcp::socket socket_;
};

} // namespace client
} // namespace asio_http2
} // namespace nghttp2

#endif // ASIO_CLIENT_SESSION_TCP_IMPL_H
