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
#include "asio_client_stream.h"

#include "asio_client_request_impl.h"
#include "asio_client_response_impl.h"
#include "asio_client_session_impl.h"

namespace nghttp2 {
namespace asio_http2 {
namespace client {

stream::stream(session_impl *sess) : sess_(sess), stream_id_(0) {
  request_.impl().stream(this);
}

void stream::stream_id(int32_t stream_id) { stream_id_ = stream_id; }

int32_t stream::stream_id() const { return stream_id_; }

class request &stream::request() {
  return request_;
}

class response &stream::response() {
  return response_;
}

session_impl *stream::session() const { return sess_; }

bool stream::expect_final_response() const {
  return response_.status_code() / 100 == 1;
}

} // namespace client
} // namespace asio_http2
} // namespace nghttp2
