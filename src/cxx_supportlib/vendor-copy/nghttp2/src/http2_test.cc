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
#include "http2_test.h"

#include <cassert>
#include <cstring>
#include <iostream>

#include <CUnit/CUnit.h>

#include "http-parser/http_parser.h"

#include "http2.h"
#include "util.h"

using namespace nghttp2;

#define MAKE_NV(K, V)                                                          \
  {                                                                            \
    (uint8_t *) K, (uint8_t *)V, sizeof(K) - 1, sizeof(V) - 1,                 \
        NGHTTP2_NV_FLAG_NONE                                                   \
  }

namespace shrpx {

namespace {
void check_nv(const Header &a, const nghttp2_nv *b) {
  CU_ASSERT(a.name.size() == b->namelen);
  CU_ASSERT(a.value.size() == b->valuelen);
  CU_ASSERT(memcmp(a.name.c_str(), b->name, b->namelen) == 0);
  CU_ASSERT(memcmp(a.value.c_str(), b->value, b->valuelen) == 0);
}
} // namespace

void test_http2_add_header(void) {
  auto nva = Headers();

  http2::add_header(nva, (const uint8_t *)"alpha", 5, (const uint8_t *)"123", 3,
                    false, -1);
  CU_ASSERT(Headers::value_type("alpha", "123") == nva[0]);
  CU_ASSERT(!nva[0].no_index);

  nva.clear();

  http2::add_header(nva, (const uint8_t *)"alpha", 5, (const uint8_t *)"", 0,
                    true, -1);
  CU_ASSERT(Headers::value_type("alpha", "") == nva[0]);
  CU_ASSERT(nva[0].no_index);

  nva.clear();

  http2::add_header(nva, (const uint8_t *)"a", 1, (const uint8_t *)" b", 2,
                    false, -1);
  CU_ASSERT(Headers::value_type("a", "b") == nva[0]);

  nva.clear();

  http2::add_header(nva, (const uint8_t *)"a", 1, (const uint8_t *)"b ", 2,
                    false, -1);
  CU_ASSERT(Headers::value_type("a", "b") == nva[0]);

  nva.clear();

  http2::add_header(nva, (const uint8_t *)"a", 1, (const uint8_t *)"  b  ", 5,
                    false, -1);
  CU_ASSERT(Headers::value_type("a", "b") == nva[0]);

  nva.clear();

  http2::add_header(nva, (const uint8_t *)"a", 1, (const uint8_t *)"  bravo  ",
                    9, false, -1);
  CU_ASSERT(Headers::value_type("a", "bravo") == nva[0]);

  nva.clear();

  http2::add_header(nva, (const uint8_t *)"a", 1, (const uint8_t *)"    ", 4,
                    false, -1);
  CU_ASSERT(Headers::value_type("a", "") == nva[0]);

  nva.clear();

  http2::add_header(nva, (const uint8_t *)"te", 2, (const uint8_t *)"trailers",
                    8, false, http2::HD_TE);
  CU_ASSERT(http2::HD_TE == nva[0].token);
}

void test_http2_get_header(void) {
  auto nva = Headers{{"alpha", "1"},
                     {"bravo", "2"},
                     {"bravo", "3"},
                     {"charlie", "4"},
                     {"delta", "5"},
                     {"echo", "6"},
                     {"content-length", "7"}};
  const Headers::value_type *rv;
  rv = http2::get_header(nva, "delta");
  CU_ASSERT(rv != nullptr);
  CU_ASSERT("delta" == rv->name);

  rv = http2::get_header(nva, "bravo");
  CU_ASSERT(rv != nullptr);
  CU_ASSERT("bravo" == rv->name);

  rv = http2::get_header(nva, "foxtrot");
  CU_ASSERT(rv == nullptr);

  http2::HeaderIndex hdidx;
  http2::init_hdidx(hdidx);
  hdidx[http2::HD_CONTENT_LENGTH] = 6;
  rv = http2::get_header(hdidx, http2::HD_CONTENT_LENGTH, nva);
  CU_ASSERT("content-length" == rv->name);
}

namespace {
auto headers =
    Headers{{"alpha", "0", true},
            {"bravo", "1"},
            {"connection", "2", false, http2::HD_CONNECTION},
            {"connection", "3", false, http2::HD_CONNECTION},
            {"delta", "4"},
            {"expect", "5"},
            {"foxtrot", "6"},
            {"tango", "7"},
            {"te", "8", false, http2::HD_TE},
            {"te", "9", false, http2::HD_TE},
            {"x-forwarded-proto", "10", false, http2::HD_X_FORWARDED_FOR},
            {"x-forwarded-proto", "11", false, http2::HD_X_FORWARDED_FOR},
            {"zulu", "12"}};
} // namespace

void test_http2_copy_headers_to_nva(void) {
  auto ans = std::vector<int>{0, 1, 4, 5, 6, 7, 12};
  std::vector<nghttp2_nv> nva;

  http2::copy_headers_to_nva_nocopy(nva, headers);
  CU_ASSERT(7 == nva.size());
  for (size_t i = 0; i < ans.size(); ++i) {
    check_nv(headers[ans[i]], &nva[i]);

    if (ans[i] == 0) {
      CU_ASSERT((NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE |
                 NGHTTP2_NV_FLAG_NO_INDEX) == nva[i].flags);
    } else {
      CU_ASSERT((NGHTTP2_NV_FLAG_NO_COPY_NAME |
                 NGHTTP2_NV_FLAG_NO_COPY_VALUE) == nva[i].flags);
    }
  }

  nva.clear();
  http2::copy_headers_to_nva(nva, headers);
  CU_ASSERT(7 == nva.size());
  for (size_t i = 0; i < ans.size(); ++i) {
    check_nv(headers[ans[i]], &nva[i]);

    if (ans[i] == 0) {
      CU_ASSERT(nva[i].flags & NGHTTP2_NV_FLAG_NO_INDEX);
    } else {
      CU_ASSERT(NGHTTP2_NV_FLAG_NONE == nva[i].flags);
    }
  }
}

void test_http2_build_http1_headers_from_headers(void) {
  MemchunkPool pool;
  DefaultMemchunks buf(&pool);
  http2::build_http1_headers_from_headers(&buf, headers);
  auto hdrs = std::string(buf.head->pos, buf.head->last);
  CU_ASSERT("Alpha: 0\r\n"
            "Bravo: 1\r\n"
            "Delta: 4\r\n"
            "Expect: 5\r\n"
            "Foxtrot: 6\r\n"
            "Tango: 7\r\n"
            "Te: 8\r\n"
            "Te: 9\r\n"
            "Zulu: 12\r\n" == hdrs);
}

void test_http2_lws(void) {
  CU_ASSERT(!http2::lws("alpha"));
  CU_ASSERT(http2::lws(" "));
  CU_ASSERT(http2::lws(""));
}

namespace {
void check_rewrite_location_uri(const std::string &want, const std::string &uri,
                                const std::string &match_host,
                                const std::string &req_authority,
                                const std::string &upstream_scheme) {
  http_parser_url u{};
  CU_ASSERT(0 == http_parser_parse_url(uri.c_str(), uri.size(), 0, &u));
  auto got = http2::rewrite_location_uri(uri, u, match_host, req_authority,
                                         upstream_scheme);
  CU_ASSERT(want == got);
}
} // namespace

void test_http2_rewrite_location_uri(void) {
  check_rewrite_location_uri("https://localhost:3000/alpha?bravo#charlie",
                             "http://localhost:3001/alpha?bravo#charlie",
                             "localhost:3001", "localhost:3000", "https");
  check_rewrite_location_uri("https://localhost/", "http://localhost:3001/",
                             "localhost", "localhost", "https");
  check_rewrite_location_uri("http://localhost/", "http://localhost:3001/",
                             "localhost", "localhost", "http");
  check_rewrite_location_uri("http://localhost:443/", "http://localhost:3001/",
                             "localhost", "localhost:443", "http");
  check_rewrite_location_uri("https://localhost:80/", "http://localhost:3001/",
                             "localhost", "localhost:80", "https");
  check_rewrite_location_uri("", "http://localhost:3001/", "127.0.0.1",
                             "127.0.0.1", "https");
  check_rewrite_location_uri("https://localhost:3000/",
                             "http://localhost:3001/", "localhost",
                             "localhost:3000", "https");
  check_rewrite_location_uri("https://localhost:3000/", "http://localhost/",
                             "localhost", "localhost:3000", "https");

  // match_host != req_authority
  check_rewrite_location_uri("https://example.org", "http://127.0.0.1:8080",
                             "127.0.0.1", "example.org", "https");
  check_rewrite_location_uri("", "http://example.org", "127.0.0.1",
                             "example.org", "https");
}

void test_http2_parse_http_status_code(void) {
  CU_ASSERT(200 == http2::parse_http_status_code("200"));
  CU_ASSERT(102 == http2::parse_http_status_code("102"));
  CU_ASSERT(-1 == http2::parse_http_status_code("099"));
  CU_ASSERT(-1 == http2::parse_http_status_code("99"));
  CU_ASSERT(-1 == http2::parse_http_status_code("-1"));
  CU_ASSERT(-1 == http2::parse_http_status_code("20a"));
  CU_ASSERT(-1 == http2::parse_http_status_code(""));
}

void test_http2_index_header(void) {
  http2::HeaderIndex hdidx;
  http2::init_hdidx(hdidx);

  http2::index_header(hdidx, http2::HD__AUTHORITY, 0);
  http2::index_header(hdidx, -1, 1);

  CU_ASSERT(0 == hdidx[http2::HD__AUTHORITY]);
}

void test_http2_lookup_token(void) {
  CU_ASSERT(http2::HD__AUTHORITY == http2::lookup_token(":authority"));
  CU_ASSERT(-1 == http2::lookup_token(":authorit"));
  CU_ASSERT(-1 == http2::lookup_token(":Authority"));
  CU_ASSERT(http2::HD_EXPECT == http2::lookup_token("expect"));
}

void test_http2_check_http2_pseudo_header(void) {
  http2::HeaderIndex hdidx;
  http2::init_hdidx(hdidx);

  CU_ASSERT(http2::check_http2_request_pseudo_header(hdidx, http2::HD__METHOD));
  hdidx[http2::HD__PATH] = 0;
  CU_ASSERT(http2::check_http2_request_pseudo_header(hdidx, http2::HD__METHOD));
  hdidx[http2::HD__METHOD] = 1;
  CU_ASSERT(
      !http2::check_http2_request_pseudo_header(hdidx, http2::HD__METHOD));
  CU_ASSERT(!http2::check_http2_request_pseudo_header(hdidx, http2::HD_VIA));

  http2::init_hdidx(hdidx);

  CU_ASSERT(
      http2::check_http2_response_pseudo_header(hdidx, http2::HD__STATUS));
  hdidx[http2::HD__STATUS] = 0;
  CU_ASSERT(
      !http2::check_http2_response_pseudo_header(hdidx, http2::HD__STATUS));
  CU_ASSERT(!http2::check_http2_response_pseudo_header(hdidx, http2::HD_VIA));
}

void test_http2_http2_header_allowed(void) {
  CU_ASSERT(http2::http2_header_allowed(http2::HD__PATH));
  CU_ASSERT(http2::http2_header_allowed(http2::HD_CONTENT_LENGTH));
  CU_ASSERT(!http2::http2_header_allowed(http2::HD_CONNECTION));
}

void test_http2_mandatory_request_headers_presence(void) {
  http2::HeaderIndex hdidx;
  http2::init_hdidx(hdidx);

  CU_ASSERT(!http2::http2_mandatory_request_headers_presence(hdidx));
  hdidx[http2::HD__AUTHORITY] = 0;
  CU_ASSERT(!http2::http2_mandatory_request_headers_presence(hdidx));
  hdidx[http2::HD__METHOD] = 1;
  CU_ASSERT(!http2::http2_mandatory_request_headers_presence(hdidx));
  hdidx[http2::HD__PATH] = 2;
  CU_ASSERT(!http2::http2_mandatory_request_headers_presence(hdidx));
  hdidx[http2::HD__SCHEME] = 3;
  CU_ASSERT(http2::http2_mandatory_request_headers_presence(hdidx));

  hdidx[http2::HD__AUTHORITY] = -1;
  hdidx[http2::HD_HOST] = 0;
  CU_ASSERT(http2::http2_mandatory_request_headers_presence(hdidx));
}

void test_http2_parse_link_header(void) {
  {
    // only URI appears; we don't extract URI unless it bears rel=preload
    const char s[] = "<url>";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // URI url should be extracted
    const char s[] = "<url>; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // With extra link-param.  URI url should be extracted
    const char s[] = "<url>; rel=preload; as=file";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // With extra link-param.  URI url should be extracted
    const char s[] = "<url>; as=file; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // With extra link-param and quote-string.  URI url should be
    // extracted
    const char s[] = R"(<url>; rel=preload; title="foo,bar")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // With extra link-param and quote-string.  URI url should be
    // extracted
    const char s[] = R"(<url>; title="foo,bar"; rel=preload)";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // ',' after quote-string
    const char s[] = R"(<url>; title="foo,bar", <url>; rel=preload)";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[25], &s[28]) == res[0].uri);
  }
  {
    // Only first URI should be extracted.
    const char s[] = "<url>; rel=preload, <url>";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // Both have rel=preload, so both urls should be extracted
    const char s[] = "<url>; rel=preload, <url>; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(2 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
    CU_ASSERT(std::make_pair(&s[21], &s[24]) == res[1].uri);
  }
  {
    // Second URI uri should be extracted.
    const char s[] = "<url>, <url>;rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[8], &s[11]) == res[0].uri);
  }
  {
    // Error if input ends with ';'
    const char s[] = "<url>;rel=preload;";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Error if link header ends with ';'
    const char s[] = "<url>;rel=preload;, <url>";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // OK if input ends with ','
    const char s[] = "<url>;rel=preload,";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // Multiple repeated ','s between fields is OK
    const char s[] = "<url>,,,<url>;rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[9], &s[12]) == res[0].uri);
  }
  {
    // Error if url is not enclosed by <>
    const char s[] = "url>;rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Error if url is not enclosed by <>
    const char s[] = "<url;rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Empty parameter value is not allowed
    const char s[] = "<url>;rel=preload; as=";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Empty parameter value is not allowed
    const char s[] = "<url>;as=;rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Empty parameter value is not allowed
    const char s[] = "<url>;as=, <url>;rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Empty parameter name is not allowed
    const char s[] = "<url>; =file; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Without whitespaces
    const char s[] = "<url>;as=file;rel=preload,<url>;rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(2 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
    CU_ASSERT(std::make_pair(&s[27], &s[30]) == res[1].uri);
  }
  {
    // link-extension may have no value
    const char s[] = "<url>; as; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // ext-name-star
    const char s[] = "<url>; foo*=bar; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // '*' is not allowed expect for trailing one
    const char s[] = "<url>; *=bar; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // '*' is not allowed expect for trailing one
    const char s[] = "<url>; foo*bar=buzz; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // ext-name-star must be followed by '='
    const char s[] = "<url>; foo*; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // '>' is not followed by ';'
    const char s[] = "<url> rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // Starting with whitespace is no problem.
    const char s[] = "  <url>; rel=preload";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[3], &s[6]) == res[0].uri);
  }
  {
    // preload is a prefix of bogus rel parameter value
    const char s[] = "<url>; rel=preloadx";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // preload in relation-types list
    const char s[] = R"(<url>; rel="preload")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // preload in relation-types list followed by another parameter
    const char s[] = R"(<url>; rel="preload foo")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // preload in relation-types list following another parameter
    const char s[] = R"(<url>; rel="foo preload")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // preload in relation-types list between other parameters
    const char s[] = R"(<url>; rel="foo preload bar")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // preload in relation-types list between other parameters
    const char s[] = R"(<url>; rel="foo   preload   bar")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // no preload in relation-types list
    const char s[] = R"(<url>; rel="foo")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // no preload in relation-types list, multiple unrelated elements.
    const char s[] = R"(<url>; rel="foo bar")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // preload in relation-types list, followed by another link-value.
    const char s[] = R"(<url>; rel="preload", <url>)";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // preload in relation-types list, following another link-value.
    const char s[] = R"(<url>, <url>; rel="preload")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[8], &s[11]) == res[0].uri);
  }
  {
    // preload in relation-types list, followed by another link-param.
    const char s[] = R"(<url>; rel="preload"; as="font")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // preload in relation-types list, followed by character other
    // than ';' or ','
    const char s[] = R"(<url>; rel="preload".)";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // preload in relation-types list, followed by ';' but it
    // terminates input
    const char s[] = R"(<url>; rel="preload";)";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // preload in relation-types list, followed by ',' but it
    // terminates input
    const char s[] = R"(<url>; rel="preload",)";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // preload in relation-types list but there is preceding white
    // space.
    const char s[] = R"(<url>; rel=" preload")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // preload in relation-types list but there is trailing white
    // space.
    const char s[] = R"(<url>; rel="preload ")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // backslash escaped characters in quoted-string
    const char s[] = R"(<url>; rel=preload; title="foo\"baz\"bar")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // anchor="" is acceptable
    const char s[] = R"(<url>; rel=preload; anchor="")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // With anchor="#foo", url should be ignored
    const char s[] = R"(<url>; rel=preload; anchor="#foo")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // With anchor=f, url should be ignored
    const char s[] = "<url>; rel=preload; anchor=f";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // First url is ignored With anchor="#foo", but url should be
    // accepted.
    const char s[] = R"(<url>; rel=preload; anchor="#foo", <url>; rel=preload)";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[36], &s[39]) == res[0].uri);
  }
  {
    // With loadpolicy="next", url should be ignored
    const char s[] = R"(<url>; rel=preload; loadpolicy="next")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(0 == res.size());
  }
  {
    // url should be picked up if empty loadpolicy is specified
    const char s[] = R"(<url>; rel=preload; loadpolicy="")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(1 == res.size());
    CU_ASSERT(std::make_pair(&s[1], &s[4]) == res[0].uri);
  }
  {
    // case-insensitive match
    const char s[] = R"(<url>; rel=preload; ANCHOR="#foo", <url>; )"
                     R"(REL=PRELOAD, <url>; REL="foo PRELOAD bar")";
    auto res = http2::parse_link_header(s, sizeof(s) - 1);
    CU_ASSERT(2 == res.size());
    CU_ASSERT(std::make_pair(&s[36], &s[39]) == res[0].uri);
    CU_ASSERT(std::make_pair(&s[42 + 14], &s[42 + 17]) == res[1].uri);
  }
}

