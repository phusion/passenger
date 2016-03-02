/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
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
#include "util.h"

#ifdef HAVE_TIME_H
#include <time.h>
#endif // HAVE_TIME_H
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif // HAVE_SYS_SOCKET_H
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif // HAVE_NETDB_H
#include <sys/stat.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif // HAVE_FCNTL_H
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif // HAVE_NETINET_IN_H
#include <netinet/tcp.h>
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif // HAVE_ARPA_INET_H

#include <cmath>
#include <cerrno>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>

#include <nghttp2/nghttp2.h>

#include "timegm.h"
#include "template.h"

namespace nghttp2 {

namespace util {

const char UPPER_XDIGITS[] = "0123456789ABCDEF";

bool in_rfc3986_unreserved_chars(const char c) {
  static constexpr const char unreserved[] = {'-', '.', '_', '~'};
  return is_alpha(c) || is_digit(c) ||
         std::find(std::begin(unreserved), std::end(unreserved), c) !=
             std::end(unreserved);
}

bool in_rfc3986_sub_delims(const char c) {
  static constexpr const char sub_delims[] = {'!', '$', '&', '\'', '(', ')',
                                              '*', '+', ',', ';',  '='};
  return std::find(std::begin(sub_delims), std::end(sub_delims), c) !=
         std::end(sub_delims);
}

std::string percent_encode(const unsigned char *target, size_t len) {
  std::string dest;
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = target[i];

    if (in_rfc3986_unreserved_chars(c)) {
      dest += c;
    } else {
      dest += '%';
      dest += UPPER_XDIGITS[c >> 4];
      dest += UPPER_XDIGITS[(c & 0x0f)];
    }
  }
  return dest;
}

std::string percent_encode(const std::string &target) {
  return percent_encode(reinterpret_cast<const unsigned char *>(target.c_str()),
                        target.size());
}

std::string percent_encode_path(const std::string &s) {
  std::string dest;
  for (auto c : s) {
    if (in_rfc3986_unreserved_chars(c) || in_rfc3986_sub_delims(c) ||
        c == '/') {
      dest += c;
      continue;
    }

    dest += '%';
    dest += UPPER_XDIGITS[(c >> 4) & 0x0f];
    dest += UPPER_XDIGITS[(c & 0x0f)];
  }
  return dest;
}

bool in_token(char c) {
  static constexpr const char extra[] = {'!',  '#', '$', '%', '&',
                                         '\'', '*', '+', '-', '.',
                                         '^',  '_', '`', '|', '~'};
  return is_alpha(c) || is_digit(c) ||
         std::find(std::begin(extra), std::end(extra), c) != std::end(extra);
}

bool in_attr_char(char c) {
  static constexpr const char bad[] = {'*', '\'', '%'};
  return util::in_token(c) &&
         std::find(std::begin(bad), std::end(bad), c) == std::end(bad);
}

std::string percent_encode_token(const std::string &target) {
  auto len = target.size();
  std::string dest;

  for (size_t i = 0; i < len; ++i) {
    unsigned char c = target[i];

    if (c != '%' && in_token(c)) {
      dest += c;
    } else {
      dest += '%';
      dest += UPPER_XDIGITS[c >> 4];
      dest += UPPER_XDIGITS[(c & 0x0f)];
    }
  }
  return dest;
}

uint32_t hex_to_uint(char c) {
  if (c <= '9') {
    return c - '0';
  }
  if (c <= 'Z') {
    return c - 'A' + 10;
  }
  if (c <= 'z') {
    return c - 'a' + 10;
  }
  return c;
}

std::string quote_string(const std::string &target) {
  auto cnt = std::count(std::begin(target), std::end(target), '"');

  if (cnt == 0) {
    return target;
  }

  std::string res;
  res.reserve(target.size() + cnt);

  for (auto c : target) {
    if (c == '"') {
      res += "\\\"";
    } else {
      res += c;
    }
  }

  return res;
}

namespace {
template <typename Iterator>
Iterator cpydig(Iterator d, uint32_t n, size_t len) {
  auto p = d + len - 1;

  do {
    *p-- = (n % 10) + '0';
    n /= 10;
  } while (p >= d);

  return d + len;
}
} // namespace

namespace {
const char *MONTH[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *DAY_OF_WEEK[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
} // namespace

std::string http_date(time_t t) {
  struct tm tms;
  std::string res;

  if (gmtime_r(&t, &tms) == nullptr) {
    return res;
  }

  /* Sat, 27 Sep 2014 06:31:15 GMT */
  res.resize(29);

  auto p = std::begin(res);

  auto s = DAY_OF_WEEK[tms.tm_wday];
  p = std::copy_n(s, 3, p);
  *p++ = ',';
  *p++ = ' ';
  p = cpydig(p, tms.tm_mday, 2);
  *p++ = ' ';
  s = MONTH[tms.tm_mon];
  p = std::copy_n(s, 3, p);
  *p++ = ' ';
  p = cpydig(p, tms.tm_year + 1900, 4);
  *p++ = ' ';
  p = cpydig(p, tms.tm_hour, 2);
  *p++ = ':';
  p = cpydig(p, tms.tm_min, 2);
  *p++ = ':';
  p = cpydig(p, tms.tm_sec, 2);
  s = " GMT";
  p = std::copy_n(s, 4, p);

  return res;
}

std::string common_log_date(time_t t) {
  struct tm tms;

  if (localtime_r(&t, &tms) == nullptr) {
    return "";
  }

  // Format data like this:
  // 03/Jul/2014:00:19:38 +0900
  std::string res;
  res.resize(26);

  auto p = std::begin(res);

  p = cpydig(p, tms.tm_mday, 2);
  *p++ = '/';
  auto s = MONTH[tms.tm_mon];
  p = std::copy_n(s, 3, p);
  *p++ = '/';
  p = cpydig(p, tms.tm_year + 1900, 4);
  *p++ = ':';
  p = cpydig(p, tms.tm_hour, 2);
  *p++ = ':';
  p = cpydig(p, tms.tm_min, 2);
  *p++ = ':';
  p = cpydig(p, tms.tm_sec, 2);
  *p++ = ' ';

#ifdef HAVE_STRUCT_TM_TM_GMTOFF
  auto gmtoff = tms.tm_gmtoff;
#else  // !HAVE_STRUCT_TM_TM_GMTOFF
  auto gmtoff = nghttp2_timegm(&tms) - t;
#endif // !HAVE_STRUCT_TM_TM_GMTOFF
  if (gmtoff >= 0) {
    *p++ = '+';
  } else {
    *p++ = '-';
    gmtoff = -gmtoff;
  }

  p = cpydig(p, gmtoff / 3600, 2);
  p = cpydig(p, (gmtoff % 3600) / 60, 2);

  return res;
}

std::string iso8601_date(int64_t ms) {
  time_t sec = ms / 1000;

  tm tms;
  if (localtime_r(&sec, &tms) == nullptr) {
    return "";
  }

  // Format data like this:
  // 2014-11-15T12:58:24.741Z
  // 2014-11-15T12:58:24.741+09:00
  std::string res;
  res.resize(29);

  auto p = std::begin(res);

  p = cpydig(p, tms.tm_year + 1900, 4);
  *p++ = '-';
  p = cpydig(p, tms.tm_mon + 1, 2);
  *p++ = '-';
  p = cpydig(p, tms.tm_mday, 2);
  *p++ = 'T';
  p = cpydig(p, tms.tm_hour, 2);
  *p++ = ':';
  p = cpydig(p, tms.tm_min, 2);
  *p++ = ':';
  p = cpydig(p, tms.tm_sec, 2);
  *p++ = '.';
  p = cpydig(p, ms % 1000, 3);

#ifdef HAVE_STRUCT_TM_TM_GMTOFF
  auto gmtoff = tms.tm_gmtoff;
#else  // !HAVE_STRUCT_TM_TM_GMTOFF
  auto gmtoff = nghttp2_timegm(&tms) - sec;
#endif // !HAVE_STRUCT_TM_TM_GMTOFF
  if (gmtoff == 0) {
    *p++ = 'Z';
  } else {
    if (gmtoff > 0) {
      *p++ = '+';
    } else {
      *p++ = '-';
      gmtoff = -gmtoff;
    }
    p = cpydig(p, gmtoff / 3600, 2);
    *p++ = ':';
    p = cpydig(p, (gmtoff % 3600) / 60, 2);
  }

  res.resize(p - std::begin(res));

  return res;
}

time_t parse_http_date(const std::string &s) {
  tm tm{};
  char *r = strptime(s.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm);
  if (r == 0) {
    return 0;
  }
  return nghttp2_timegm_without_yday(&tm);
}

namespace {
void streq_advance(const char **ap, const char **bp) {
  for (; **ap && **bp && lowcase(**ap) == lowcase(**bp); ++*ap, ++*bp)
    ;
}
} // namespace

bool istarts_with(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  streq_advance(&a, &b);
  return !*b;
}

bool strieq(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  for (; *a && *b && lowcase(*a) == lowcase(*b); ++a, ++b)
    ;
  return !*a && !*b;
}

int strcompare(const char *a, const uint8_t *b, size_t bn) {
  assert(a && b);
  const uint8_t *blast = b + bn;
  for (; *a && b != blast; ++a, ++b) {
    if (*a < *b) {
      return -1;
    } else if (*a > *b) {
      return 1;
    }
  }
  if (!*a && b == blast) {
    return 0;
  } else if (b == blast) {
    return 1;
  } else {
    return -1;
  }
}

bool strifind(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  for (size_t i = 0; a[i]; ++i) {
    const char *ap = &a[i], *bp = b;
    for (; *ap && *bp && lowcase(*ap) == lowcase(*bp); ++ap, ++bp)
      ;
    if (!*bp) {
      return true;
    }
  }
  return false;
}

char upcase(char c) {
  if ('a' <= c && c <= 'z') {
    return c - 'a' + 'A';
  } else {
    return c;
  }
}

namespace {
const char LOWER_XDIGITS[] = "0123456789abcdef";
} // namespace

std::string format_hex(const unsigned char *s, size_t len) {
  std::string res;
  res.resize(len * 2);

  for (size_t i = 0; i < len; ++i) {
    unsigned char c = s[i];

    res[i * 2] = LOWER_XDIGITS[c >> 4];
    res[i * 2 + 1] = LOWER_XDIGITS[c & 0x0f];
  }
  return res;
}

void to_token68(std::string &base64str) {
  std::transform(std::begin(base64str), std::end(base64str),
                 std::begin(base64str), [](char c) {
                   switch (c) {
                   case '+':
                     return '-';
                   case '/':
                     return '_';
                   default:
                     return c;
                   }
                 });
  base64str.erase(std::find(std::begin(base64str), std::end(base64str), '='),
                  std::end(base64str));
}

void to_base64(std::string &token68str) {
  std::transform(std::begin(token68str), std::end(token68str),
                 std::begin(token68str), [](char c) {
                   switch (c) {
                   case '-':
                     return '+';
                   case '_':
                     return '/';
                   default:
                     return c;
                   }
                 });
  if (token68str.size() & 0x3) {
    token68str.append(4 - (token68str.size() & 0x3), '=');
  }
  return;
}

namespace {
// Calculates Damerau–Levenshtein distance between c-string a and b
// with given costs.  swapcost, subcost, addcost and delcost are cost
// to swap 2 adjacent characters, substitute characters, add character
// and delete character respectively.
int levenshtein(const char *a, int alen, const char *b, int blen, int swapcost,
                int subcost, int addcost, int delcost) {
  auto dp = std::vector<std::vector<int>>(3, std::vector<int>(blen + 1));
  for (int i = 0; i <= blen; ++i) {
    dp[1][i] = i;
  }
  for (int i = 1; i <= alen; ++i) {
    dp[0][0] = i;
    for (int j = 1; j <= blen; ++j) {
      dp[0][j] = dp[1][j - 1] + (a[i - 1] == b[j - 1] ? 0 : subcost);
      if (i >= 2 && j >= 2 && a[i - 1] != b[j - 1] && a[i - 2] == b[j - 1] &&
          a[i - 1] == b[j - 2]) {
        dp[0][j] = std::min(dp[0][j], dp[2][j - 2] + swapcost);
      }
      dp[0][j] = std::min(dp[0][j],
                          std::min(dp[1][j] + delcost, dp[0][j - 1] + addcost));
    }
    std::rotate(std::begin(dp), std::begin(dp) + 2, std::end(dp));
  }
  return dp[1][blen];
}
} // namespace

void show_candidates(const char *unkopt, option *options) {
  for (; *unkopt == '-'; ++unkopt)
    ;
  if (*unkopt == '\0') {
    return;
  }
  auto unkoptend = unkopt;
  for (; *unkoptend && *unkoptend != '='; ++unkoptend)
    ;
  auto unkoptlen = unkoptend - unkopt;
  if (unkoptlen == 0) {
    return;
  }
  int prefix_match = 0;
  auto cands = std::vector<std::pair<int, const char *>>();
  for (size_t i = 0; options[i].name != nullptr; ++i) {
    auto optnamelen = strlen(options[i].name);
    // Use cost 0 for prefix match
    if (istarts_with(options[i].name, options[i].name + optnamelen, unkopt,
                     unkopt + unkoptlen)) {
      if (optnamelen == static_cast<size_t>(unkoptlen)) {
        // Exact match, then we don't show any condidates.
        return;
      }
      ++prefix_match;
      cands.emplace_back(0, options[i].name);
      continue;
    }
    // Use cost 0 for suffix match, but match at least 3 characters
    if (unkoptlen >= 3 &&
        iends_with(options[i].name, options[i].name + optnamelen, unkopt,
                   unkopt + unkoptlen)) {
      cands.emplace_back(0, options[i].name);
      continue;
    }
    // cost values are borrowed from git, help.c.
    int sim =
        levenshtein(unkopt, unkoptlen, options[i].name, optnamelen, 0, 2, 1, 3);
    cands.emplace_back(sim, options[i].name);
  }
  if (prefix_match == 1 || cands.empty()) {
    return;
  }
  std::sort(std::begin(cands), std::end(cands));
  int threshold = cands[0].first;
  // threshold value is a magic value.
  if (threshold > 6) {
    return;
  }
  std::cerr << "\nDid you mean:\n";
  for (auto &item : cands) {
    if (item.first > threshold) {
      break;
    }
    std::cerr << "\t--" << item.second << "\n";
  }
}

bool has_uri_field(const http_parser_url &u, http_parser_url_fields field) {
  return u.field_set & (1 << field);
}

bool fieldeq(const char *uri1, const http_parser_url &u1, const char *uri2,
             const http_parser_url &u2, http_parser_url_fields field) {
  if (!has_uri_field(u1, field)) {
    if (!has_uri_field(u2, field)) {
      return true;
    } else {
      return false;
    }
  } else if (!has_uri_field(u2, field)) {
    return false;
  }
  if (u1.field_data[field].len != u2.field_data[field].len) {
    return false;
  }
  return memcmp(uri1 + u1.field_data[field].off,
                uri2 + u2.field_data[field].off, u1.field_data[field].len) == 0;
}

bool fieldeq(const char *uri, const http_parser_url &u,
             http_parser_url_fields field, const char *t) {
  if (!has_uri_field(u, field)) {
    if (!t[0]) {
      return true;
    } else {
      return false;
    }
  } else if (!t[0]) {
    return false;
  }
  int i, len = u.field_data[field].len;
  const char *p = uri + u.field_data[field].off;
  for (i = 0; i < len && t[i] && p[i] == t[i]; ++i)
    ;
  return i == len && !t[i];
}

std::string get_uri_field(const char *uri, const http_parser_url &u,
                          http_parser_url_fields field) {
  if (util::has_uri_field(u, field)) {
    return std::string(uri + u.field_data[field].off, u.field_data[field].len);
  } else {
    return "";
  }
}

uint16_t get_default_port(const char *uri, const http_parser_url &u) {
  if (util::fieldeq(uri, u, UF_SCHEMA, "https")) {
    return 443;
  } else if (util::fieldeq(uri, u, UF_SCHEMA, "http")) {
    return 80;
  } else {
    return 443;
  }
}

bool porteq(const char *uri1, const http_parser_url &u1, const char *uri2,
            const http_parser_url &u2) {
  uint16_t port1, port2;
  port1 =
      util::has_uri_field(u1, UF_PORT) ? u1.port : get_default_port(uri1, u1);
  port2 =
      util::has_uri_field(u2, UF_PORT) ? u2.port : get_default_port(uri2, u2);
  return port1 == port2;
}

void write_uri_field(std::ostream &o, const char *uri, const http_parser_url &u,
                     http_parser_url_fields field) {
  if (util::has_uri_field(u, field)) {
    o.write(uri + u.field_data[field].off, u.field_data[field].len);
  }
}

bool numeric_host(const char *hostname) {
  return numeric_host(hostname, AF_INET) || numeric_host(hostname, AF_INET6);
}

bool numeric_host(const char *hostname, int family) {
  int rv;
  std::array<uint8_t, sizeof(struct in6_addr)> dst;

  rv = inet_pton(family, hostname, dst.data());

  return rv == 1;
}

std::string numeric_name(const struct sockaddr *sa, socklen_t salen) {
  std::array<char, NI_MAXHOST> host;
  auto rv = getnameinfo(sa, salen, host.data(), host.size(), nullptr, 0,
                        NI_NUMERICHOST);
  if (rv != 0) {
    return "unknown";
  }
  return host.data();
}

static int STDERR_COPY = -1;
static int STDOUT_COPY = -1;

void store_original_fds() {
  // consider dup'ing stdout too
  STDERR_COPY = dup(STDERR_FILENO);
  STDOUT_COPY = STDOUT_FILENO;
  // no race here, since it is called early
  make_socket_closeonexec(STDERR_COPY);
}

void restore_original_fds() { dup2(STDERR_COPY, STDERR_FILENO); }

void close_log_file(int &fd) {
  if (fd != STDERR_COPY && fd != STDOUT_COPY && fd != -1) {
    close(fd);
  }
  fd = -1;
}

int open_log_file(const char *path) {

  if (strcmp(path, "/dev/stdout") == 0 ||
      strcmp(path, "/proc/self/fd/1") == 0) {
    return STDOUT_COPY;
  }

  if (strcmp(path, "/dev/stderr") == 0 ||
      strcmp(path, "/proc/self/fd/2") == 0) {
    return STDERR_COPY;
  }
#if defined O_CLOEXEC

  auto fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC,
                 S_IRUSR | S_IWUSR | S_IRGRP);
#else // !O_CLOEXEC

  auto fd =
      open(path, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);

  // We get race condition if execve is called at the same time.
  if (fd != -1) {
    make_socket_closeonexec(fd);
  }

#endif // !O_CLOEXEC

  if (fd == -1) {
    return -1;
  }

  return fd;
}

std::string ascii_dump(const uint8_t *data, size_t len) {
  std::string res;

  for (size_t i = 0; i < len; ++i) {
    auto c = data[i];

    if (c >= 0x20 && c < 0x7f) {
      res += c;
    } else {
      res += '.';
    }
  }

  return res;
}

char *get_exec_path(int argc, char **const argv, const char *cwd) {
  if (argc == 0 || cwd == nullptr) {
    return nullptr;
  }

  auto argv0 = argv[0];
  auto len = strlen(argv0);

  char *path;

  if (argv0[0] == '/') {
    path = static_cast<char *>(malloc(len + 1));
    if (path == nullptr) {
      return nullptr;
    }
    memcpy(path, argv0, len + 1);
  } else {
    auto cwdlen = strlen(cwd);
    path = static_cast<char *>(malloc(len + 1 + cwdlen + 1));
    if (path == nullptr) {
      return nullptr;
    }
    memcpy(path, cwd, cwdlen);
    path[cwdlen] = '/';
    memcpy(path + cwdlen + 1, argv0, len + 1);
  }

  return path;
}

bool check_path(const std::string &path) {
  // We don't like '\' in path.
  return !path.empty() && path[0] == '/' &&
         path.find('\\') == std::string::npos &&
         path.find("/../") == std::string::npos &&
         path.find("/./") == std::string::npos &&
         !util::ends_with(path, "/..") && !util::ends_with(path, "/.");
}

int64_t to_time64(const timeval &tv) {
  return tv.tv_sec * 1000000 + tv.tv_usec;
}

bool check_h2_is_selected(const unsigned char *proto, size_t len) {
  return streq_l(NGHTTP2_PROTO_VERSION_ID, proto, len) ||
         streq_l(NGHTTP2_H2_16, proto, len) ||
         streq_l(NGHTTP2_H2_14, proto, len);
}

namespace {
bool select_proto(const unsigned char **out, unsigned char *outlen,
                  const unsigned char *in, unsigned int inlen, const char *key,
                  unsigned int keylen) {
  for (auto p = in, end = in + inlen; p + keylen <= end; p += *p + 1) {
    if (std::equal(key, key + keylen, p)) {
      *out = p + 1;
      *outlen = *p;
      return true;
    }
  }
  return false;
}
} // namespace

bool select_h2(const unsigned char **out, unsigned char *outlen,
               const unsigned char *in, unsigned int inlen) {
  return select_proto(out, outlen, in, inlen, NGHTTP2_PROTO_ALPN,
                      str_size(NGHTTP2_PROTO_ALPN)) ||
         select_proto(out, outlen, in, inlen, NGHTTP2_H2_16_ALPN,
                      str_size(NGHTTP2_H2_16_ALPN)) ||
         select_proto(out, outlen, in, inlen, NGHTTP2_H2_14_ALPN,
                      str_size(NGHTTP2_H2_14_ALPN));
}

bool select_protocol(const unsigned char **out, unsigned char *outlen,
                     const unsigned char *in, unsigned int inlen,
                     std::vector<std::string> proto_list) {
  for (const auto &proto : proto_list) {
    if (select_proto(out, outlen, in, inlen, proto.c_str(),
                     static_cast<unsigned int>(proto.size()))) {
      return true;
    }
  }

  return false;
}

std::vector<unsigned char> get_default_alpn() {
  auto res = std::vector<unsigned char>(str_size(NGHTTP2_PROTO_ALPN) +
                                        str_size(NGHTTP2_H2_16_ALPN) +
                                        str_size(NGHTTP2_H2_14_ALPN));
  auto p = std::begin(res);

  p = std::copy_n(NGHTTP2_PROTO_ALPN, str_size(NGHTTP2_PROTO_ALPN), p);
  p = std::copy_n(NGHTTP2_H2_16_ALPN, str_size(NGHTTP2_H2_16_ALPN), p);
  p = std::copy_n(NGHTTP2_H2_14_ALPN, str_size(NGHTTP2_H2_14_ALPN), p);

  return res;
}

std::vector<Range<const char *>> split_config_str_list(const char *s,
                                                       char delim) {
  size_t len = 1;
  auto last = s + strlen(s);
  for (const char *first = s, *d = nullptr;
       (d = std::find(first, last, delim)) != last; ++len, first = d + 1)
    ;

  auto list = std::vector<Range<const char *>>(len);

  len = 0;
  for (auto first = s;; ++len) {
    auto stop = std::find(first, last, delim);
    list[len] = {first, stop};
    if (stop == last) {
      break;
    }
    first = stop + 1;
  }
  return list;
}

std::vector<std::string> parse_config_str_list(const char *s, char delim) {
  auto ranges = split_config_str_list(s, delim);
  auto res = std::vector<std::string>();
  res.reserve(ranges.size());
  for (const auto &range : ranges) {
    res.emplace_back(range.first, range.second);
  }
  return res;
}

int make_socket_closeonexec(int fd) {
  int flags;
  int rv;
  while ((flags = fcntl(fd, F_GETFD)) == -1 && errno == EINTR)
    ;
  while ((rv = fcntl(fd, F_SETFD, flags | FD_CLOEXEC)) == -1 && errno == EINTR)
    ;
  return rv;
}

int make_socket_nonblocking(int fd) {
  int flags;
  int rv;
  while ((flags = fcntl(fd, F_GETFL, 0)) == -1 && errno == EINTR)
    ;
  while ((rv = fcntl(fd, F_SETFL, flags | O_NONBLOCK)) == -1 && errno == EINTR)
    ;
  return rv;
}

int make_socket_nodelay(int fd) {
  int val = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&val),
                 sizeof(val)) == -1) {
    return -1;
  }
  return 0;
}

