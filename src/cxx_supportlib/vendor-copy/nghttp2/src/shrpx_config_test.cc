/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
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
#include "shrpx_config_test.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H

#include <cstdlib>

#include <CUnit/CUnit.h>

#include "shrpx_config.h"

namespace shrpx {

void test_shrpx_config_parse_header(void) {
  auto p = parse_header("a: b");
  CU_ASSERT("a" == p.first);
  CU_ASSERT("b" == p.second);

  p = parse_header("a:  b");
  CU_ASSERT("a" == p.first);
  CU_ASSERT("b" == p.second);

  p = parse_header(":a: b");
  CU_ASSERT(p.first.empty());

  p = parse_header("a: :b");
  CU_ASSERT("a" == p.first);
  CU_ASSERT(":b" == p.second);

  p = parse_header(": b");
  CU_ASSERT(p.first.empty());

  p = parse_header("alpha: bravo charlie");
  CU_ASSERT("alpha" == p.first);
  CU_ASSERT("bravo charlie" == p.second);

  p = parse_header("a,: b");
  CU_ASSERT(p.first.empty());

  p = parse_header("a: b\x0a");
  CU_ASSERT(p.first.empty());
}

void test_shrpx_config_parse_log_format(void) {
  auto res =
      parse_log_format(R"($remote_addr - $remote_user [$time_local] )"
                       R"("$request" $status $body_bytes_sent )"
                       R"("${http_referer}" $http_host "$http_user_agent")");
  CU_ASSERT(16 == res.size());

  CU_ASSERT(SHRPX_LOGF_REMOTE_ADDR == res[0].type);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[1].type);
  CU_ASSERT(" - $remote_user [" == res[1].value);

  CU_ASSERT(SHRPX_LOGF_TIME_LOCAL == res[2].type);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[3].type);
  CU_ASSERT("] \"" == res[3].value);

  CU_ASSERT(SHRPX_LOGF_REQUEST == res[4].type);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[5].type);
  CU_ASSERT("\" " == res[5].value);

  CU_ASSERT(SHRPX_LOGF_STATUS == res[6].type);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[7].type);
  CU_ASSERT(" " == res[7].value);

  CU_ASSERT(SHRPX_LOGF_BODY_BYTES_SENT == res[8].type);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[9].type);
  CU_ASSERT(" \"" == res[9].value);

  CU_ASSERT(SHRPX_LOGF_HTTP == res[10].type);
  CU_ASSERT("referer" == res[10].value);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[11].type);
  CU_ASSERT("\" " == res[11].value);

  CU_ASSERT(SHRPX_LOGF_AUTHORITY == res[12].type);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[13].type);
  CU_ASSERT(" \"" == res[13].value);

  CU_ASSERT(SHRPX_LOGF_HTTP == res[14].type);
  CU_ASSERT("user-agent" == res[14].value);

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[15].type);
  CU_ASSERT("\"" == res[15].value);

  res = parse_log_format("$");

  CU_ASSERT(1 == res.size());

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[0].type);
  CU_ASSERT("$" == res[0].value);

  res = parse_log_format("${");

  CU_ASSERT(1 == res.size());

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[0].type);
  CU_ASSERT("${" == res[0].value);

  res = parse_log_format("${a");

  CU_ASSERT(1 == res.size());

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[0].type);
  CU_ASSERT("${a" == res[0].value);

  res = parse_log_format("${a ");

  CU_ASSERT(1 == res.size());

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[0].type);
  CU_ASSERT("${a " == res[0].value);

  res = parse_log_format("$$remote_addr");

  CU_ASSERT(2 == res.size());

  CU_ASSERT(SHRPX_LOGF_LITERAL == res[0].type);
  CU_ASSERT("$" == res[0].value);

  CU_ASSERT(SHRPX_LOGF_REMOTE_ADDR == res[1].type);
  CU_ASSERT("" == res[1].value);
}

