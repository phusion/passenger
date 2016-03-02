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
#include "shrpx_downstream_connection_pool.h"
#include "shrpx_downstream_connection.h"

namespace shrpx {

DownstreamConnectionPool::DownstreamConnectionPool(size_t num_groups)
    : gpool_(num_groups) {}

DownstreamConnectionPool::~DownstreamConnectionPool() {
  for (auto &pool : gpool_) {
    for (auto dconn : pool) {
      delete dconn;
    }
  }
}

void DownstreamConnectionPool::add_downstream_connection(
    std::unique_ptr<DownstreamConnection> dconn) {
  auto group = dconn->get_group();
  assert(gpool_.size() > group);
  gpool_[group].insert(dconn.release());
}

std::unique_ptr<DownstreamConnection>
DownstreamConnectionPool::pop_downstream_connection(size_t group) {
  assert(gpool_.size() > group);
  auto &pool = gpool_[group];
  if (pool.empty()) {
    return nullptr;
  }

  auto dconn = std::unique_ptr<DownstreamConnection>(*std::begin(pool));
  pool.erase(std::begin(pool));
  return dconn;
}

void DownstreamConnectionPool::remove_downstream_connection(
    DownstreamConnection *dconn) {
  auto group = dconn->get_group();
  assert(gpool_.size() > group);
  gpool_[group].erase(dconn);
  delete dconn;
}

} // namespace shrpx
