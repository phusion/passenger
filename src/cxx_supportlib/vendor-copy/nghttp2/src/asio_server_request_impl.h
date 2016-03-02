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
#ifndef ASIO_SERVER_REQUEST_IMPL_H
#define ASIO_SERVER_REQUEST_IMPL_H

#include "nghttp2_config.h"

#include <nghttp2/asio_http2_server.h>
#include <boost/asio/ip/tcp.hpp>

namespace nghttp2 {
namespace asio_http2 {
namespace server {

class stream;

class request_impl {
public:
  request_impl();

  void header(header_map h);
  const header_map &header() const;
  header_map &header();

  void method(std::string method);
  const std::string &method() const;

  const uri_ref &uri() const;
  uri_ref &uri();

  void on_data(data_cb cb);

  void stream(class stream *s);
  void call_on_data(const uint8_t *data, std::size_t len);

  const boost::asio::ip::tcp::endpoint &remote_endpoint() const;
  void remote_endpoint(boost::asio::ip::tcp::endpoint ep);

  size_t header_buffer_size() const;
  void update_header_buffer_size(size_t len);

private:
  class stream *strm_;
  header_map header_;
  std::string method_;
  uri_ref uri_;
  data_cb on_data_cb_;
  boost::asio::ip::tcp::endpoint remote_ep_;
  size_t header_buffer_size_;
};

} // namespace server
} // namespace asio_http2
} // namespace nghttp2

#endif // ASIO_SERVER_REQUEST_IMPL_H