void test_shrpx_config_read_tls_ticket_key_file(void) {
  char file1[] = "/tmp/nghttpx-unittest.XXXXXX";
  auto fd1 = mkstemp(file1);
  assert(fd1 != -1);
  assert(48 ==
         write(fd1, "0..............12..............34..............5", 48));
  char file2[] = "/tmp/nghttpx-unittest.XXXXXX";
  auto fd2 = mkstemp(file2);
  assert(fd2 != -1);
  assert(48 ==
         write(fd2, "6..............78..............9a..............b", 48));

  close(fd1);
  close(fd2);
  auto ticket_keys =
      read_tls_ticket_key_file({file1, file2}, EVP_aes_128_cbc(), EVP_sha256());
  unlink(file1);
  unlink(file2);
  CU_ASSERT(ticket_keys.get() != nullptr);
  CU_ASSERT(2 == ticket_keys->keys.size());
  auto key = &ticket_keys->keys[0];
  CU_ASSERT(std::equal(std::begin(key->data.name), std::end(key->data.name),
                       "0..............1"));
  CU_ASSERT(std::equal(std::begin(key->data.enc_key),
                       std::begin(key->data.enc_key) + 16, "2..............3"));
  CU_ASSERT(std::equal(std::begin(key->data.hmac_key),
                       std::begin(key->data.hmac_key) + 16,
                       "4..............5"));
  CU_ASSERT(16 == key->hmac_keylen);

  key = &ticket_keys->keys[1];
  CU_ASSERT(std::equal(std::begin(key->data.name), std::end(key->data.name),
                       "6..............7"));
  CU_ASSERT(std::equal(std::begin(key->data.enc_key),
                       std::begin(key->data.enc_key) + 16, "8..............9"));
  CU_ASSERT(std::equal(std::begin(key->data.hmac_key),
                       std::begin(key->data.hmac_key) + 16,
                       "a..............b"));
  CU_ASSERT(16 == key->hmac_keylen);
}

void test_shrpx_config_read_tls_ticket_key_file_aes_256(void) {
  char file1[] = "/tmp/nghttpx-unittest.XXXXXX";
  auto fd1 = mkstemp(file1);
  assert(fd1 != -1);
  assert(80 == write(fd1, "0..............12..............................34..."
                          "...........................5",
                     80));
  char file2[] = "/tmp/nghttpx-unittest.XXXXXX";
  auto fd2 = mkstemp(file2);
  assert(fd2 != -1);
  assert(80 == write(fd2, "6..............78..............................9a..."
                          "...........................b",
                     80));

  close(fd1);
  close(fd2);
  auto ticket_keys =
      read_tls_ticket_key_file({file1, file2}, EVP_aes_256_cbc(), EVP_sha256());
  unlink(file1);
  unlink(file2);
  CU_ASSERT(ticket_keys.get() != nullptr);
  CU_ASSERT(2 == ticket_keys->keys.size());
  auto key = &ticket_keys->keys[0];
  CU_ASSERT(std::equal(std::begin(key->data.name), std::end(key->data.name),
                       "0..............1"));
  CU_ASSERT(std::equal(std::begin(key->data.enc_key),
                       std::end(key->data.enc_key),
                       "2..............................3"));
  CU_ASSERT(std::equal(std::begin(key->data.hmac_key),
                       std::end(key->data.hmac_key),
                       "4..............................5"));

  key = &ticket_keys->keys[1];
  CU_ASSERT(std::equal(std::begin(key->data.name), std::end(key->data.name),
                       "6..............7"));
  CU_ASSERT(std::equal(std::begin(key->data.enc_key),
                       std::end(key->data.enc_key),
                       "8..............................9"));
  CU_ASSERT(std::equal(std::begin(key->data.hmac_key),
                       std::end(key->data.hmac_key),
                       "a..............................b"));
}

