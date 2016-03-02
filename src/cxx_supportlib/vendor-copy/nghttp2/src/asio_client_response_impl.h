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
#ifndef ASIO_CLIENT_RESPONSE_IMPL_H
#define ASIO_CLIENT_RESPONSE_IMPL_H

#include "nghttp2_config.h"

#include <nghttp2/asio_http2_client.h>

namespace nghttp2 {
namespace asio_http2 {
namespace client {

class response_impl {
public:
  response_impl();

  response_impl(const response_impl &) = delete;
  response_impl &operator=(const response_impl &) = delete;

  void on_data(data_cb cb);

  void call_on_data(const uint8_t *data, std::size_t len);

  void status_code(int sc);
  int status_code() const;

  void content_length(int64_t n);
  int64_t content_length() const;

  header_map &header();
  const header_map &header() const;

  size_t header_buffer_size() const;
  void update_header_buffer_size(size_t len);

private:
  data_cb data_cb_;

  header_map header_;

  int64_t content_length_;
  size_t header_buffer_size_;
  int status_code_;
};

} // namespace client
} // namespace asio_http2
} // namespace nghttp2

#endif // ASIO_CLIENT_RESPONSE_IMPL_H