int create_nonblock_socket(int family) {
#ifdef SOCK_NONBLOCK
  auto fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

  if (fd == -1) {
    return -1;
  }
#else  // !SOCK_NONBLOCK
  auto fd = socket(family, SOCK_STREAM, 0);

  if (fd == -1) {
    return -1;
  }

  make_socket_nonblocking(fd);
  make_socket_closeonexec(fd);
#endif // !SOCK_NONBLOCK

  if (family == AF_INET || family == AF_INET6) {
    make_socket_nodelay(fd);
  }

  return fd;
}

bool check_socket_connected(int fd) {
  int error;
  socklen_t len = sizeof(error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
    if (error != 0) {
      return false;
    }
  }
  return true;
}

bool ipv6_numeric_addr(const char *host) {
  uint8_t dst[16];
  return inet_pton(AF_INET6, host, dst) == 1;
}

namespace {
std::pair<int64_t, size_t> parse_uint_digits(const void *ss, size_t len) {
  const uint8_t *s = static_cast<const uint8_t *>(ss);
  int64_t n = 0;
  size_t i;
  if (len == 0) {
    return {-1, 0};
  }
  constexpr int64_t max = std::numeric_limits<int64_t>::max();
  for (i = 0; i < len; ++i) {
    if ('0' <= s[i] && s[i] <= '9') {
      if (n > max / 10) {
        return {-1, 0};
      }
      n *= 10;
      if (n > max - (s[i] - '0')) {
        return {-1, 0};
      }
      n += s[i] - '0';
      continue;
    }
    break;
  }
  if (i == 0) {
    return {-1, 0};
  }
  return {n, i};
}
} // namespace

