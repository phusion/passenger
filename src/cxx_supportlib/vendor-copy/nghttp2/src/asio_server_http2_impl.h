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
#ifndef ASIO_SERVER_HTTP2_IMPL_H
#define ASIO_SERVER_HTTP2_IMPL_H

#include "nghttp2_config.h"

#include <nghttp2/asio_http2_server.h>

#include "asio_server_serve_mux.h"

namespace nghttp2 {

namespace asio_http2 {

namespace server {

class server;

class http2_impl {
public:
  http2_impl();
  boost::system::error_code listen_and_serve(
      boost::system::error_code &ec, boost::asio::ssl::context *tls_context,
      const std::string &address, const std::string &port, bool asynchronous);
  void num_threads(size_t num_threads);
  void backlog(int backlog);
  void tls_handshake_timeout(const boost::posix_time::time_duration &t);
  void read_timeout(const boost::posix_time::time_duration &t);
  bool handle(std::string pattern, request_cb cb);
  void stop();
  void join();
  const std::vector<std::shared_ptr<boost::asio::io_service>> &
  io_services() const;

private:
  std::unique_ptr<server> server_;
  std::size_t num_threads_;
  int backlog_;
  serve_mux mux_;
  boost::posix_time::time_duration tls_handshake_timeout_;
  boost::posix_time::time_duration read_timeout_;
};

} // namespace server

} // namespace asio_http2

} // namespace nghttp2

#endif // ASIO_SERVER_HTTP2_IMPL_H
