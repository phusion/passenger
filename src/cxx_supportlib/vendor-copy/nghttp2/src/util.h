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
#ifndef UTIL_H
#define UTIL_H

#include "nghttp2_config.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H
#include <getopt.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif // HAVE_NETDB_H

#include <cmath>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <memory>
#include <chrono>
#include <map>
#include <random>

#include "http-parser/http_parser.h"

namespace nghttp2 {

// The additional HTTP/2 protocol ALPN protocol identifier we also
// supports for our applications to make smooth migration into final
// h2 ALPN ID.
constexpr const char NGHTTP2_H2_16_ALPN[] = "\x5h2-16";
constexpr const char NGHTTP2_H2_16[] = "h2-16";

constexpr const char NGHTTP2_H2_14_ALPN[] = "\x5h2-14";
constexpr const char NGHTTP2_H2_14[] = "h2-14";

constexpr const char NGHTTP2_H1_1_ALPN[] = "\x8http/1.1";
constexpr const char NGHTTP2_H1_1[] = "http/1.1";

namespace util {

inline bool is_alpha(const char c) {
  return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z');
}

inline bool is_digit(const char c) { return '0' <= c && c <= '9'; }

inline bool is_hex_digit(const char c) {
  return is_digit(c) || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

bool in_rfc3986_unreserved_chars(const char c);

bool in_rfc3986_sub_delims(const char c);

// Returns true if |c| is in token (HTTP-p1, Section 3.2.6)
bool in_token(char c);

bool in_attr_char(char c);

// Returns integer corresponding to hex notation |c|.  It is undefined
// if is_hex_digit(c) is false.
uint32_t hex_to_uint(char c);

std::string percent_encode(const unsigned char *target, size_t len);

std::string percent_encode(const std::string &target);

// percent-encode path component of URI |s|.
std::string percent_encode_path(const std::string &s);

template <typename InputIt>
std::string percent_decode(InputIt first, InputIt last) {
  std::string result;
  for (; first != last; ++first) {
    if (*first == '%') {
      if (first + 1 != last && first + 2 != last &&
          is_hex_digit(*(first + 1)) && is_hex_digit(*(first + 2))) {
        result += (hex_to_uint(*(first + 1)) << 4) + hex_to_uint(*(first + 2));
        first += 2;
        continue;
      }
      result += *first;
      continue;
    }
    result += *first;
  }
  return result;
}

// Percent encode |target| if character is not in token or '%'.
std::string percent_encode_token(const std::string &target);

// Returns quotedString version of |target|.  Currently, this function
// just replace '"' with '\"'.
std::string quote_string(const std::string &target);

std::string format_hex(const unsigned char *s, size_t len);

template <size_t N> std::string format_hex(const unsigned char(&s)[N]) {
  return format_hex(s, N);
}

template <size_t N> std::string format_hex(const std::array<uint8_t, N> &s) {
  return format_hex(s.data(), s.size());
}

std::string http_date(time_t t);

// Returns given time |t| from epoch in Common Log format (e.g.,
// 03/Jul/2014:00:19:38 +0900)
std::string common_log_date(time_t t);

// Returns given millisecond |ms| from epoch in ISO 8601 format (e.g.,
// 2014-11-15T12:58:24.741Z)
std::string iso8601_date(int64_t ms);

time_t parse_http_date(const std::string &s);

char upcase(char c);

inline char lowcase(char c) {
  static unsigned char tbl[] = {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,
      15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
      30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
      45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
      60,  61,  62,  63,  64,  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
      'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y',
      'z', 91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104,
      105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
      120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
      135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
      150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
      165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
      180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
      195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
      210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
      225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
      240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
      255,
  };
  return tbl[static_cast<unsigned char>(c)];
}

template <typename InputIterator1, typename InputIterator2>
bool starts_with(InputIterator1 first1, InputIterator1 last1,
                 InputIterator2 first2, InputIterator2 last2) {
  if (last1 - first1 < last2 - first2) {
    return false;
  }
  return std::equal(first2, last2, first1);
}

inline bool starts_with(const std::string &a, const std::string &b) {
  return starts_with(std::begin(a), std::end(a), std::begin(b), std::end(b));
}

inline bool starts_with(const char *a, const char *b) {
  return starts_with(a, a + strlen(a), b, b + strlen(b));
}

struct CaseCmp {
  bool operator()(char lhs, char rhs) const {
    return lowcase(lhs) == lowcase(rhs);
  }
};

template <typename InputIterator1, typename InputIterator2>
bool istarts_with(InputIterator1 first1, InputIterator1 last1,
                  InputIterator2 first2, InputIterator2 last2) {
  if (last1 - first1 < last2 - first2) {
    return false;
  }
  return std::equal(first2, last2, first1, CaseCmp());
}

inline bool istarts_with(const std::string &a, const std::string &b) {
  return istarts_with(std::begin(a), std::end(a), std::begin(b), std::end(b));
}

template <typename InputIt>
bool istarts_with(InputIt a, size_t an, const char *b) {
  return istarts_with(a, a + an, b, b + strlen(b));
}

bool istarts_with(const char *a, const char *b);

template <size_t N>
bool istarts_with_l(const std::string &a, const char(&b)[N]) {
  return istarts_with(std::begin(a), std::end(a), b, b + N - 1);
}

template <typename InputIterator1, typename InputIterator2>
bool ends_with(InputIterator1 first1, InputIterator1 last1,
               InputIterator2 first2, InputIterator2 last2) {
  if (last1 - first1 < last2 - first2) {
    return false;
  }
  return std::equal(first2, last2, last1 - (last2 - first2));
}

inline bool ends_with(const std::string &a, const std::string &b) {
  return ends_with(std::begin(a), std::end(a), std::begin(b), std::end(b));
}

template <typename InputIterator1, typename InputIterator2>
bool iends_with(InputIterator1 first1, InputIterator1 last1,
                InputIterator2 first2, InputIterator2 last2) {
  if (last1 - first1 < last2 - first2) {
    return false;
  }
  return std::equal(first2, last2, last1 - (last2 - first2), CaseCmp());
}

inline bool iends_with(const std::string &a, const std::string &b) {
  return iends_with(std::begin(a), std::end(a), std::begin(b), std::end(b));
}

template <size_t N> bool iends_with_l(const std::string &a, const char(&b)[N]) {
  return iends_with(std::begin(a), std::end(a), b, b + N - 1);
}

int strcompare(const char *a, const uint8_t *b, size_t n);

template <typename InputIt> bool strieq(const char *a, InputIt b, size_t bn) {
  if (!a) {
    return false;
  }
  auto blast = b + bn;
  for (; *a && b != blast && lowcase(*a) == lowcase(*b); ++a, ++b)
    ;
  return !*a && b == blast;
}

template <typename InputIt1, typename InputIt2>
bool strieq(InputIt1 a, size_t alen, InputIt2 b, size_t blen) {
  if (alen != blen) {
    return false;
  }
  return std::equal(a, a + alen, b, CaseCmp());
}

template <typename InputIt1, typename InputIt2>
bool strieq(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2) {
  if (std::distance(first1, last1) != std::distance(first2, last2)) {
    return false;
  }

  return std::equal(first1, last1, first2, CaseCmp());
}

inline bool strieq(const std::string &a, const std::string &b) {
  return strieq(std::begin(a), a.size(), std::begin(b), b.size());
}

bool strieq(const char *a, const char *b);

inline bool strieq(const char *a, const std::string &b) {
  return strieq(a, b.c_str(), b.size());
}

template <typename InputIt, size_t N>
bool strieq_l(const char(&a)[N], InputIt b, size_t blen) {
  return strieq(a, N - 1, b, blen);
}

template <size_t N> bool strieq_l(const char(&a)[N], const std::string &b) {
  return strieq(a, N - 1, std::begin(b), b.size());
}

template <typename InputIt> bool streq(const char *a, InputIt b, size_t bn) {
  if (!a) {
    return false;
  }
  auto blast = b + bn;
  for (; *a && b != blast && *a == *b; ++a, ++b)
    ;
  return !*a && b == blast;
}

template <typename InputIt1, typename InputIt2>
bool streq(InputIt1 a, size_t alen, InputIt2 b, size_t blen) {
  if (alen != blen) {
    return false;
  }
  return std::equal(a, a + alen, b);
}

inline bool streq(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  return streq(a, strlen(a), b, strlen(b));
}

template <typename InputIt, size_t N>
bool streq_l(const char(&a)[N], InputIt b, size_t blen) {
  return streq(a, N - 1, b, blen);
}

template <size_t N> bool streq_l(const char(&a)[N], const std::string &b) {
  return streq(a, N - 1, std::begin(b), b.size());
}

bool strifind(const char *a, const char *b);

template <typename InputIt> void inp_strlower(InputIt first, InputIt last) {
  std::transform(first, last, first, lowcase);
}

// Lowercase |s| in place.
inline void inp_strlower(std::string &s) {
  inp_strlower(std::begin(s), std::end(s));
}

// Returns string representation of |n| with 2 fractional digits.
std::string dtos(double n);

template <typename T> std::string utos(T n) {
  std::string res;
  if (n == 0) {
    res = "0";
    return res;
  }
  int i = 0;
  T t = n;
  for (; t; t /= 10, ++i)
    ;
  res.resize(i);
  --i;
  for (; n; --i, n /= 10) {
    res[i] = (n % 10) + '0';
  }
  return res;
}

template <typename T> std::string utos_unit(T n) {
  char u = 0;
  if (n >= (1 << 30)) {
    u = 'G';
    n /= (1 << 30);
  } else if (n >= (1 << 20)) {
    u = 'M';
    n /= (1 << 20);
  } else if (n >= (1 << 10)) {
    u = 'K';
    n /= (1 << 10);
  }
  if (u == 0) {
    return utos(n);
  }
  return utos(n) + u;
}

// Like utos_unit(), but 2 digits fraction part is followed.
template <typename T> std::string utos_funit(T n) {
  char u = 0;
  int b = 0;
  if (n >= (1 << 30)) {
    u = 'G';
    b = 30;
  } else if (n >= (1 << 20)) {
    u = 'M';
    b = 20;
  } else if (n >= (1 << 10)) {
    u = 'K';
    b = 10;
  }
  if (b == 0) {
    return utos(n);
  }
  return dtos(static_cast<double>(n) / (1 << b)) + u;
}

extern const char UPPER_XDIGITS[];

template <typename T> std::string utox(T n) {
  std::string res;
  if (n == 0) {
    res = "0";
    return res;
  }
  int i = 0;
  T t = n;
  for (; t; t /= 16, ++i)
    ;
  res.resize(i);
  --i;
  for (; n; --i, n /= 16) {
    res[i] = UPPER_XDIGITS[(n & 0x0f)];
  }
  return res;
}

void to_token68(std::string &base64str);
void to_base64(std::string &token68str);

void show_candidates(const char *unkopt, option *options);

bool has_uri_field(const http_parser_url &u, http_parser_url_fields field);

bool fieldeq(const char *uri1, const http_parser_url &u1, const char *uri2,
             const http_parser_url &u2, http_parser_url_fields field);

bool fieldeq(const char *uri, const http_parser_url &u,
             http_parser_url_fields field, const char *t);

std::string get_uri_field(const char *uri, const http_parser_url &u,
                          http_parser_url_fields field);

uint16_t get_default_port(const char *uri, const http_parser_url &u);

bool porteq(const char *uri1, const http_parser_url &u1, const char *uri2,
            const http_parser_url &u2);

void write_uri_field(std::ostream &o, const char *uri, const http_parser_url &u,
                     http_parser_url_fields field);

bool numeric_host(const char *hostname);

bool numeric_host(const char *hostname, int family);

// Returns numeric address string of |addr|.  If getnameinfo() is
// failed, "unknown" is returned.
std::string numeric_name(const struct sockaddr *sa, socklen_t salen);

// Makes internal copy of stderr (and possibly stdout in the future),
// which is then used as pointer to /dev/stderr or /proc/self/fd/2
void store_original_fds();

// Restores the original stderr that was stored with copy_original_fds
// Used just before execv
void restore_original_fds();

// Closes |fd| which was returned by open_log_file (see below)
// and sets it to -1. In the case that |fd| points to stdout or
// stderr, or is -1, the descriptor is not closed (but still set to -1).
void close_log_file(int &fd);

// Opens |path| with O_APPEND enabled.  If file does not exist, it is
// created first.  This function returns file descriptor referring the
// opened file if it succeeds, or -1.
int open_log_file(const char *path);

// Returns ASCII dump of |data| of length |len|.  Only ASCII printable
// characters are preserved.  Other characters are replaced with ".".
std::string ascii_dump(const uint8_t *data, size_t len);

// Returns absolute path of executable path.  If argc == 0 or |cwd| is
// nullptr, this function returns nullptr.  If argv[0] starts with
// '/', this function returns argv[0].  Oterwise return cwd + "/" +
// argv[0].  If non-null is returned, it is NULL-terminated string and
// dynamically allocated by malloc.  The caller is responsible to free
// it.
char *get_exec_path(int argc, char **const argv, const char *cwd);

// Validates path so that it does not contain directory traversal
// vector.  Returns true if path is safe.  The |path| must start with
// "/" otherwise returns false.  This function should be called after
// percent-decode was performed.
bool check_path(const std::string &path);

// Returns the |tv| value as 64 bit integer using a microsecond as an
// unit.
int64_t to_time64(const timeval &tv);

// Returns true if ALPN ID |proto| of length |len| is supported HTTP/2
// protocol identifier.
bool check_h2_is_selected(const unsigned char *alpn, size_t len);

// Selects h2 protocol ALPN ID if one of supported h2 versions are
// present in |in| of length inlen.  Returns true if h2 version is
// selected.
bool select_h2(const unsigned char **out, unsigned char *outlen,
               const unsigned char *in, unsigned int inlen);

// Selects protocol ALPN ID if one of identifiers contained in |protolist| is
// present in |in| of length inlen.  Returns true if identifier is
// selected.
bool select_protocol(const unsigned char **out, unsigned char *outlen,
                     const unsigned char *in, unsigned int inlen,
                     std::vector<std::string> proto_list);

// Returns default ALPN protocol list, which only contains supported
// HTTP/2 protocol identifier.
std::vector<unsigned char> get_default_alpn();

template <typename T> using Range = std::pair<T, T>;

// Parses delimited strings in |s| and returns the array of substring,
// delimited by |delim|.  The any white spaces around substring are
// treated as a part of substring.
std::vector<std::string> parse_config_str_list(const char *s, char delim = ',');

// Parses delimited strings in |s| and returns the array of pointers,
// each element points to the beginning and one beyond last of
// substring in |s|.  The delimiter is given by |delim|.  The any
// white spaces around substring are treated as a part of substring.
std::vector<Range<const char *>> split_config_str_list(const char *s,
                                                       char delim);

// Returns given time |tp| in Common Log format (e.g.,
// 03/Jul/2014:00:19:38 +0900)
// Expected type of |tp| is std::chrono::timepoint
template <typename T> std::string format_common_log(const T &tp) {
  auto t =
      std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
  return common_log_date(t.count());
}

// Returns given time |tp| in ISO 8601 format (e.g.,
// 2014-11-15T12:58:24.741Z)
// Expected type of |tp| is std::chrono::timepoint
template <typename T> std::string format_iso8601(const T &tp) {
  auto t = std::chrono::duration_cast<std::chrono::milliseconds>(
      tp.time_since_epoch());
  return iso8601_date(t.count());
}

// Returns given time |tp| in HTTP date format.
template <typename T> std::string format_http_date(const T &tp) {
  auto t =
      std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
  return http_date(t.count());
}

// Return the system precision of the template parameter |Clock| as
// a nanosecond value of type |Rep|
template <typename Clock, typename Rep> Rep clock_precision() {
  std::chrono::duration<Rep, std::nano> duration = typename Clock::duration(1);

  return duration.count();
}

int make_socket_closeonexec(int fd);
int make_socket_nonblocking(int fd);
int make_socket_nodelay(int fd);

int create_nonblock_socket(int family);

bool check_socket_connected(int fd);

// Returns true if |host| is IPv6 numeric address (e.g., ::1)
bool ipv6_numeric_addr(const char *host);

// Parses NULL terminated string |s| as unsigned integer and returns
// the parsed integer.  Additionally, if |s| ends with 'k', 'm', 'g'
// and its upper case characters, multiply the integer by 1024, 1024 *
// 1024 and 1024 * 1024 respectively.  If there is an error, returns
// -1.
int64_t parse_uint_with_unit(const char *s);

// Parses NULL terminated string |s| as unsigned integer and returns
// the parsed integer.  If there is an error, returns -1.
int64_t parse_uint(const char *s);
int64_t parse_uint(const uint8_t *s, size_t len);
int64_t parse_uint(const std::string &s);

// Parses NULL terminated string |s| as unsigned integer and returns
// the parsed integer casted to double.  If |s| ends with "s", the
// parsed value's unit is a second.  If |s| ends with "ms", the unit
// is millisecond.  Similarly, it also supports 'm' and 'h' for
// minutes and hours respectively.  If none of them are given, the
// unit is second.  This function returns
// std::numeric_limits<double>::infinity() if error occurs.
double parse_duration_with_unit(const char *s);

// Returns string representation of time duration |t|.  If t has
// fractional part (at least more than or equal to 1e-3), |t| is
// multiplied by 1000 and the unit "ms" is appended.  Otherwise, |t|
// is left as is and "s" is appended.
std::string duration_str(double t);

// Returns string representation of time duration |t|.  It appends
// unit after the formatting.  The available units are s, ms and us.
// The unit which is equal to or less than |t| is used and 2
// fractional digits follow.
std::string format_duration(const std::chrono::microseconds &u);

// Just like above, but this takes |t| as seconds.
std::string format_duration(double t);

// Creates "host:port" string using given |host| and |port|.  If
// |host| is numeric IPv6 address (e.g., ::1), it is enclosed by "["
// and "]".  If |port| is 80 or 443, port part is omitted.
std::string make_hostport(const char *host, uint16_t port);

// Dumps |src| of length |len| in the format similar to `hexdump -C`.
void hexdump(FILE *out, const uint8_t *src, size_t len);

// Copies 2 byte unsigned integer |n| in host byte order to |buf| in
// network byte order.
void put_uint16be(uint8_t *buf, uint16_t n);

// Copies 4 byte unsigned integer |n| in host byte order to |buf| in
// network byte order.
void put_uint32be(uint8_t *buf, uint32_t n);

// Retrieves 2 byte unsigned integer stored in |data| in network byte
// order and returns it in host byte order.
uint16_t get_uint16(const uint8_t *data);

// Retrieves 4 byte unsigned integer stored in |data| in network byte
// order and returns it in host byte order.
uint32_t get_uint32(const uint8_t *data);

// Retrieves 8 byte unsigned integer stored in |data| in network byte
// order and returns it in host byte order.
uint64_t get_uint64(const uint8_t *data);

// Reads mime types file (see /etc/mime.types), and stores extension
// -> MIME type map in |res|.  This function returns 0 if it succeeds,
// or -1.
int read_mime_types(std::map<std::string, std::string> &res,
                    const char *filename);

template <typename Generator>
std::string random_alpha_digit(Generator &gen, size_t len) {
  std::string res;
  res.reserve(len);
  std::uniform_int_distribution<> dis(0, 26 * 2 + 10 - 1);
  for (; len > 0; --len) {
    res += "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"[dis(
        gen)];
  }
  return res;
}

} // namespace util

} // namespace nghttp2

#endif // UTIL_H