int64_t parse_uint_with_unit(const char *s) {
  int64_t n;
  size_t i;
  auto len = strlen(s);
  std::tie(n, i) = parse_uint_digits(s, len);
  if (n == -1) {
    return -1;
  }
  if (i == len) {
    return n;
  }
  if (i + 1 != len) {
    return -1;
  }
  int mul = 1;
  switch (s[i]) {
  case 'K':
  case 'k':
    mul = 1 << 10;
    break;
  case 'M':
  case 'm':
    mul = 1 << 20;
    break;
  case 'G':
  case 'g':
    mul = 1 << 30;
    break;
  default:
    return -1;
  }
  constexpr int64_t max = std::numeric_limits<int64_t>::max();
  if (n > max / mul) {
    return -1;
  }
  return n * mul;
}

int64_t parse_uint(const char *s) {
  return parse_uint(reinterpret_cast<const uint8_t *>(s), strlen(s));
}

int64_t parse_uint(const std::string &s) {
  return parse_uint(reinterpret_cast<const uint8_t *>(s.c_str()), s.size());
}

int64_t parse_uint(const uint8_t *s, size_t len) {
  int64_t n;
  size_t i;
  std::tie(n, i) = parse_uint_digits(s, len);
  if (n == -1 || i != len) {
    return -1;
  }
  return n;
}

