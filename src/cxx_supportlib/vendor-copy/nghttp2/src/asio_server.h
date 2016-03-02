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
// We wrote this code based on the original code which has the
// following license:
//
// server.hpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_SERVER_H
#define ASIO_SERVER_H

#include "nghttp2_config.h"

#include <string>
#include <vector>
#include <memory>

#include <boost/noncopyable.hpp>

#include <nghttp2/asio_http2_server.h>

#include "asio_io_service_pool.h"

namespace nghttp2 {

namespace asio_http2 {

namespace server {

class serve_mux;

using boost::asio::ip::tcp;

using ssl_socket = boost::asio::ssl::stream<tcp::socket>;

class server : private boost::noncopyable {
public:
  explicit server(std::size_t io_service_pool_size,
                  const boost::posix_time::time_duration &tls_handshake_timeout,
                  const boost::posix_time::time_duration &read_timeout);

  boost::system::error_code
  listen_and_serve(boost::system::error_code &ec,
                   boost::asio::ssl::context *tls_context,
                   const std::string &address, const std::string &port,
                   int backlog, serve_mux &mux, bool asynchronous = false);
  void join();
  void stop();

  /// Get access to all io_service objects.
  const std::vector<std::shared_ptr<boost::asio::io_service>> &
  io_services() const;

private:
  /// Initiate an asynchronous accept operation.
  void start_accept(tcp::acceptor &acceptor, serve_mux &mux);
  /// Same as above but with tls_context
  void start_accept(boost::asio::ssl::context &tls_context,
                    tcp::acceptor &acceptor, serve_mux &mux);

  /// Resolves address and bind socket to the resolved addresses.
  boost::system::error_code bind_and_listen(boost::system::error_code &ec,
                                            const std::string &address,
                                            const std::string &port,
                                            int backlog);

  /// The pool of io_service objects used to perform asynchronous
  /// operations.
  io_service_pool io_service_pool_;

  /// Acceptor used to listen for incoming connections.
  std::vector<tcp::acceptor> acceptors_;

  std::unique_ptr<boost::asio::ssl::context> ssl_ctx_;

  boost::posix_time::time_duration tls_handshake_timeout_;
  boost::posix_time::time_duration read_timeout_;
};

} // namespace server

} // namespace asio_http2

} // namespace nghttp2

#endif // ASIO_SERVER_H
