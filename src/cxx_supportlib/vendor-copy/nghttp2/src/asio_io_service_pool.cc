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
// io_service_pool.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "asio_io_service_pool.h"

namespace nghttp2 {

namespace asio_http2 {

io_service_pool::io_service_pool(std::size_t pool_size) : next_io_service_(0) {
  if (pool_size == 0) {
    throw std::runtime_error("io_service_pool size is 0");
  }

  // Give all the io_services work to do so that their run() functions will not
  // exit until they are explicitly stopped.
  for (std::size_t i = 0; i < pool_size; ++i) {
    auto io_service = std::make_shared<boost::asio::io_service>();
    auto work = std::make_shared<boost::asio::io_service::work>(*io_service);
    io_services_.push_back(io_service);
    work_.push_back(work);
  }
}

void io_service_pool::run(bool asynchronous) {
  // Create a pool of threads to run all of the io_services.
  for (std::size_t i = 0; i < io_services_.size(); ++i) {
    futures_.push_back(std::async(std::launch::async,
                                  (size_t (boost::asio::io_service::*)(void)) &
                                      boost::asio::io_service::run,
                                  io_services_[i]));
  }

  if (!asynchronous) {
    join();
  }
}

void io_service_pool::join() {
  // Wait for all threads in the pool to exit.
  for (auto &fut : futures_) {
    fut.get();
  }
}

void io_service_pool::stop() {
  // Explicitly stop all io_services.
  for (auto &iosv : io_services_) {
    iosv->stop();
  }
}

boost::asio::io_service &io_service_pool::get_io_service() {
  // Use a round-robin scheme to choose the next io_service to use.
  auto &io_service = *io_services_[next_io_service_];
  ++next_io_service_;
  if (next_io_service_ == io_services_.size()) {
    next_io_service_ = 0;
  }
  return io_service;
}

const std::vector<std::shared_ptr<boost::asio::io_service>> &
io_service_pool::io_services() const {
  return io_services_;
}

} // namespace asio_http2

} // namespace nghttp2