double parse_duration_with_unit(const char *s) {
  constexpr auto max = std::numeric_limits<int64_t>::max();
  int64_t n;
  size_t i;
  auto len = strlen(s);
  std::tie(n, i) = parse_uint_digits(s, len);
  if (n == -1) {
    goto fail;
  }
  if (i == len) {
    return static_cast<double>(n);
  }
  switch (s[i]) {
  case 'S':
  case 's':
    // seconds
    if (i + 1 != len) {
      goto fail;
    }
    return static_cast<double>(n);
  case 'M':
  case 'm':
    if (i + 1 == len) {
      // minutes
      if (n > max / 60) {
        goto fail;
      }
      return static_cast<double>(n) * 60;
    }

    if (i + 2 != len || (s[i + 1] != 's' && s[i + 1] != 'S')) {
      goto fail;
    }
    // milliseconds
    return static_cast<double>(n) / 1000.;
  case 'H':
  case 'h':
    // hours
    if (i + 1 != len) {
      goto fail;
    }
    if (n > max / 3600) {
      goto fail;
    }
    return static_cast<double>(n) * 3600;
  }
fail:
  return std::numeric_limits<double>::infinity();
}

std::string duration_str(double t) {
  if (t == 0.) {
    return "0";
  }
  auto frac = static_cast<int64_t>(t * 1000) % 1000;
  if (frac > 0) {
    return utos(static_cast<int64_t>(t * 1000)) + "ms";
  }
  auto v = static_cast<int64_t>(t);
  if (v % 60) {
    return utos(v) + "s";
  }
  v /= 60;
  if (v % 60) {
    return utos(v) + "m";
  }
  v /= 60;
  return utos(v) + "h";
}

