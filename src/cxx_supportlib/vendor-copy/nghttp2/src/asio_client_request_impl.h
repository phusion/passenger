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
#ifndef ASIO_CLIENT_REQUEST_IMPL_H
#define ASIO_CLIENT_REQUEST_IMPL_H

#include "nghttp2_config.h"

#include <nghttp2/asio_http2_client.h>

namespace nghttp2 {
namespace asio_http2 {
namespace client {

class response;
class stream;

class request_impl {
public:
  request_impl();

  request_impl(const request_impl &) = delete;
  request_impl &operator=(const request_impl &) = delete;

  void write_trailer(header_map h);

  void cancel(uint32_t error_code);

  void on_response(response_cb cb);
  void call_on_response(response &res);

  void on_push(request_cb cb);
  void call_on_push(request &push_req);

  void on_close(close_cb cb);
  void call_on_close(uint32_t error_code);

  void on_read(generator_cb cb);
  generator_cb::result_type call_on_read(uint8_t *buf, std::size_t len,
                                         uint32_t *data_flags);

  void resume();

  void header(header_map h);
  header_map &header();
  const header_map &header() const;

  void stream(class stream *strm);

  void uri(uri_ref uri);
  const uri_ref &uri() const;
  uri_ref &uri();

  void method(std::string s);
  const std::string &method() const;

  size_t header_buffer_size() const;
  void update_header_buffer_size(size_t len);

private:
  header_map header_;
  response_cb response_cb_;
  request_cb push_request_cb_;
  close_cb close_cb_;
  generator_cb generator_cb_;
  class stream *strm_;
  uri_ref uri_;
  std::string method_;
  size_t header_buffer_size_;
};

} // namespace client
} // namespace asio_http2
} // namespace nghttp2

#endif // ASIO_CLIENT_REQUEST_IMPL_H
