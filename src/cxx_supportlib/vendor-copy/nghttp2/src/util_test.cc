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
#include "util_test.h"

#include <cstring>
#include <iostream>

#include <CUnit/CUnit.h>

#include <nghttp2/nghttp2.h>

#include "util.h"
#include "template.h"

using namespace nghttp2;

namespace shrpx {

void test_util_streq(void) {
  CU_ASSERT(util::streq("alpha", "alpha", 5));
  CU_ASSERT(util::streq("alpha", "alphabravo", 5));
  CU_ASSERT(!util::streq("alpha", "alphabravo", 6));
  CU_ASSERT(!util::streq("alphabravo", "alpha", 5));
  CU_ASSERT(!util::streq("alpha", "alphA", 5));
  CU_ASSERT(!util::streq("", "a", 1));
  CU_ASSERT(util::streq("", "", 0));
  CU_ASSERT(!util::streq("alpha", "", 0));

  CU_ASSERT(util::streq("alpha", 5, "alpha", 5));
  CU_ASSERT(!util::streq("alpha", 4, "alpha", 5));
  CU_ASSERT(!util::streq("alpha", 5, "alpha", 4));
  CU_ASSERT(!util::streq("alpha", 5, "alphA", 5));
  char *a = nullptr;
  char *b = nullptr;
  CU_ASSERT(util::streq(a, 0, b, 0));

  CU_ASSERT(util::streq_l("alpha", "alpha", 5));
  CU_ASSERT(util::streq_l("alpha", "alphabravo", 5));
  CU_ASSERT(!util::streq_l("alpha", "alphabravo", 6));
  CU_ASSERT(!util::streq_l("alphabravo", "alpha", 5));
  CU_ASSERT(!util::streq_l("alpha", "alphA", 5));
  CU_ASSERT(!util::streq_l("", "a", 1));
  CU_ASSERT(util::streq_l("", "", 0));
  CU_ASSERT(!util::streq_l("alpha", "", 0));
}

void test_util_strieq(void) {
  CU_ASSERT(util::strieq(std::string("alpha"), std::string("alpha")));
  CU_ASSERT(util::strieq(std::string("alpha"), std::string("AlPhA")));
  CU_ASSERT(util::strieq(std::string(), std::string()));
  CU_ASSERT(!util::strieq(std::string("alpha"), std::string("AlPhA ")));
  CU_ASSERT(!util::strieq(std::string(), std::string("AlPhA ")));

  CU_ASSERT(util::strieq("alpha", "alpha", 5));
  CU_ASSERT(util::strieq("alpha", "AlPhA", 5));
  CU_ASSERT(util::strieq("", static_cast<const char *>(nullptr), 0));
  CU_ASSERT(!util::strieq("alpha", "AlPhA ", 6));
  CU_ASSERT(!util::strieq("", "AlPhA ", 6));

  CU_ASSERT(util::strieq("alpha", "alpha"));
  CU_ASSERT(util::strieq("alpha", "AlPhA"));
  CU_ASSERT(util::strieq("", ""));
  CU_ASSERT(!util::strieq("alpha", "AlPhA "));
  CU_ASSERT(!util::strieq("", "AlPhA "));

  CU_ASSERT(util::strieq_l("alpha", "alpha", 5));
  CU_ASSERT(util::strieq_l("alpha", "AlPhA", 5));
  CU_ASSERT(util::strieq_l("", static_cast<const char *>(nullptr), 0));
  CU_ASSERT(!util::strieq_l("alpha", "AlPhA ", 6));
  CU_ASSERT(!util::strieq_l("", "AlPhA ", 6));

  CU_ASSERT(util::strieq_l("alpha", "alpha"));
  CU_ASSERT(util::strieq_l("alpha", "AlPhA"));
  CU_ASSERT(util::strieq_l("", ""));
  CU_ASSERT(!util::strieq_l("alpha", "AlPhA "));
  CU_ASSERT(!util::strieq_l("", "AlPhA "));
}

void test_util_inp_strlower(void) {
  std::string a("alPha");
  util::inp_strlower(a);
  CU_ASSERT("alpha" == a);

  a = "ALPHA123BRAVO";
  util::inp_strlower(a);
  CU_ASSERT("alpha123bravo" == a);

  a = "";
  util::inp_strlower(a);
  CU_ASSERT("" == a);
}

void test_util_to_base64(void) {
  std::string x = "AAA--B_";
  util::to_base64(x);
  CU_ASSERT("AAA++B/=" == x);

  x = "AAA--B_B";
  util::to_base64(x);
  CU_ASSERT("AAA++B/B" == x);
}

void test_util_to_token68(void) {
  std::string x = "AAA++B/=";
  util::to_token68(x);
  CU_ASSERT("AAA--B_" == x);

  x = "AAA++B/B";
  util::to_token68(x);
  CU_ASSERT("AAA--B_B" == x);
}

void test_util_percent_encode_token(void) {
  CU_ASSERT("h2" == util::percent_encode_token("h2"));
  CU_ASSERT("h3~" == util::percent_encode_token("h3~"));
  CU_ASSERT("100%25" == util::percent_encode_token("100%"));
  CU_ASSERT("http%202" == util::percent_encode_token("http 2"));
}

void test_util_percent_encode_path(void) {
  CU_ASSERT("/foo1/bar%3F&/%0A" == util::percent_encode_path("/foo1/bar?&/"
                                                             "\x0a"));
}

void test_util_percent_decode(void) {
  {
    std::string s = "%66%6F%6f%62%61%72";
    CU_ASSERT("foobar" == util::percent_decode(std::begin(s), std::end(s)));
  }
  {
    std::string s = "%66%6";
    CU_ASSERT("f%6" == util::percent_decode(std::begin(s), std::end(s)));
  }
  {
    std::string s = "%66%";
    CU_ASSERT("f%" == util::percent_decode(std::begin(s), std::end(s)));
  }
}

void test_util_quote_string(void) {
  CU_ASSERT("alpha" == util::quote_string("alpha"));
  CU_ASSERT("" == util::quote_string(""));
  CU_ASSERT("\\\"alpha\\\"" == util::quote_string("\"alpha\""));
}

void test_util_utox(void) {
  CU_ASSERT("0" == util::utox(0));
  CU_ASSERT("1" == util::utox(1));
  CU_ASSERT("F" == util::utox(15));
  CU_ASSERT("10" == util::utox(16));
  CU_ASSERT("3B9ACA07" == util::utox(1000000007));
  CU_ASSERT("100000000" == util::utox(1LL << 32));
}

void test_util_http_date(void) {
  CU_ASSERT("Thu, 01 Jan 1970 00:00:00 GMT" == util::http_date(0));
  CU_ASSERT("Wed, 29 Feb 2012 09:15:16 GMT" == util::http_date(1330506916));
}

void test_util_select_h2(void) {
  const unsigned char *out = NULL;
  unsigned char outlen = 0;

  // Check single entry and select it.
  const unsigned char t1[] = "\x2h2";
  CU_ASSERT(util::select_h2(&out, &outlen, t1, sizeof(t1) - 1));
  CU_ASSERT(
      memcmp(NGHTTP2_PROTO_VERSION_ID, out, NGHTTP2_PROTO_VERSION_ID_LEN) == 0);
  CU_ASSERT(NGHTTP2_PROTO_VERSION_ID_LEN == outlen);

  out = NULL;
  outlen = 0;

  // Check the case where id is correct but length is invalid and too
  // long.
  const unsigned char t2[] = "\x6h2-14";
  CU_ASSERT(!util::select_h2(&out, &outlen, t2, sizeof(t2) - 1));

  // Check the case where h2 is located after bogus ID.
  const unsigned char t3[] = "\x2h3\x2h2";
  CU_ASSERT(util::select_h2(&out, &outlen, t3, sizeof(t3) - 1));

  CU_ASSERT(
      memcmp(NGHTTP2_PROTO_VERSION_ID, out, NGHTTP2_PROTO_VERSION_ID_LEN) == 0);
  CU_ASSERT(NGHTTP2_PROTO_VERSION_ID_LEN == outlen);

  out = NULL;
  outlen = 0;

  // Check the case that last entry's length is invalid and too long.
  const unsigned char t4[] = "\x2h3\x6h2-14";
  CU_ASSERT(!util::select_h2(&out, &outlen, t4, sizeof(t4) - 1));

  // Check the case that all entries are not supported.
  const unsigned char t5[] = "\x2h3\x2h4";
  CU_ASSERT(!util::select_h2(&out, &outlen, t5, sizeof(t5) - 1));

  // Check the case where 2 values are eligible, but last one is
  // picked up because it has precedence over the other.
  const unsigned char t6[] = "\x5h2-14\x5h2-16";
  CU_ASSERT(util::select_h2(&out, &outlen, t6, sizeof(t6) - 1));
  CU_ASSERT(memcmp(NGHTTP2_H2_16, out, str_size(NGHTTP2_H2_16)) == 0);
  CU_ASSERT(str_size(NGHTTP2_H2_16) == outlen);
}

void test_util_ipv6_numeric_addr(void) {
  CU_ASSERT(util::ipv6_numeric_addr("::1"));
  CU_ASSERT(util::ipv6_numeric_addr("2001:0db8:85a3:0042:1000:8a2e:0370:7334"));
  // IPv4
  CU_ASSERT(!util::ipv6_numeric_addr("127.0.0.1"));
  // not numeric address
  CU_ASSERT(!util::ipv6_numeric_addr("localhost"));
}

void test_util_utos_unit(void) {
  CU_ASSERT("0" == util::utos_unit(0));
  CU_ASSERT("1023" == util::utos_unit(1023));
  CU_ASSERT("1K" == util::utos_unit(1024));
  CU_ASSERT("1K" == util::utos_unit(1025));
  CU_ASSERT("1M" == util::utos_unit(1 << 20));
  CU_ASSERT("1G" == util::utos_unit(1 << 30));
  CU_ASSERT("1024G" == util::utos_unit(1LL << 40));
}

void test_util_utos_funit(void) {
  CU_ASSERT("0" == util::utos_funit(0));
  CU_ASSERT("1023" == util::utos_funit(1023));
  CU_ASSERT("1.00K" == util::utos_funit(1024));
  CU_ASSERT("1.00K" == util::utos_funit(1025));
  CU_ASSERT("1.09K" == util::utos_funit(1119));
  CU_ASSERT("1.27K" == util::utos_funit(1300));
  CU_ASSERT("1.00M" == util::utos_funit(1 << 20));
  CU_ASSERT("1.18M" == util::utos_funit(1234567));
  CU_ASSERT("1.00G" == util::utos_funit(1 << 30));
  CU_ASSERT("4492450797.23G" == util::utos_funit(4823732313248234343LL));
  CU_ASSERT("1024.00G" == util::utos_funit(1LL << 40));
}

void test_util_parse_uint_with_unit(void) {
  CU_ASSERT(0 == util::parse_uint_with_unit("0"));
  CU_ASSERT(1023 == util::parse_uint_with_unit("1023"));
  CU_ASSERT(1024 == util::parse_uint_with_unit("1k"));
  CU_ASSERT(2048 == util::parse_uint_with_unit("2K"));
  CU_ASSERT(1 << 20 == util::parse_uint_with_unit("1m"));
  CU_ASSERT(1 << 21 == util::parse_uint_with_unit("2M"));
  CU_ASSERT(1 << 30 == util::parse_uint_with_unit("1g"));
  CU_ASSERT(1LL << 31 == util::parse_uint_with_unit("2G"));
  CU_ASSERT(9223372036854775807LL ==
            util::parse_uint_with_unit("9223372036854775807"));
  // check overflow case
  CU_ASSERT(-1 == util::parse_uint_with_unit("9223372036854775808"));
  CU_ASSERT(-1 == util::parse_uint_with_unit("10000000000000000000"));
  CU_ASSERT(-1 == util::parse_uint_with_unit("9223372036854775807G"));
  // bad characters
  CU_ASSERT(-1 == util::parse_uint_with_unit("1.1"));
  CU_ASSERT(-1 == util::parse_uint_with_unit("1a"));
  CU_ASSERT(-1 == util::parse_uint_with_unit("a1"));
  CU_ASSERT(-1 == util::parse_uint_with_unit("1T"));
  CU_ASSERT(-1 == util::parse_uint_with_unit(""));
}

void test_util_parse_uint(void) {
  CU_ASSERT(0 == util::parse_uint("0"));
  CU_ASSERT(1023 == util::parse_uint("1023"));
  CU_ASSERT(-1 == util::parse_uint("1k"));
  CU_ASSERT(9223372036854775807LL == util::parse_uint("9223372036854775807"));
  // check overflow case
  CU_ASSERT(-1 == util::parse_uint("9223372036854775808"));
  CU_ASSERT(-1 == util::parse_uint("10000000000000000000"));
  // bad characters
  CU_ASSERT(-1 == util::parse_uint("1.1"));
  CU_ASSERT(-1 == util::parse_uint("1a"));
  CU_ASSERT(-1 == util::parse_uint("a1"));
  CU_ASSERT(-1 == util::parse_uint("1T"));
  CU_ASSERT(-1 == util::parse_uint(""));
}

void test_util_parse_duration_with_unit(void) {
  CU_ASSERT(0. == util::parse_duration_with_unit("0"));
  CU_ASSERT(123. == util::parse_duration_with_unit("123"));
  CU_ASSERT(123. == util::parse_duration_with_unit("123s"));
  CU_ASSERT(0.500 == util::parse_duration_with_unit("500ms"));
  CU_ASSERT(123. == util::parse_duration_with_unit("123S"));
  CU_ASSERT(0.500 == util::parse_duration_with_unit("500MS"));
  CU_ASSERT(180 == util::parse_duration_with_unit("3m"));
  CU_ASSERT(3600 * 5 == util::parse_duration_with_unit("5h"));

  auto err = std::numeric_limits<double>::infinity();
  // check overflow case
  CU_ASSERT(err == util::parse_duration_with_unit("9223372036854775808"));
  // bad characters
  CU_ASSERT(err == util::parse_duration_with_unit("0u"));
  CU_ASSERT(err == util::parse_duration_with_unit("0xs"));
  CU_ASSERT(err == util::parse_duration_with_unit("0mt"));
  CU_ASSERT(err == util::parse_duration_with_unit("0mss"));
  CU_ASSERT(err == util::parse_duration_with_unit("s"));
  CU_ASSERT(err == util::parse_duration_with_unit("ms"));
}

void test_util_duration_str(void) {
  CU_ASSERT("0" == util::duration_str(0.));
  CU_ASSERT("1s" == util::duration_str(1.));
  CU_ASSERT("500ms" == util::duration_str(0.5));
  CU_ASSERT("1500ms" == util::duration_str(1.5));
  CU_ASSERT("2m" == util::duration_str(120.));
  CU_ASSERT("121s" == util::duration_str(121.));
  CU_ASSERT("1h" == util::duration_str(3600.));
}

void test_util_format_duration(void) {
  CU_ASSERT("0us" == util::format_duration(std::chrono::microseconds(0)));
  CU_ASSERT("999us" == util::format_duration(std::chrono::microseconds(999)));
  CU_ASSERT("1.00ms" == util::format_duration(std::chrono::microseconds(1000)));
  CU_ASSERT("1.09ms" == util::format_duration(std::chrono::microseconds(1090)));
  CU_ASSERT("1.01ms" == util::format_duration(std::chrono::microseconds(1009)));
  CU_ASSERT("999.99ms" ==
            util::format_duration(std::chrono::microseconds(999990)));
  CU_ASSERT("1.00s" ==
            util::format_duration(std::chrono::microseconds(1000000)));
  CU_ASSERT("1.05s" ==
            util::format_duration(std::chrono::microseconds(1050000)));

  CU_ASSERT("0us" == util::format_duration(0.));
  CU_ASSERT("999us" == util::format_duration(0.000999));
  CU_ASSERT("1.00ms" == util::format_duration(0.001));
  CU_ASSERT("1.09ms" == util::format_duration(0.00109));
  CU_ASSERT("1.01ms" == util::format_duration(0.001009));
  CU_ASSERT("999.99ms" == util::format_duration(0.99999));
  CU_ASSERT("1.00s" == util::format_duration(1.));
  CU_ASSERT("1.05s" == util::format_duration(1.05));
}

void test_util_starts_with(void) {
  CU_ASSERT(util::starts_with("foo", "foo"));
  CU_ASSERT(util::starts_with("fooo", "foo"));
  CU_ASSERT(util::starts_with("ofoo", ""));
  CU_ASSERT(!util::starts_with("ofoo", "foo"));

  CU_ASSERT(util::istarts_with("FOO", "fOO"));
  CU_ASSERT(util::starts_with("ofoo", ""));
  CU_ASSERT(util::istarts_with("fOOo", "Foo"));
  CU_ASSERT(!util::istarts_with("ofoo", "foo"));

  CU_ASSERT(util::istarts_with_l("fOOo", "Foo"));
  CU_ASSERT(!util::istarts_with_l("ofoo", "foo"));
}

void test_util_ends_with(void) {
  CU_ASSERT(util::ends_with("foo", "foo"));
  CU_ASSERT(util::ends_with("foo", ""));
  CU_ASSERT(util::ends_with("ofoo", "foo"));
  CU_ASSERT(!util::ends_with("ofoo", "fo"));

  CU_ASSERT(util::iends_with("fOo", "Foo"));
  CU_ASSERT(util::iends_with("foo", ""));
  CU_ASSERT(util::iends_with("oFoo", "fOO"));
  CU_ASSERT(!util::iends_with("ofoo", "fo"));

  CU_ASSERT(util::iends_with_l("oFoo", "fOO"));
  CU_ASSERT(!util::iends_with_l("ofoo", "fo"));
}

void test_util_parse_http_date(void) {
  CU_ASSERT(1001939696 ==
            util::parse_http_date("Mon, 1 Oct 2001 12:34:56 GMT"));
}

void test_util_localtime_date(void) {
  auto tz = getenv("TZ");
  if (tz) {
    tz = strdup(tz);
  }
  setenv("TZ", ":Pacific/Auckland", 1);
  tzset();

  CU_ASSERT_STRING_EQUAL("02/Oct/2001:00:34:56 +1200",
                         util::common_log_date(1001939696).c_str());
  CU_ASSERT_STRING_EQUAL("2001-10-02T00:34:56.123+12:00",
                         util::iso8601_date(1001939696000LL + 123).c_str());

  if (tz) {
    setenv("TZ", tz, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
}

void test_util_get_uint64(void) {
  {
    auto v = std::array<unsigned char, 8>{
        {0x01, 0x12, 0x34, 0x56, 0xff, 0x9a, 0xab, 0xbc}};

    auto n = util::get_uint64(v.data());

    CU_ASSERT(0x01123456ff9aabbcULL == n);
  }
  {
    auto v = std::array<unsigned char, 8>{
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

    auto n = util::get_uint64(v.data());

    CU_ASSERT(0xffffffffffffffffULL == n);
  }
}

void test_util_parse_config_str_list(void) {
  auto res = util::parse_config_str_list("a");
  CU_ASSERT(1 == res.size());
  CU_ASSERT("a" == res[0]);

  res = util::parse_config_str_list("a,");
  CU_ASSERT(2 == res.size());
  CU_ASSERT("a" == res[0]);
  CU_ASSERT("" == res[1]);

  res = util::parse_config_str_list(":a::", ':');
  CU_ASSERT(4 == res.size());
  CU_ASSERT("" == res[0]);
  CU_ASSERT("a" == res[1]);
  CU_ASSERT("" == res[2]);
  CU_ASSERT("" == res[3]);

  res = util::parse_config_str_list("");
  CU_ASSERT(1 == res.size());
  CU_ASSERT("" == res[0]);

  res = util::parse_config_str_list("alpha,bravo,charlie");
  CU_ASSERT(3 == res.size());
  CU_ASSERT("alpha" == res[0]);
  CU_ASSERT("bravo" == res[1]);
  CU_ASSERT("charlie" == res[2]);
}

} // namespace shrpx