std::string format_duration(const std::chrono::microseconds &u) {
  const char *unit = "us";
  int d = 0;
  auto t = u.count();
  if (t >= 1000000) {
    d = 1000000;
    unit = "s";
  } else if (t >= 1000) {
    d = 1000;
    unit = "ms";
  } else {
    return utos(t) + unit;
  }
  return dtos(static_cast<double>(t) / d) + unit;
}

std::string format_duration(double t) {
  const char *unit = "us";
  if (t >= 1.) {
    unit = "s";
  } else if (t >= 0.001) {
    t *= 1000.;
    unit = "ms";
  } else {
    t *= 1000000.;
    return utos(static_cast<int64_t>(t)) + unit;
  }
  return dtos(t) + unit;
}

std::string dtos(double n) {
  auto f = utos(static_cast<int64_t>(round(100. * n)) % 100);
  return utos(static_cast<int64_t>(n)) + "." + (f.size() == 1 ? "0" : "") + f;
}

std::string make_hostport(const char *host, uint16_t port) {
  auto ipv6 = ipv6_numeric_addr(host);
  std::string hostport;

  if (ipv6) {
    hostport += '[';
  }

  hostport += host;

  if (ipv6) {
    hostport += ']';
  }

  if (port != 80 && port != 443) {
    hostport += ':';
    hostport += utos(port);
  }

  return hostport;
}