void test_http2_path_join(void) {
  {
    const char base[] = "/";
    const char rel[] = "/";
    CU_ASSERT("/" == http2::path_join(base, sizeof(base) - 1, nullptr, 0, rel,
                                      sizeof(rel) - 1, nullptr, 0));
  }
  {
    const char base[] = "/";
    const char rel[] = "/alpha";
    CU_ASSERT("/alpha" == http2::path_join(base, sizeof(base) - 1, nullptr, 0,
                                           rel, sizeof(rel) - 1, nullptr, 0));
  }
  {
    // rel ends with trailing '/'
    const char base[] = "/";
    const char rel[] = "/alpha/";
    CU_ASSERT("/alpha/" == http2::path_join(base, sizeof(base) - 1, nullptr, 0,
                                            rel, sizeof(rel) - 1, nullptr, 0));
  }
  {
    // rel contains multiple components
    const char base[] = "/";
    const char rel[] = "/alpha/bravo";
    CU_ASSERT("/alpha/bravo" == http2::path_join(base, sizeof(base) - 1,
                                                 nullptr, 0, rel,
                                                 sizeof(rel) - 1, nullptr, 0));
  }
  {
    // rel is relative
    const char base[] = "/";
    const char rel[] = "alpha/bravo";
    CU_ASSERT("/alpha/bravo" == http2::path_join(base, sizeof(base) - 1,
                                                 nullptr, 0, rel,
                                                 sizeof(rel) - 1, nullptr, 0));
  }
  {
    // rel is relative and base ends without /, which means it refers
    // to file.
    const char base[] = "/alpha";
    const char rel[] = "bravo/charlie";
    CU_ASSERT("/bravo/charlie" ==
              http2::path_join(base, sizeof(base) - 1, nullptr, 0, rel,
                               sizeof(rel) - 1, nullptr, 0));
  }
  {
    // rel contains repeated '/'s
    const char base[] = "/";
    const char rel[] = "/alpha/////bravo/////";
    CU_ASSERT("/alpha/bravo/" == http2::path_join(base, sizeof(base) - 1,
                                                  nullptr, 0, rel,
                                                  sizeof(rel) - 1, nullptr, 0));
  }
  {
    // base ends with '/', so '..' eats 'bravo'
    const char base[] = "/alpha/bravo/";
    const char rel[] = "../charlie/delta";
    CU_ASSERT("/alpha/charlie/delta" ==
              http2::path_join(base, sizeof(base) - 1, nullptr, 0, rel,
                               sizeof(rel) - 1, nullptr, 0));
  }
  {
    // base does not end with '/', so '..' eats 'alpha/bravo'
    const char base[] = "/alpha/bravo";
    const char rel[] = "../charlie";
    CU_ASSERT("/charlie" == http2::path_join(base, sizeof(base) - 1, nullptr, 0,
                                             rel, sizeof(rel) - 1, nullptr, 0));
  }
  {
    // 'charlie' is eaten by following '..'
    const char base[] = "/alpha/bravo/";
    const char rel[] = "../charlie/../delta";
    CU_ASSERT("/alpha/delta" == http2::path_join(base, sizeof(base) - 1,
                                                 nullptr, 0, rel,
                                                 sizeof(rel) - 1, nullptr, 0));
  }
  {
    // excessive '..' results in '/'
    const char base[] = "/alpha/bravo/";
    const char rel[] = "../../../";
    CU_ASSERT("/" == http2::path_join(base, sizeof(base) - 1, nullptr, 0, rel,
                                      sizeof(rel) - 1, nullptr, 0));
  }
  {
    // excessive '..'  and  path component
    const char base[] = "/alpha/bravo/";
    const char rel[] = "../../../charlie";
    CU_ASSERT("/charlie" == http2::path_join(base, sizeof(base) - 1, nullptr, 0,
                                             rel, sizeof(rel) - 1, nullptr, 0));
  }
  {
    // rel ends with '..'
    const char base[] = "/alpha/bravo/";
    const char rel[] = "charlie/..";
    CU_ASSERT("/alpha/bravo/" == http2::path_join(base, sizeof(base) - 1,
                                                  nullptr, 0, rel,
                                                  sizeof(rel) - 1, nullptr, 0));
  }
  {
    // base empty and rel contains '..'
    const char base[] = "";
    const char rel[] = "charlie/..";
    CU_ASSERT("/" == http2::path_join(base, sizeof(base) - 1, nullptr, 0, rel,
                                      sizeof(rel) - 1, nullptr, 0));
  }
  {
    // '.' is ignored
    const char base[] = "/";
    const char rel[] = "charlie/././././delta";
    CU_ASSERT("/charlie/delta" ==
              http2::path_join(base, sizeof(base) - 1, nullptr, 0, rel,
                               sizeof(rel) - 1, nullptr, 0));
  }
  {
    // trailing '.' is ignored
    const char base[] = "/";
    const char rel[] = "charlie/.";
    CU_ASSERT("/charlie/" == http2::path_join(base, sizeof(base) - 1, nullptr,
                                              0, rel, sizeof(rel) - 1, nullptr,
                                              0));
  }
  {
    // query
    const char base[] = "/";
    const char rel[] = "/";
    const char relq[] = "q";
    CU_ASSERT("/?q" == http2::path_join(base, sizeof(base) - 1, nullptr, 0, rel,
                                        sizeof(rel) - 1, relq,
                                        sizeof(relq) - 1));
  }
  {
    // empty rel and query
    const char base[] = "/alpha";
    const char rel[] = "";
    const char relq[] = "q";
    CU_ASSERT("/alpha?q" == http2::path_join(base, sizeof(base) - 1, nullptr, 0,
                                             rel, sizeof(rel) - 1, relq,
                                             sizeof(relq) - 1));
  }
  {
    // both rel and query are empty
    const char base[] = "/alpha";
    const char baseq[] = "r";
    const char rel[] = "";
    const char relq[] = "";
    CU_ASSERT("/alpha?r" ==
              http2::path_join(base, sizeof(base) - 1, baseq, sizeof(baseq) - 1,
                               rel, sizeof(rel) - 1, relq, sizeof(relq) - 1));
  }
  {
    // empty base
    const char base[] = "";
    const char rel[] = "/alpha";
    CU_ASSERT("/alpha" == http2::path_join(base, sizeof(base) - 1, nullptr, 0,
                                           rel, sizeof(rel) - 1, nullptr, 0));
  }
  {
    // everything is empty
    CU_ASSERT("/" ==
              http2::path_join(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0));
  }
  {
    // only baseq is not empty
    const char base[] = "";
    const char baseq[] = "r";
    const char rel[] = "";
    CU_ASSERT("/?r" == http2::path_join(base, sizeof(base) - 1, baseq,
                                        sizeof(baseq) - 1, rel, sizeof(rel) - 1,
                                        nullptr, 0));
  }
}

