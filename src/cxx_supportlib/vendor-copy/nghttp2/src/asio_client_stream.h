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
#ifndef ASIO_CLIENT_STREAM_H
#define ASIO_CLIENT_STREAM_H

#include "nghttp2_config.h"

#include <nghttp2/asio_http2_client.h>

namespace nghttp2 {
namespace asio_http2 {
namespace client {

class request;
class response;
class session_impl;

class stream {
public:
  stream(session_impl *sess);

  stream(const stream &) = delete;
  stream &operator=(const stream &) = delete;

  void stream_id(int32_t stream_id);
  int32_t stream_id() const;

  class request &request();
  class response &response();

  session_impl *session() const;

  bool expect_final_response() const;

private:
  nghttp2::asio_http2::client::request request_;
  nghttp2::asio_http2::client::response response_;
  session_impl *sess_;
  uint32_t stream_id_;
};

} // namespace client
} // namespace asio_http2
} // namespace nghttp2

#endif // ASIO_CLIENT_STREAM_H