namespace {
void hexdump8(FILE *out, const uint8_t *first, const uint8_t *last) {
  auto stop = std::min(first + 8, last);
  for (auto k = first; k != stop; ++k) {
    fprintf(out, "%02x ", *k);
  }
  // each byte needs 3 spaces (2 hex value and space)
  for (; stop != first + 8; ++stop) {
    fputs("   ", out);
  }
  // we have extra space after 8 bytes
  fputc(' ', out);
}
} // namespace

void hexdump(FILE *out, const uint8_t *src, size_t len) {
  if (len == 0) {
    return;
  }
  size_t buflen = 0;
  auto repeated = false;
  std::array<uint8_t, 16> buf{};
  auto end = src + len;
  auto i = src;
  for (;;) {
    auto nextlen =
        std::min(static_cast<size_t>(16), static_cast<size_t>(end - i));
    if (nextlen == buflen &&
        std::equal(std::begin(buf), std::begin(buf) + buflen, i)) {
      // as long as adjacent 16 bytes block are the same, we just
      // print single '*'.
      if (!repeated) {
        repeated = true;
        fputs("*\n", out);
      }
      i += nextlen;
      continue;
    }
    repeated = false;
    fprintf(out, "%08lx", static_cast<unsigned long>(i - src));
    if (i == end) {
      fputc('\n', out);
      break;
    }
    fputs("  ", out);
    hexdump8(out, i, end);
    hexdump8(out, i + 8, std::max(i + 8, end));
    fputc('|', out);
    auto stop = std::min(i + 16, end);
    buflen = stop - i;
    auto p = buf.data();
    for (; i != stop; ++i) {
      *p++ = *i;
      if (0x20 <= *i && *i <= 0x7e) {
        fputc(*i, out);
      } else {
        fputc('.', out);
      }
    }
    fputs("|\n", out);
  }
}

