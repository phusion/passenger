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
#ifndef ASIO_COMMON_H
#define ASIO_COMMON_H

#include "nghttp2_config.h"

#include <string>

#include <nghttp2/asio_http2.h>

#include "util.h"

namespace nghttp2 {

namespace asio_http2 {

boost::system::error_code make_error_code(nghttp2_error ev);

boost::system::error_code make_error_code(nghttp2_asio_error ev);

generator_cb string_generator(std::string data);

// Returns generator_cb, which just returns NGHTTP2_ERR_DEFERRED
generator_cb deferred_generator();

template <typename InputIt>
void split_path(uri_ref &dst, InputIt first, InputIt last) {
  auto path_last = std::find(first, last, '?');
  InputIt query_first;
  if (path_last == last) {
    query_first = path_last = last;
  } else {
    query_first = path_last + 1;
  }
  dst.path = util::percent_decode(first, path_last);
  dst.raw_path.assign(first, path_last);
  dst.raw_query.assign(query_first, last);
}

using boost::asio::ip::tcp;

using ssl_socket = boost::asio::ssl::stream<tcp::socket>;

bool tls_h2_negotiated(ssl_socket &socket);

} // namespace asio_http2

} // namespace nghttp2

#endif // ASIO_COMMON_H
