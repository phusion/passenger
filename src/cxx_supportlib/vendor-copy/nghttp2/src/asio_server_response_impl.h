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
#ifndef ASIO_SERVER_RESPONSE_IMPL_H
#define ASIO_SERVER_RESPONSE_IMPL_H

#include "nghttp2_config.h"

#include <nghttp2/asio_http2_server.h>

namespace nghttp2 {
namespace asio_http2 {
namespace server {

class stream;

enum class response_state {
  INITIAL,
  // response_impl::write_head() was called
  HEADER_DONE,
  // response_impl::end() was called
  BODY_STARTED,
};

class response_impl {
public:
  response_impl();
  void write_head(unsigned int status_code, header_map h = header_map{});
  void end(std::string data = "");
  void end(generator_cb cb);
  void write_trailer(header_map h);
  void on_close(close_cb cb);
  void resume();

  void cancel(uint32_t error_code);

  response *push(boost::system::error_code &ec, std::string method,
                 std::string raw_path_query, header_map) const;

  boost::asio::io_service &io_service();

  void start_response();

  unsigned int status_code() const;
  const header_map &header() const;
  void pushed(bool f);
  void push_promise_sent();
  void stream(class stream *s);
  generator_cb::result_type call_read(uint8_t *data, std::size_t len,
                                      uint32_t *data_flags);
  void call_on_close(uint32_t error_code);

private:
  class stream *strm_;
  header_map header_;
  generator_cb generator_cb_;
  close_cb close_cb_;
  unsigned int status_code_;
  response_state state_;
  // true if this is pushed stream's response
  bool pushed_;
  // true if PUSH_PROMISE is sent if this is response of a pushed
  // stream
  bool push_promise_sent_;
};

} // namespace server
} // namespace asio_http2
} // namespace nghttp2

#endif // ASIO_SERVER_RESPONSE_IMPL_H