void test_http2_normalize_path(void) {
  std::string src;

  src = "/alpha/bravo/../charlie";
  CU_ASSERT("/alpha/charlie" ==
            http2::normalize_path(std::begin(src), std::end(src)));

  src = "/a%6c%70%68%61";
  CU_ASSERT("/alpha" == http2::normalize_path(std::begin(src), std::end(src)));

  src = "/alpha%2f%3a";
  CU_ASSERT("/alpha%2F%3A" ==
            http2::normalize_path(std::begin(src), std::end(src)));

  src = "%2f";
  CU_ASSERT("/%2F" == http2::normalize_path(std::begin(src), std::end(src)));

  src = "%f";
  CU_ASSERT("/%f" == http2::normalize_path(std::begin(src), std::end(src)));

  src = "%";
  CU_ASSERT("/%" == http2::normalize_path(std::begin(src), std::end(src)));

  src = "";
  CU_ASSERT("/" == http2::normalize_path(std::begin(src), std::end(src)));
}

void test_http2_rewrite_clean_path(void) {
  std::string src;

  // unreserved characters
  src = "/alpha/%62ravo/";
  CU_ASSERT("/alpha/bravo/" ==
            http2::rewrite_clean_path(std::begin(src), std::end(src)));

  // percent-encoding is converted to upper case.
  src = "/delta%3a";
  CU_ASSERT("/delta%3A" ==
            http2::rewrite_clean_path(std::begin(src), std::end(src)));

  // path component is normalized before mathcing
  src = "/alpha/charlie/%2e././bravo/delta/..";
  CU_ASSERT("/alpha/bravo/" ==
            http2::rewrite_clean_path(std::begin(src), std::end(src)));

  src = "alpha%3a";
  CU_ASSERT(src == http2::rewrite_clean_path(std::begin(src), std::end(src)));

  src = "";
  CU_ASSERT(src == http2::rewrite_clean_path(std::begin(src), std::end(src)));
}

