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
#include "asio_server_serve_mux.h"

#include "asio_server_request_impl.h"
#include "asio_server_request_handler.h"
#include "util.h"
#include "http2.h"

namespace nghttp2 {

namespace asio_http2 {

namespace server {

bool serve_mux::handle(std::string pattern, request_cb cb) {
  if (pattern.empty() || !cb) {
    return false;
  }

  auto it = mux_.find(pattern);
  if (it != std::end(mux_) && (*it).second.user_defined) {
    return false;
  }

  // if pattern ends with '/' (e.g., /foo/), add implicit permanent
  // redirect for '/foo'.
  if (pattern.size() >= 2 && pattern.back() == '/') {
    auto redirect_pattern = pattern.substr(0, pattern.size() - 1);
    auto it = mux_.find(redirect_pattern);
    if (it == std::end(mux_) || !(*it).second.user_defined) {
      std::string path;
      if (pattern[0] == '/') {
        path = pattern;
      } else {
        // skip host part
        path = pattern.substr(pattern.find('/'));
      }
      if (it == std::end(mux_)) {
        mux_.emplace(std::move(redirect_pattern),
                     handler_entry{false,
                                   redirect_handler(301, std::move(path)),
                                   pattern});
      } else {
        (*it).second = handler_entry{
            false, redirect_handler(301, std::move(path)), pattern};
      }
    }
  }
  mux_.emplace(pattern, handler_entry{true, std::move(cb), pattern});

  return true;
}

request_cb serve_mux::handler(request_impl &req) const {
  auto &path = req.uri().path;
  if (req.method() != "CONNECT") {
    auto clean_path = ::nghttp2::http2::path_join(
        nullptr, 0, nullptr, 0, path.c_str(), path.size(), nullptr, 0);
    if (clean_path != path) {
      auto new_uri = util::percent_encode_path(clean_path);
      auto &uref = req.uri();
      if (!uref.raw_query.empty()) {
        new_uri += '?';
        new_uri += uref.raw_query;
      }

      return redirect_handler(301, std::move(new_uri));
    }
  }
  auto &host = req.uri().host;

  auto cb = match(host + path);
  if (cb) {
    return cb;
  }
  cb = match(path);
  if (cb) {
    return cb;
  }
  return status_handler(404);
}

namespace {
bool path_match(const std::string &pattern, const std::string &path) {
  if (pattern.back() != '/') {
    return pattern == path;
  }
  return util::starts_with(path, pattern);
}
} // namespace

request_cb serve_mux::match(const std::string &path) const {
  const handler_entry *ent = nullptr;
  size_t best = 0;
  for (auto &kv : mux_) {
    auto &pattern = kv.first;
    if (!path_match(pattern, path)) {
      continue;
    }
    if (!ent || best < pattern.size()) {
      best = pattern.size();
      ent = &kv.second;
    }
  }
  if (ent) {
    return ent->cb;
  }
  return request_cb();
}

} // namespace server

} // namespace asio_http2

} // namespace nghttp2