void put_uint16be(uint8_t *buf, uint16_t n) {
  uint16_t x = htons(n);
  memcpy(buf, &x, sizeof(uint16_t));
}

void put_uint32be(uint8_t *buf, uint32_t n) {
  uint32_t x = htonl(n);
  memcpy(buf, &x, sizeof(uint32_t));
}

uint16_t get_uint16(const uint8_t *data) {
  uint16_t n;
  memcpy(&n, data, sizeof(uint16_t));
  return ntohs(n);
}

uint32_t get_uint32(const uint8_t *data) {
  uint32_t n;
  memcpy(&n, data, sizeof(uint32_t));
  return ntohl(n);
}

uint64_t get_uint64(const uint8_t *data) {
  uint64_t n = 0;
  n += static_cast<uint64_t>(data[0]) << 56;
  n += static_cast<uint64_t>(data[1]) << 48;
  n += static_cast<uint64_t>(data[2]) << 40;
  n += static_cast<uint64_t>(data[3]) << 32;
  n += static_cast<uint64_t>(data[4]) << 24;
  n += data[5] << 16;
  n += data[6] << 8;
  n += data[7];
  return n;
}

int read_mime_types(std::map<std::string, std::string> &res,
                    const char *filename) {
  std::ifstream infile(filename);
  if (!infile) {
    return -1;
  }

  auto delim_pred = [](char c) { return c == ' ' || c == '\t'; };

  std::string line;
  while (std::getline(infile, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    auto type_end = std::find_if(std::begin(line), std::end(line), delim_pred);
    if (type_end == std::begin(line)) {
      continue;
    }

    auto ext_end = type_end;
    for (;;) {
      auto ext_start = std::find_if_not(ext_end, std::end(line), delim_pred);
      if (ext_start == std::end(line)) {
        break;
      }
      ext_end = std::find_if(ext_start, std::end(line), delim_pred);
#ifdef HAVE_STD_MAP_EMPLACE
      res.emplace(std::string(ext_start, ext_end),
                  std::string(std::begin(line), type_end));
#else  // !HAVE_STD_MAP_EMPLACE
      res.insert(std::make_pair(std::string(ext_start, ext_end),
                                std::string(std::begin(line), type_end)));
#endif // !HAVE_STD_MAP_EMPLACE
    }
  }

  return 0;
}

} // namespace util

} // namespace nghttp2
