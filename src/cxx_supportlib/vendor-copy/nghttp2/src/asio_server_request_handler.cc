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
#include "asio_server_request_handler.h"

#include "util.h"
#include "http2.h"

namespace nghttp2 {
namespace asio_http2 {
namespace server {

namespace {
std::string create_html(int status_code) {
  std::string res;
  res.reserve(512);
  auto status = ::nghttp2::http2::get_status_string(status_code);
  res += R"(<!DOCTYPE html><html lang="en"><title>)";
  res += status;
  res += "</title><body><h1>";
  res += status;
  res += "</h1></body></html>";
  return res;
}
} // namespace

request_cb redirect_handler(int status_code, std::string uri) {
  return [status_code, uri](const request &req, const response &res) {
    header_map h;
    h.emplace("location", header_value{std::move(uri)});
    std::string html;
    if (req.method() == "GET") {
      html = create_html(status_code);
    }
    h.emplace("content-length", header_value{util::utos(html.size())});

    res.write_head(status_code, std::move(h));
    res.end(std::move(html));
  };
}

request_cb status_handler(int status_code) {
  return [status_code](const request &req, const response &res) {
    if (!::nghttp2::http2::expect_response_body(status_code)) {
      res.write_head(status_code);
      res.end();
      return;
    }
    // we supply content-length for HEAD request, but body will not be
    // sent.
    auto html = create_html(status_code);
    header_map h;
    h.emplace("content-length", header_value{util::utos(html.size())});
    h.emplace("content-type", header_value{"text/html; charset=utf-8"});

    res.write_head(status_code, std::move(h));
    res.end(std::move(html));
  };
}

} // namespace server
} // namespace asio_http2
} // namespace nghttp2