void test_shrpx_config_match_downstream_addr_group(void) {
  auto groups = std::vector<DownstreamAddrGroup>{
      {"nghttp2.org/"},
      {"nghttp2.org/alpha/bravo/"},
      {"nghttp2.org/alpha/charlie"},
      {"nghttp2.org/delta%3A"},
      {"www.nghttp2.org/"},
      {"[::1]/"},
      {"nghttp2.org/alpha/bravo/delta"},
      // Check that match is done in the single node
      {"example.com/alpha/bravo"},
      {"192.168.0.1/alpha/"},
  };

  Router router;

  for (size_t i = 0; i < groups.size(); ++i) {
    auto &g = groups[i];
    router.add_route(g.pattern.get(), strlen(g.pattern.get()), i);
  }

  CU_ASSERT(0 == match_downstream_addr_group(router, "nghttp2.org", "/", groups,
                                             255));

  // port is removed
  CU_ASSERT(0 == match_downstream_addr_group(router, "nghttp2.org:8080", "/",
                                             groups, 255));

  // host is case-insensitive
  CU_ASSERT(4 == match_downstream_addr_group(router, "WWW.nghttp2.org",
                                             "/alpha", groups, 255));

  CU_ASSERT(1 == match_downstream_addr_group(router, "nghttp2.org",
                                             "/alpha/bravo/", groups, 255));

  // /alpha/bravo also matches /alpha/bravo/
  CU_ASSERT(1 == match_downstream_addr_group(router, "nghttp2.org",
                                             "/alpha/bravo", groups, 255));

  // path part is case-sensitive
  CU_ASSERT(0 == match_downstream_addr_group(router, "nghttp2.org",
                                             "/Alpha/bravo", groups, 255));

  CU_ASSERT(1 == match_downstream_addr_group(router, "nghttp2.org",
                                             "/alpha/bravo/charlie", groups,
                                             255));

  CU_ASSERT(2 == match_downstream_addr_group(router, "nghttp2.org",
                                             "/alpha/charlie", groups, 255));

  // pattern which does not end with '/' must match its entirely.  So
  // this matches to group 0, not group 2.
  CU_ASSERT(0 == match_downstream_addr_group(router, "nghttp2.org",
                                             "/alpha/charlie/", groups, 255));

  CU_ASSERT(255 == match_downstream_addr_group(router, "example.org", "/",
                                               groups, 255));

  CU_ASSERT(255 == match_downstream_addr_group(router, "", "/", groups, 255));

  CU_ASSERT(255 ==
            match_downstream_addr_group(router, "", "alpha", groups, 255));

  CU_ASSERT(255 ==
            match_downstream_addr_group(router, "foo/bar", "/", groups, 255));

  // If path is "*", only match with host + "/".
  CU_ASSERT(0 == match_downstream_addr_group(router, "nghttp2.org", "*", groups,
                                             255));

  CU_ASSERT(5 ==
            match_downstream_addr_group(router, "[::1]", "/", groups, 255));
  CU_ASSERT(
      5 == match_downstream_addr_group(router, "[::1]:8080", "/", groups, 255));
  CU_ASSERT(255 ==
            match_downstream_addr_group(router, "[::1", "/", groups, 255));
  CU_ASSERT(255 ==
            match_downstream_addr_group(router, "[::1]8000", "/", groups, 255));

  // Check the case where adding route extends tree
  CU_ASSERT(6 == match_downstream_addr_group(
                     router, "nghttp2.org", "/alpha/bravo/delta", groups, 255));

  CU_ASSERT(1 == match_downstream_addr_group(router, "nghttp2.org",
                                             "/alpha/bravo/delta/", groups,
                                             255));

  // Check the case where query is done in a single node
  CU_ASSERT(7 == match_downstream_addr_group(router, "example.com",
                                             "/alpha/bravo", groups, 255));

  CU_ASSERT(255 == match_downstream_addr_group(router, "example.com",
                                               "/alpha/bravo/", groups, 255));

  CU_ASSERT(255 == match_downstream_addr_group(router, "example.com", "/alpha",
                                               groups, 255));

  // Check the case where quey is done in a single node
  CU_ASSERT(8 == match_downstream_addr_group(router, "192.168.0.1", "/alpha",
                                             groups, 255));

  CU_ASSERT(8 == match_downstream_addr_group(router, "192.168.0.1", "/alpha/",
                                             groups, 255));

  CU_ASSERT(8 == match_downstream_addr_group(router, "192.168.0.1",
                                             "/alpha/bravo", groups, 255));

  CU_ASSERT(255 == match_downstream_addr_group(router, "192.168.0.1", "/alph",
                                               groups, 255));

  CU_ASSERT(255 == match_downstream_addr_group(router, "192.168.0.1", "/",
                                               groups, 255));

  router.dump();
}

} // namespace shrpx
