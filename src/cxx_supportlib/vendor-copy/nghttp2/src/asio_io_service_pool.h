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
// io_service_pool.hpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_IO_SERVICE_POOL_H
#define ASIO_IO_SERVICE_POOL_H

#include "nghttp2_config.h"

#include <vector>
#include <memory>
#include <future>

#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>

#include <nghttp2/asio_http2.h>

namespace nghttp2 {

namespace asio_http2 {

/// A pool of io_service objects.
class io_service_pool : private boost::noncopyable {
public:
  /// Construct the io_service pool.
  explicit io_service_pool(std::size_t pool_size);

  /// Run all io_service objects in the pool.
  void run(bool asynchronous = false);

  /// Stop all io_service objects in the pool.
  void stop();

  /// Join on all io_service objects in the pool.
  void join();

  /// Get an io_service to use.
  boost::asio::io_service &get_io_service();

  /// Get access to all io_service objects.
  const std::vector<std::shared_ptr<boost::asio::io_service>> &
  io_services() const;

private:
  /// The pool of io_services.
  std::vector<std::shared_ptr<boost::asio::io_service>> io_services_;

  /// The work that keeps the io_services running.
  std::vector<std::shared_ptr<boost::asio::io_service::work>> work_;

  /// The next io_service to use for a connection.
  std::size_t next_io_service_;

  /// Futures to all the io_service objects
  std::vector<std::future<std::size_t>> futures_;
};

} // namespace asio_http2

} // namespace nghttp2

#endif // ASIO_IO_SERVICE_POOL_H