void test_http2_get_pure_path_component(void) {
  const char *base;
  size_t len;
  std::string path;

  path = "/";
  CU_ASSERT(0 == http2::get_pure_path_component(&base, &len, path));
  CU_ASSERT(util::streq_l("/", base, len));

  path = "/foo";
  CU_ASSERT(0 == http2::get_pure_path_component(&base, &len, path));
  CU_ASSERT(util::streq_l("/foo", base, len));

  path = "https://example.org/bar";
  CU_ASSERT(0 == http2::get_pure_path_component(&base, &len, path));
  CU_ASSERT(util::streq_l("/bar", base, len));

  path = "https://example.org/alpha?q=a";
  CU_ASSERT(0 == http2::get_pure_path_component(&base, &len, path));
  CU_ASSERT(util::streq_l("/alpha", base, len));

  path = "https://example.org/bravo?q=a#fragment";
  CU_ASSERT(0 == http2::get_pure_path_component(&base, &len, path));
  CU_ASSERT(util::streq_l("/bravo", base, len));

  path = "\x01\x02";
  CU_ASSERT(-1 == http2::get_pure_path_component(&base, &len, path));
}

void test_http2_construct_push_component(void) {
  const char *base;
  size_t baselen;
  std::string uri;
  std::string scheme, authority, path;

  base = "/b/";
  baselen = 3;

  uri = "https://example.org/foo";

  CU_ASSERT(0 == http2::construct_push_component(scheme, authority, path, base,
                                                 baselen, uri.c_str(),
                                                 uri.size()));
  CU_ASSERT("https" == scheme);
  CU_ASSERT("example.org" == authority);
  CU_ASSERT("/foo" == path);

  scheme.clear();
  authority.clear();
  path.clear();

  uri = "/foo/bar?q=a";

  CU_ASSERT(0 == http2::construct_push_component(scheme, authority, path, base,
                                                 baselen, uri.c_str(),
                                                 uri.size()));
  CU_ASSERT("" == scheme);
  CU_ASSERT("" == authority);
  CU_ASSERT("/foo/bar?q=a" == path);

  scheme.clear();
  authority.clear();
  path.clear();

  uri = "foo/../bar?q=a";

  CU_ASSERT(0 == http2::construct_push_component(scheme, authority, path, base,
                                                 baselen, uri.c_str(),
                                                 uri.size()));
  CU_ASSERT("" == scheme);
  CU_ASSERT("" == authority);
  CU_ASSERT("/b/bar?q=a" == path);

  scheme.clear();
  authority.clear();
  path.clear();

  uri = "";

  CU_ASSERT(0 == http2::construct_push_component(scheme, authority, path, base,
                                                 baselen, uri.c_str(),
                                                 uri.size()));
  CU_ASSERT("" == scheme);
  CU_ASSERT("" == authority);
  CU_ASSERT("/" == path);

  scheme.clear();
  authority.clear();
  path.clear();

  uri = "?q=a";

  CU_ASSERT(0 == http2::construct_push_component(scheme, authority, path, base,
                                                 baselen, uri.c_str(),
                                                 uri.size()));
  CU_ASSERT("" == scheme);
  CU_ASSERT("" == authority);
  CU_ASSERT("/b/?q=a" == path);
}

} // namespace shrpx
