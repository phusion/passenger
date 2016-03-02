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
#ifndef HTTP2_H
#define HTTP2_H

#include "nghttp2_config.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>

#include <nghttp2/nghttp2.h>

#include "http-parser/http_parser.h"

#include "util.h"
#include "memchunk.h"

namespace nghttp2 {

struct Header {
  Header(std::string name, std::string value, bool no_index = false,
         int16_t token = -1)
      : name(std::move(name)), value(std::move(value)), token(token),
        no_index(no_index) {}

  Header() : token(-1), no_index(false) {}

  bool operator==(const Header &other) const {
    return name == other.name && value == other.value;
  }

  bool operator<(const Header &rhs) const {
    return name < rhs.name || (name == rhs.name && value < rhs.value);
  }

  std::string name;
  std::string value;
  int16_t token;
  bool no_index;
};

using Headers = std::vector<Header>;

namespace http2 {

std::string get_status_string(unsigned int status_code);

// Returns string version of |status_code|.  This function can handle
// only predefined status code.  Otherwise, returns nullptr.
const char *stringify_status(unsigned int status_code);

void capitalize(DefaultMemchunks *buf, const std::string &s);

// Returns true if |value| is LWS
bool lws(const char *value);

// Copies the |field| component value from |u| and |url| to the
// |dest|. If |u| does not have |field|, then this function does
// nothing.
void copy_url_component(std::string &dest, const http_parser_url *u, int field,
                        const char *url);

Headers::value_type to_header(const uint8_t *name, size_t namelen,
                              const uint8_t *value, size_t valuelen,
                              bool no_index, int16_t token);

// Add name/value pairs to |nva|.  If |no_index| is true, this
// name/value pair won't be indexed when it is forwarded to the next
// hop.  This function strips white spaces around |value|.
void add_header(Headers &nva, const uint8_t *name, size_t namelen,
                const uint8_t *value, size_t valuelen, bool no_index,
                int16_t token);

// Returns pointer to the entry in |nva| which has name |name|.  If
// more than one entries which have the name |name|, last occurrence
// in |nva| is returned.  If no such entry exist, returns nullptr.
const Headers::value_type *get_header(const Headers &nva, const char *name);

// Returns nv->second if nv is not nullptr. Otherwise, returns "".
std::string value_to_str(const Headers::value_type *nv);

// Returns true if the value of |nv| is not empty.
bool non_empty_value(const Headers::value_type *nv);

// Creates nghttp2_nv using |name| and |value| and returns it. The
// returned value only references the data pointer to name.c_str() and
// value.c_str().  If |no_index| is true, nghttp2_nv flags member has
// NGHTTP2_NV_FLAG_NO_INDEX flag set.
nghttp2_nv make_nv(const std::string &name, const std::string &value,
                   bool no_index = false);

nghttp2_nv make_nv_nocopy(const std::string &name, const std::string &value,
                          bool no_index = false);

// Create nghttp2_nv from string literal |name| and |value|.
template <size_t N, size_t M>
constexpr nghttp2_nv make_nv_ll(const char(&name)[N], const char(&value)[M]) {
  return {(uint8_t *)name, (uint8_t *)value, N - 1, M - 1,
          NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE};
}

// Create nghttp2_nv from string literal |name| and c-string |value|.
template <size_t N>
nghttp2_nv make_nv_lc(const char(&name)[N], const char *value) {
  return {(uint8_t *)name, (uint8_t *)value, N - 1, strlen(value),
          NGHTTP2_NV_FLAG_NO_COPY_NAME};
}

template <size_t N>
nghttp2_nv make_nv_lc_nocopy(const char(&name)[N], const char *value) {
  return {(uint8_t *)name, (uint8_t *)value, N - 1, strlen(value),
          NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE};
}

// Create nghttp2_nv from string literal |name| and std::string
// |value|.
template <size_t N>
nghttp2_nv make_nv_ls(const char(&name)[N], const std::string &value) {
  return {(uint8_t *)name, (uint8_t *)value.c_str(), N - 1, value.size(),
          NGHTTP2_NV_FLAG_NO_COPY_NAME};
}

template <size_t N>
nghttp2_nv make_nv_ls_nocopy(const char(&name)[N], const std::string &value) {
  return {(uint8_t *)name, (uint8_t *)value.c_str(), N - 1, value.size(),
          NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE};
}

template <size_t N>
nghttp2_nv make_nv_ls_nocopy(const char(&name)[N], const StringRef &value) {
  return {(uint8_t *)name, (uint8_t *)value.c_str(), N - 1, value.size(),
          NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE};
}

// Appends headers in |headers| to |nv|.  |headers| must be indexed
// before this call (its element's token field is assigned).  Certain
// headers, including disallowed headers in HTTP/2 spec and headers
// which require special handling (i.e. via), are not copied.
void copy_headers_to_nva(std::vector<nghttp2_nv> &nva, const Headers &headers);

// Just like copy_headers_to_nva(), but this adds
// NGHTTP2_NV_FLAG_NO_COPY_NAME and NGHTTP2_NV_FLAG_NO_COPY_VALUE.
void copy_headers_to_nva_nocopy(std::vector<nghttp2_nv> &nva,
                                const Headers &headers);

// Appends HTTP/1.1 style header lines to |buf| from headers in
// |headers|.  |headers| must be indexed before this call (its
// element's token field is assigned).  Certain headers, which
// requires special handling (i.e. via and cookie), are not appended.
void build_http1_headers_from_headers(DefaultMemchunks *buf,
                                      const Headers &headers);

// Return positive window_size_increment if WINDOW_UPDATE should be
// sent for the stream |stream_id|. If |stream_id| == 0, this function
// determines the necessity of the WINDOW_UPDATE for a connection.
//
// If the function determines WINDOW_UPDATE is not necessary at the
// moment, it returns -1.
int32_t determine_window_update_transmission(nghttp2_session *session,
                                             int32_t stream_id);

// Dumps name/value pairs in |nv| to |out|. The |nv| must be
// terminated by nullptr.
void dump_nv(FILE *out, const char **nv);

// Dumps name/value pairs in |nva| to |out|.
void dump_nv(FILE *out, const nghttp2_nv *nva, size_t nvlen);

// Dumps name/value pairs in |nva| to |out|.
void dump_nv(FILE *out, const Headers &nva);

// Rewrites redirection URI which usually appears in location header
// field. The |uri| is the URI in the location header field. The |u|
// stores the result of parsed |uri|. The |request_authority| is the
// host or :authority header field value in the request. The
// |upstream_scheme| is either "https" or "http" in the upstream
// interface.  Rewrite is done only if location header field value
// contains |match_host| as host excluding port.  The |match_host| and
// |request_authority| could be different.  If |request_authority| is
// empty, strip authority.
//
// This function returns the new rewritten URI on success. If the
// location URI is not subject to the rewrite, this function returns
// emtpy string.
std::string rewrite_location_uri(const std::string &uri,
                                 const http_parser_url &u,
                                 const std::string &match_host,
                                 const std::string &request_authority,
                                 const std::string &upstream_scheme);

// Checks the header name/value pair using nghttp2_check_header_name()
// and nghttp2_check_header_value(). If both function returns nonzero,
// this function returns nonzero.
int check_nv(const uint8_t *name, size_t namelen, const uint8_t *value,
             size_t valuelen);

// Returns parsed HTTP status code.  Returns -1 on failure.
int parse_http_status_code(const std::string &src);

// Header fields to be indexed, except HD_MAXIDX which is convenient
// member to get maximum value.
//
// generated by genheaderfunc.py
enum {
  HD__AUTHORITY,
  HD__HOST,
  HD__METHOD,
  HD__PATH,
  HD__SCHEME,
  HD__STATUS,
  HD_ACCEPT_ENCODING,
  HD_ACCEPT_LANGUAGE,
  HD_ALT_SVC,
  HD_CACHE_CONTROL,
  HD_CONNECTION,
  HD_CONTENT_LENGTH,
  HD_CONTENT_TYPE,
  HD_COOKIE,
  HD_DATE,
  HD_EXPECT,
  HD_FORWARDED,
  HD_HOST,
  HD_HTTP2_SETTINGS,
  HD_IF_MODIFIED_SINCE,
  HD_KEEP_ALIVE,
  HD_LINK,
  HD_LOCATION,
  HD_PROXY_CONNECTION,
  HD_SERVER,
  HD_TE,
  HD_TRAILER,
  HD_TRANSFER_ENCODING,
  HD_UPGRADE,
  HD_USER_AGENT,
  HD_VIA,
  HD_X_FORWARDED_FOR,
  HD_X_FORWARDED_PROTO,
  HD_MAXIDX,
};

using HeaderIndex = std::array<int16_t, HD_MAXIDX>;

// Looks up header token for header name |name| of length |namelen|.
// Only headers we are interested in are tokenized.  If header name
// cannot be tokenized, returns -1.
int lookup_token(const uint8_t *name, size_t namelen);
int lookup_token(const std::string &name);

// Initializes |hdidx|, header index.  The |hdidx| must point to the
// array containing at least HD_MAXIDX elements.
void init_hdidx(HeaderIndex &hdidx);
// Indexes header |token| using index |idx|.
void index_header(HeaderIndex &hdidx, int16_t token, size_t idx);

// Returns true if HTTP/2 request pseudo header |token| is not indexed
// yet and not -1.
bool check_http2_request_pseudo_header(const HeaderIndex &hdidx, int16_t token);

// Returns true if HTTP/2 response pseudo header |token| is not
// indexed yet and not -1.
bool check_http2_response_pseudo_header(const HeaderIndex &hdidx,
                                        int16_t token);

// Returns true if header field denoted by |token| is allowed for
// HTTP/2.
bool http2_header_allowed(int16_t token);

// Returns true that |hdidx| contains mandatory HTTP/2 request
// headers.
bool http2_mandatory_request_headers_presence(const HeaderIndex &hdidx);

// Returns header denoted by |token| using index |hdidx|.
const Headers::value_type *get_header(const HeaderIndex &hdidx, int16_t token,
                                      const Headers &nva);

Headers::value_type *get_header(const HeaderIndex &hdidx, int16_t token,
                                Headers &nva);

struct LinkHeader {
  // The region of URI is [uri.first, uri.second).
  std::pair<const char *, const char *> uri;
};

// Returns next URI-reference in Link header field value |src| of
// length |len|.  If no URI-reference found after searching all input,
// returned uri field is empty.  This imply that empty URI-reference
// is ignored during parsing.
std::vector<LinkHeader> parse_link_header(const char *src, size_t len);

// Constructs path by combining base path |base_path| of length
// |base_pathlen| with another path |rel_path| of length
// |rel_pathlen|.  The base path and another path can have optional
// query component.  This function assumes |base_path| is normalized.
// In other words, it does not contain ".." or "."  path components
// and starts with "/" if it is not empty.
std::string path_join(const char *base_path, size_t base_pathlen,
                      const char *base_query, size_t base_querylen,
                      const char *rel_path, size_t rel_pathlen,
                      const char *rel_query, size_t rel_querylen);

// true if response has body, taking into account the request method
// and status code.
bool expect_response_body(const std::string &method, int status_code);
bool expect_response_body(int method_token, int status_code);

// true if response has body, taking into account status code only.
bool expect_response_body(int status_code);

// Looks up method token for method name |name| of length |namelen|.
// Only methods defined in http-parser/http-parser.h (http_method) are
// tokenized.  If method name cannot be tokenized, returns -1.
int lookup_method_token(const uint8_t *name, size_t namelen);
int lookup_method_token(const std::string &name);

const char *to_method_string(int method_token);

template <typename InputIt>
std::string normalize_path(InputIt first, InputIt last) {
  // First, decode %XX for unreserved characters, then do
  // http2::join_path
  std::string result;
  // We won't find %XX if length is less than 3.
  if (last - first < 3) {
    result.assign(first, last);
  } else {
    for (; first < last - 2;) {
      if (*first == '%') {
        if (util::is_hex_digit(*(first + 1)) &&
            util::is_hex_digit(*(first + 2))) {
          auto c = (util::hex_to_uint(*(first + 1)) << 4) +
                   util::hex_to_uint(*(first + 2));
          if (util::in_rfc3986_unreserved_chars(c)) {
            result += c;
            first += 3;
            continue;
          }
          result += '%';
          result += util::upcase(*(first + 1));
          result += util::upcase(*(first + 2));
          first += 3;
          continue;
        }
      }
      result += *first++;
    }
    result.append(first, last);
  }
  return path_join(nullptr, 0, nullptr, 0, result.c_str(), result.size(),
                   nullptr, 0);
}

template <typename InputIt>
std::string rewrite_clean_path(InputIt first, InputIt last) {
  if (first == last || *first != '/') {
    return std::string(first, last);
  }
  // probably, not necessary most of the case, but just in case.
  auto fragment = std::find(first, last, '#');
  auto query = std::find(first, fragment, '?');
  auto path = normalize_path(first, query);
  if (query != fragment) {
    path.append(query, fragment);
  }
  return path;
}

// Stores path component of |uri| in *base.  Its extracted length is
// stored in *baselen.  The extracted path does not include query
// component.  This function returns 0 if it succeeds, or -1.
int get_pure_path_component(const char **base, size_t *baselen,
                            const std::string &uri);

// Deduces scheme, authority and path from given |uri| of length
// |len|, and stores them in |scheme|, |authority|, and |path|
// respectively.  If |uri| is relative path, path resolution is taken
// palce using path given in |base| of length |baselen|.  This
// function returns 0 if it succeeds, or -1.
int construct_push_component(std::string &scheme, std::string &authority,
                             std::string &path, const char *base,
                             size_t baselen, const char *uri, size_t len);

} // namespace http2

} // namespace nghttp2

#endif // HTTP2_H
