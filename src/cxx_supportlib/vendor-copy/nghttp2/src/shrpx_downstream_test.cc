/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2013 Tatsuhiro Tsujikawa
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
#include "shrpx_downstream_test.h"

#include <iostream>

#include <CUnit/CUnit.h>

#include "shrpx_downstream.h"

namespace shrpx {

void test_downstream_field_store_index_headers(void) {
  FieldStore fs(0);
  fs.add_header("1", "0");
  fs.add_header("2", "1");
  fs.add_header("Charlie", "2");
  fs.add_header("Alpha", "3");
  fs.add_header("Delta", "4");
  fs.add_header("BravO", "5");
  fs.add_header(":method", "6");
  fs.add_header(":authority", "7");
  fs.index_headers();

  auto ans = Headers{{"1", "0"},
                     {"2", "1"},
                     {"charlie", "2"},
                     {"alpha", "3"},
                     {"delta", "4"},
                     {"bravo", "5"},
                     {":method", "6"},
                     {":authority", "7"}};
  CU_ASSERT(ans == fs.headers());
}

void test_downstream_field_store_header(void) {
  FieldStore fs(0);
  fs.add_header("alpha", "0");
  fs.add_header(":authority", "1");
  fs.add_header("content-length", "2");
  fs.index_headers();

  // By token
  CU_ASSERT(Header(":authority", "1") == *fs.header(http2::HD__AUTHORITY));
  CU_ASSERT(nullptr == fs.header(http2::HD__METHOD));

  // By name
  CU_ASSERT(Header("alpha", "0") == *fs.header("alpha"));
  CU_ASSERT(nullptr == fs.header("bravo"));
}

void test_downstream_crumble_request_cookie(void) {
  Downstream d(nullptr, nullptr, 0);
  auto &req = d.request();
  req.fs.add_header(":method", "get");
  req.fs.add_header(":path", "/");
  auto val = "alpha; bravo; ; ;; charlie;;";
  req.fs.add_header(
      reinterpret_cast<const uint8_t *>("cookie"), sizeof("cookie") - 1,
      reinterpret_cast<const uint8_t *>(val), strlen(val), true, -1);
  req.fs.add_header("cookie", ";delta");
  req.fs.add_header("cookie", "echo");

  std::vector<nghttp2_nv> nva;
  d.crumble_request_cookie(nva);

  auto num_cookies = d.count_crumble_request_cookie();

  CU_ASSERT(5 == nva.size());
  CU_ASSERT(5 == num_cookies);

  Headers cookies;
  std::transform(std::begin(nva), std::end(nva), std::back_inserter(cookies),
                 [](const nghttp2_nv &nv) {
                   return Header(std::string(nv.name, nv.name + nv.namelen),
                                 std::string(nv.value, nv.value + nv.valuelen),
                                 nv.flags & NGHTTP2_NV_FLAG_NO_INDEX);
                 });

  Headers ans = {{"cookie", "alpha"},
                 {"cookie", "bravo"},
                 {"cookie", "charlie"},
                 {"cookie", "delta"},
                 {"cookie", "echo"}};

  CU_ASSERT(ans == cookies);
  CU_ASSERT(cookies[0].no_index);
  CU_ASSERT(cookies[1].no_index);
  CU_ASSERT(cookies[2].no_index);
}

void test_downstream_assemble_request_cookie(void) {
  Downstream d(nullptr, nullptr, 0);
  auto &req = d.request();
  req.fs.add_header(":method", "get");
  req.fs.add_header(":path", "/");
  req.fs.add_header("cookie", "alpha");
  req.fs.add_header("cookie", "bravo;");
  req.fs.add_header("cookie", "charlie; ");
  req.fs.add_header("cookie", "delta;;");
  CU_ASSERT("alpha; bravo; charlie; delta" == d.assemble_request_cookie());
}

void test_downstream_rewrite_location_response_header(void) {
  Downstream d(nullptr, nullptr, 0);
  auto &req = d.request();
  auto &resp = d.response();
  d.set_request_downstream_host("localhost2");
  req.authority = "localhost:8443";
  resp.fs.add_header("location", "http://localhost2:3000/");
  resp.fs.index_headers();
  d.rewrite_location_response_header("https");
  auto location = resp.fs.header(http2::HD_LOCATION);
  CU_ASSERT("https://localhost:8443/" == (*location).value);
}

} // namespace shrpx
