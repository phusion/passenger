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
#ifndef ASIO_SERVER_STREAM_H
#define ASIO_SERVER_STREAM_H

#include "nghttp2_config.h"

#include <nghttp2/asio_http2_server.h>

namespace nghttp2 {
namespace asio_http2 {
namespace server {

class http2_handler;

class stream {
public:
  stream(http2_handler *h, int32_t stream_id);

  int32_t get_stream_id() const;
  class request &request();
  class response &response();

  http2_handler *handler() const;

private:
  http2_handler *handler_;
  class request request_;
  class response response_;
  int32_t stream_id_;
};

} // namespace server
} // namespace asio_http2
} // namespace nghttp2

#endif // ASIO_SERVER_STREAM_H
