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
#include "http2.h"

#include "util.h"

namespace nghttp2 {

namespace http2 {

std::string get_status_string(unsigned int status_code) {
  switch (status_code) {
  case 100:
    return "100 Continue";
  case 101:
    return "101 Switching Protocols";
  case 200:
    return "200 OK";
  case 201:
    return "201 Created";
  case 202:
    return "202 Accepted";
  case 203:
    return "203 Non-Authoritative Information";
  case 204:
    return "204 No Content";
  case 205:
    return "205 Reset Content";
  case 206:
    return "206 Partial Content";
  case 300:
    return "300 Multiple Choices";
  case 301:
    return "301 Moved Permanently";
  case 302:
    return "302 Found";
  case 303:
    return "303 See Other";
  case 304:
    return "304 Not Modified";
  case 305:
    return "305 Use Proxy";
  // case 306: return "306 (Unused)";
  case 307:
    return "307 Temporary Redirect";
  case 308:
    return "308 Permanent Redirect";
  case 400:
    return "400 Bad Request";
  case 401:
    return "401 Unauthorized";
  case 402:
    return "402 Payment Required";
  case 403:
    return "403 Forbidden";
  case 404:
    return "404 Not Found";
  case 405:
    return "405 Method Not Allowed";
  case 406:
    return "406 Not Acceptable";
  case 407:
    return "407 Proxy Authentication Required";
  case 408:
    return "408 Request Timeout";
  case 409:
    return "409 Conflict";
  case 410:
    return "410 Gone";
  case 411:
    return "411 Length Required";
  case 412:
    return "412 Precondition Failed";
  case 413:
    return "413 Payload Too Large";
  case 414:
    return "414 URI Too Long";
  case 415:
    return "415 Unsupported Media Type";
  case 416:
    return "416 Requested Range Not Satisfiable";
  case 417:
    return "417 Expectation Failed";
  case 421:
    return "421 Misdirected Request";
  case 426:
    return "426 Upgrade Required";
  case 428:
    return "428 Precondition Required";
  case 429:
    return "429 Too Many Requests";
  case 431:
    return "431 Request Header Fields Too Large";
  case 451:
    return "451 Unavailable For Legal Reasons";
  case 500:
    return "500 Internal Server Error";
  case 501:
    return "501 Not Implemented";
  case 502:
    return "502 Bad Gateway";
  case 503:
    return "503 Service Unavailable";
  case 504:
    return "504 Gateway Timeout";
  case 505:
    return "505 HTTP Version Not Supported";
  case 511:
    return "511 Network Authentication Required";
  default:
    return util::utos(status_code);
  }
}

const char *stringify_status(unsigned int status_code) {
  switch (status_code) {
  case 100:
    return "100";
  case 101:
    return "101";
  case 200:
    return "200";
  case 201:
    return "201";
  case 202:
    return "202";
  case 203:
    return "203";
  case 204:
    return "204";
  case 205:
    return "205";
  case 206:
    return "206";
  case 300:
    return "300";
  case 301:
    return "301";
  case 302:
    return "302";
  case 303:
    return "303";
  case 304:
    return "304";
  case 305:
    return "305";
  // case 306: return "306";
  case 307:
    return "307";
  case 308:
    return "308";
  case 400:
    return "400";
  case 401:
    return "401";
  case 402:
    return "402";
  case 403:
    return "403";
  case 404:
    return "404";
  case 405:
    return "405";
  case 406:
    return "406";
  case 407:
    return "407";
  case 408:
    return "408";
  case 409:
    return "409";
  case 410:
    return "410";
  case 411:
    return "411";
  case 412:
    return "412";
  case 413:
    return "413";
  case 414:
    return "414";
  case 415:
    return "415";
  case 416:
    return "416";
  case 417:
    return "417";
  case 421:
    return "421";
  case 426:
    return "426";
  case 428:
    return "428";
  case 429:
    return "429";
  case 431:
    return "431";
  case 451:
    return "451";
  case 500:
    return "500";
  case 501:
    return "501";
  case 502:
    return "502";
  case 503:
    return "503";
  case 504:
    return "504";
  case 505:
    return "505";
  case 511:
    return "511";
  default:
    return nullptr;
  }
}

void capitalize(DefaultMemchunks *buf, const std::string &s) {
  buf->append(util::upcase(s[0]));
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i - 1] == '-') {
      buf->append(util::upcase(s[i]));
    } else {
      buf->append(s[i]);
    }
  }
}

bool lws(const char *value) {
  for (; *value; ++value) {
    switch (*value) {
    case '\t':
    case ' ':
      continue;
    default:
      return false;
    }
  }
  return true;
}

void copy_url_component(std::string &dest, const http_parser_url *u, int field,
                        const char *url) {
  if (u->field_set & (1 << field)) {
    dest.assign(url + u->field_data[field].off, u->field_data[field].len);
  }
}

Headers::value_type to_header(const uint8_t *name, size_t namelen,
                              const uint8_t *value, size_t valuelen,
                              bool no_index, int16_t token) {
  return Header(std::string(reinterpret_cast<const char *>(name), namelen),
                std::string(reinterpret_cast<const char *>(value), valuelen),
                no_index, token);
}

void add_header(Headers &nva, const uint8_t *name, size_t namelen,
                const uint8_t *value, size_t valuelen, bool no_index,
                int16_t token) {
  if (valuelen > 0) {
    size_t i, j;
    for (i = 0; i < valuelen && (value[i] == ' ' || value[i] == '\t'); ++i)
      ;
    for (j = valuelen - 1; j > i && (value[j] == ' ' || value[j] == '\t'); --j)
      ;
    value += i;
    valuelen -= i + (valuelen - j - 1);
  }
  nva.push_back(to_header(name, namelen, value, valuelen, no_index, token));
}

const Headers::value_type *get_header(const Headers &nva, const char *name) {
  const Headers::value_type *res = nullptr;
  for (auto &nv : nva) {
    if (nv.name == name) {
      res = &nv;
    }
  }
  return res;
}

std::string value_to_str(const Headers::value_type *nv) {
  if (nv) {
    return nv->value;
  }
  return "";
}

bool non_empty_value(const Headers::value_type *nv) {
  return nv && !nv->value.empty();
}

namespace {
nghttp2_nv make_nv_internal(const std::string &name, const std::string &value,
                            bool no_index, uint8_t nv_flags) {
  uint8_t flags;

  flags =
      nv_flags | (no_index ? NGHTTP2_NV_FLAG_NO_INDEX : NGHTTP2_NV_FLAG_NONE);

  return {(uint8_t *)name.c_str(), (uint8_t *)value.c_str(), name.size(),
          value.size(), flags};
}
} // namespace

nghttp2_nv make_nv(const std::string &name, const std::string &value,
                   bool no_index) {
  return make_nv_internal(name, value, no_index, NGHTTP2_NV_FLAG_NONE);
}

nghttp2_nv make_nv_nocopy(const std::string &name, const std::string &value,
                          bool no_index) {
  return make_nv_internal(name, value, no_index,
                          NGHTTP2_NV_FLAG_NO_COPY_NAME |
                              NGHTTP2_NV_FLAG_NO_COPY_VALUE);
}

namespace {
void copy_headers_to_nva_internal(std::vector<nghttp2_nv> &nva,
                                  const Headers &headers, uint8_t nv_flags) {
  for (auto &kv : headers) {
    if (kv.name.empty() || kv.name[0] == ':') {
      continue;
    }
    switch (kv.token) {
    case HD_COOKIE:
    case HD_CONNECTION:
    case HD_FORWARDED:
    case HD_HOST:
    case HD_HTTP2_SETTINGS:
    case HD_KEEP_ALIVE:
    case HD_PROXY_CONNECTION:
    case HD_SERVER:
    case HD_TE:
    case HD_TRANSFER_ENCODING:
    case HD_UPGRADE:
    case HD_VIA:
    case HD_X_FORWARDED_FOR:
    case HD_X_FORWARDED_PROTO:
      continue;
    }
    nva.push_back(make_nv_internal(kv.name, kv.value, kv.no_index, nv_flags));
  }
}
} // namespace

void copy_headers_to_nva(std::vector<nghttp2_nv> &nva, const Headers &headers) {
  copy_headers_to_nva_internal(nva, headers, NGHTTP2_NV_FLAG_NONE);
}

void copy_headers_to_nva_nocopy(std::vector<nghttp2_nv> &nva,
                                const Headers &headers) {
  copy_headers_to_nva_internal(nva, headers, NGHTTP2_NV_FLAG_NO_COPY_NAME |
                                                 NGHTTP2_NV_FLAG_NO_COPY_VALUE);
}

void build_http1_headers_from_headers(DefaultMemchunks *buf,
                                      const Headers &headers) {
  for (auto &kv : headers) {
    if (kv.name.empty() || kv.name[0] == ':') {
      continue;
    }
    switch (kv.token) {
    case HD_CONNECTION:
    case HD_COOKIE:
    case HD_FORWARDED:
    case HD_HOST:
    case HD_HTTP2_SETTINGS:
    case HD_KEEP_ALIVE:
    case HD_PROXY_CONNECTION:
    case HD_SERVER:
    case HD_UPGRADE:
    case HD_VIA:
    case HD_X_FORWARDED_FOR:
    case HD_X_FORWARDED_PROTO:
      continue;
    }
    capitalize(buf, kv.name);
    buf->append(": ");
    buf->append(kv.value);
    buf->append("\r\n");
  }
}

int32_t determine_window_update_transmission(nghttp2_session *session,
                                             int32_t stream_id) {
  int32_t recv_length, window_size;
  if (stream_id == 0) {
    recv_length = nghttp2_session_get_effective_recv_data_length(session);
    window_size = nghttp2_session_get_effective_local_window_size(session);
  } else {
    recv_length = nghttp2_session_get_stream_effective_recv_data_length(
        session, stream_id);
    window_size = nghttp2_session_get_stream_effective_local_window_size(
        session, stream_id);
  }
  if (recv_length != -1 && window_size != -1) {
    if (recv_length >= window_size / 2) {
      return recv_length;
    }
  }
  return -1;
}

void dump_nv(FILE *out, const char **nv) {
  for (size_t i = 0; nv[i]; i += 2) {
    fprintf(out, "%s: %s\n", nv[i], nv[i + 1]);
  }
  fputc('\n', out);
  fflush(out);
}

void dump_nv(FILE *out, const nghttp2_nv *nva, size_t nvlen) {
  auto end = nva + nvlen;
  for (; nva != end; ++nva) {
    fprintf(out, "%s: %s\n", nva->name, nva->value);
  }
  fputc('\n', out);
  fflush(out);
}

void dump_nv(FILE *out, const Headers &nva) {
  for (auto &nv : nva) {
    fprintf(out, "%s: %s\n", nv.name.c_str(), nv.value.c_str());
  }
  fputc('\n', out);
  fflush(out);
}

std::string rewrite_location_uri(const std::string &uri,
                                 const http_parser_url &u,
                                 const std::string &match_host,
                                 const std::string &request_authority,
                                 const std::string &upstream_scheme) {
  // We just rewrite scheme and authority.
  if ((u.field_set & (1 << UF_HOST)) == 0) {
    return "";
  }
  auto field = &u.field_data[UF_HOST];
  if (!util::starts_with(std::begin(match_host), std::end(match_host),
                         &uri[field->off], &uri[field->off] + field->len) ||
      (match_host.size() != field->len && match_host[field->len] != ':')) {
    return "";
  }
  std::string res;
  if (!request_authority.empty()) {
    res += upstream_scheme;
    res += "://";
    res += request_authority;
  }
  if (u.field_set & (1 << UF_PATH)) {
    field = &u.field_data[UF_PATH];
    res.append(&uri[field->off], field->len);
  }
  if (u.field_set & (1 << UF_QUERY)) {
    field = &u.field_data[UF_QUERY];
    res += '?';
    res.append(&uri[field->off], field->len);
  }
  if (u.field_set & (1 << UF_FRAGMENT)) {
    field = &u.field_data[UF_FRAGMENT];
    res += '#';
    res.append(&uri[field->off], field->len);
  }
  return res;
}

int check_nv(const uint8_t *name, size_t namelen, const uint8_t *value,
             size_t valuelen) {
  if (!nghttp2_check_header_name(name, namelen)) {
    return 0;
  }
  if (!nghttp2_check_header_value(value, valuelen)) {
    return 0;
  }
  return 1;
}

int parse_http_status_code(const std::string &src) {
  if (src.size() != 3) {
    return -1;
  }

  int status = 0;
  for (auto c : src) {
    if (!isdigit(c)) {
      return -1;
    }
    status *= 10;
    status += c - '0';
  }

  if (status < 100) {
    return -1;
  }

  return status;
}

int lookup_token(const std::string &name) {
  return lookup_token(reinterpret_cast<const uint8_t *>(name.c_str()),
                      name.size());
}

// This function was generated by genheaderfunc.py.  Inspired by h2o
// header lookup.  https://github.com/h2o/h2o
int lookup_token(const uint8_t *name, size_t namelen) {
  switch (namelen) {
  case 2:
    switch (name[1]) {
    case 'e':
      if (util::streq_l("t", name, 1)) {
        return HD_TE;
      }
      break;
    }
    break;
  case 3:
    switch (name[2]) {
    case 'a':
      if (util::streq_l("vi", name, 2)) {
        return HD_VIA;
      }
      break;
    }
    break;
  case 4:
    switch (name[3]) {
    case 'e':
      if (util::streq_l("dat", name, 3)) {
        return HD_DATE;
      }
      break;
    case 'k':
      if (util::streq_l("lin", name, 3)) {
        return HD_LINK;
      }
      break;
    case 't':
      if (util::streq_l("hos", name, 3)) {
        return HD_HOST;
      }
      break;
    }
    break;
  case 5:
    switch (name[4]) {
    case 'h':
      if (util::streq_l(":pat", name, 4)) {
        return HD__PATH;
      }
      break;
    case 't':
      if (util::streq_l(":hos", name, 4)) {
        return HD__HOST;
      }
      break;
    }
    break;
  case 6:
    switch (name[5]) {
    case 'e':
      if (util::streq_l("cooki", name, 5)) {
        return HD_COOKIE;
      }
      break;
    case 'r':
      if (util::streq_l("serve", name, 5)) {
        return HD_SERVER;
      }
      break;
    case 't':
      if (util::streq_l("expec", name, 5)) {
        return HD_EXPECT;
      }
      break;
    }
    break;
  case 7:
    switch (name[6]) {
    case 'c':
      if (util::streq_l("alt-sv", name, 6)) {
        return HD_ALT_SVC;
      }
      break;
    case 'd':
      if (util::streq_l(":metho", name, 6)) {
        return HD__METHOD;
      }
      break;
    case 'e':
      if (util::streq_l(":schem", name, 6)) {
        return HD__SCHEME;
      }
      if (util::streq_l("upgrad", name, 6)) {
        return HD_UPGRADE;
      }
      break;
    case 'r':
      if (util::streq_l("traile", name, 6)) {
        return HD_TRAILER;
      }
      break;
    case 's':
      if (util::streq_l(":statu", name, 6)) {
        return HD__STATUS;
      }
      break;
    }
    break;
  case 8:
    switch (name[7]) {
    case 'n':
      if (util::streq_l("locatio", name, 7)) {
        return HD_LOCATION;
      }
      break;
    }
    break;
  case 9:
    switch (name[8]) {
    case 'd':
      if (util::streq_l("forwarde", name, 8)) {
        return HD_FORWARDED;
      }
      break;
    }
    break;
  case 10:
    switch (name[9]) {
    case 'e':
      if (util::streq_l("keep-aliv", name, 9)) {
        return HD_KEEP_ALIVE;
      }
      break;
    case 'n':
      if (util::streq_l("connectio", name, 9)) {
        return HD_CONNECTION;
      }
      break;
    case 't':
      if (util::streq_l("user-agen", name, 9)) {
        return HD_USER_AGENT;
      }
      break;
    case 'y':
      if (util::streq_l(":authorit", name, 9)) {
        return HD__AUTHORITY;
      }
      break;
    }
    break;
  case 12:
    switch (name[11]) {
    case 'e':
      if (util::streq_l("content-typ", name, 11)) {
        return HD_CONTENT_TYPE;
      }
      break;
    }
    break;
  case 13:
    switch (name[12]) {
    case 'l':
      if (util::streq_l("cache-contro", name, 12)) {
        return HD_CACHE_CONTROL;
      }
      break;
    }
    break;
  case 14:
    switch (name[13]) {
    case 'h':
      if (util::streq_l("content-lengt", name, 13)) {
        return HD_CONTENT_LENGTH;
      }
      break;
    case 's':
      if (util::streq_l("http2-setting", name, 13)) {
        return HD_HTTP2_SETTINGS;
      }
      break;
    }
    break;
  case 15:
    switch (name[14]) {
    case 'e':
      if (util::streq_l("accept-languag", name, 14)) {
        return HD_ACCEPT_LANGUAGE;
      }
      break;
    case 'g':
      if (util::streq_l("accept-encodin", name, 14)) {
        return HD_ACCEPT_ENCODING;
      }
      break;
    case 'r':
      if (util::streq_l("x-forwarded-fo", name, 14)) {
        return HD_X_FORWARDED_FOR;
      }
      break;
    }
    break;
  case 16:
    switch (name[15]) {
    case 'n':
      if (util::streq_l("proxy-connectio", name, 15)) {
        return HD_PROXY_CONNECTION;
      }
      break;
    }
    break;
  case 17:
    switch (name[16]) {
    case 'e':
      if (util::streq_l("if-modified-sinc", name, 16)) {
        return HD_IF_MODIFIED_SINCE;
      }
      break;
    case 'g':
      if (util::streq_l("transfer-encodin", name, 16)) {
        return HD_TRANSFER_ENCODING;
      }
      break;
    case 'o':
      if (util::streq_l("x-forwarded-prot", name, 16)) {
        return HD_X_FORWARDED_PROTO;
      }
      break;
    }
    break;
  }
  return -1;
}

void init_hdidx(HeaderIndex &hdidx) {
  std::fill(std::begin(hdidx), std::end(hdidx), -1);
}

void index_header(HeaderIndex &hdidx, int16_t token, size_t idx) {
  if (token == -1) {
    return;
  }
  assert(token < HD_MAXIDX);
  hdidx[token] = idx;
}

bool check_http2_request_pseudo_header(const HeaderIndex &hdidx,
                                       int16_t token) {
  switch (token) {
  case HD__AUTHORITY:
  case HD__METHOD:
  case HD__PATH:
  case HD__SCHEME:
    return hdidx[token] == -1;
  default:
    return false;
  }
}

bool check_http2_response_pseudo_header(const HeaderIndex &hdidx,
                                        int16_t token) {
  switch (token) {
  case HD__STATUS:
    return hdidx[token] == -1;
  default:
    return false;
  }
}

bool http2_header_allowed(int16_t token) {
  switch (token) {
  case HD_CONNECTION:
  case HD_KEEP_ALIVE:
  case HD_PROXY_CONNECTION:
  case HD_TRANSFER_ENCODING:
  case HD_UPGRADE:
    return false;
  default:
    return true;
  }
}

bool http2_mandatory_request_headers_presence(const HeaderIndex &hdidx) {
  if (hdidx[HD__METHOD] == -1 || hdidx[HD__PATH] == -1 ||
      hdidx[HD__SCHEME] == -1 ||
      (hdidx[HD__AUTHORITY] == -1 && hdidx[HD_HOST] == -1)) {
    return false;
  }
  return true;
}

const Headers::value_type *get_header(const HeaderIndex &hdidx, int16_t token,
                                      const Headers &nva) {
  auto i = hdidx[token];
  if (i == -1) {
    return nullptr;
  }
  return &nva[i];
}

Headers::value_type *get_header(const HeaderIndex &hdidx, int16_t token,
                                Headers &nva) {
  auto i = hdidx[token];
  if (i == -1) {
    return nullptr;
  }
  return &nva[i];
}

namespace {
template <typename InputIt> InputIt skip_lws(InputIt first, InputIt last) {
  for (; first != last; ++first) {
    switch (*first) {
    case ' ':
    case '\t':
      continue;
    default:
      return first;
    }
  }
  return first;
}
} // namespace

namespace {
template <typename InputIt>
InputIt skip_to_next_field(InputIt first, InputIt last) {
  for (; first != last; ++first) {
    switch (*first) {
    case ' ':
    case '\t':
    case ',':
      continue;
    default:
      return first;
    }
  }
  return first;
}
} // namespace

namespace {
// Skip to the right dquote ('"'), handling backslash escapes.
// Returns |last| if input is not terminated with '"'.
template <typename InputIt>
InputIt skip_to_right_dquote(InputIt first, InputIt last) {
  for (; first != last;) {
    switch (*first) {
    case '"':
      return first;
    case '\\':
      ++first;
      if (first == last) {
        return first;
      }
      break;
    }
    ++first;
  }
  return first;
}
} // namespace

namespace {
// Returns true if link-param does not match pattern |pat| of length
// |patlen| or it has empty value ("").  |pat| should be parmname
// followed by "=".
bool check_link_param_empty(const char *first, const char *last,
                            const char *pat, size_t patlen) {
  if (first + patlen <= last) {
    if (std::equal(pat, pat + patlen, first, util::CaseCmp())) {
      // we only accept URI if pat is followd by "" (e.g.,
      // loadpolicy="") here.
      if (first + patlen + 2 <= last) {
        if (*(first + patlen) != '"' || *(first + patlen + 1) != '"') {
          return false;
        }
      } else {
        // here we got invalid production (anchor=") or anchor=?
        return false;
      }
    }
  }
  return true;
}
} // namespace

namespace {
std::pair<LinkHeader, const char *>
parse_next_link_header_once(const char *first, const char *last) {
  first = skip_to_next_field(first, last);
  if (first == last || *first != '<') {
    return {{{nullptr, nullptr}}, last};
  }
  auto url_first = ++first;
  first = std::find(first, last, '>');
  if (first == last) {
    return {{{nullptr, nullptr}}, first};
  }
  auto url_last = first++;
  if (first == last) {
    return {{{nullptr, nullptr}}, first};
  }
  // we expect ';' or ',' here
  switch (*first) {
  case ',':
    return {{{nullptr, nullptr}}, ++first};
  case ';':
    ++first;
    break;
  default:
    return {{{nullptr, nullptr}}, last};
  }

  auto ok = false;
  auto ign = false;
  for (;;) {
    first = skip_lws(first, last);
    if (first == last) {
      return {{{nullptr, nullptr}}, first};
    }
    // we expect link-param

    // rel can take several relations using quoted form.
    static constexpr char PLP[] = "rel=\"";
    static constexpr size_t PLPLEN = sizeof(PLP) - 1;

    static constexpr char PLT[] = "preload";
    static constexpr size_t PLTLEN = sizeof(PLT) - 1;
    if (first + PLPLEN < last && *(first + PLPLEN - 1) == '"' &&
        std::equal(PLP, PLP + PLPLEN, first, util::CaseCmp())) {
      // we have to search preload in whitespace separated list:
      // rel="preload something http://example.org/foo"
      first += PLPLEN;
      auto start = first;
      for (; first != last;) {
        if (*first != ' ' && *first != '"') {
          ++first;
          continue;
        }

        if (start == first) {
          return {{{nullptr, nullptr}}, last};
        }

        if (!ok && start + PLTLEN == first &&
            std::equal(PLT, PLT + PLTLEN, start, util::CaseCmp())) {
          ok = true;
        }

        if (*first == '"') {
          break;
        }
        first = skip_lws(first, last);
        start = first;
      }
      if (first == last) {
        return {{{nullptr, nullptr}}, first};
      }
      assert(*first == '"');
      ++first;
      if (first == last || *first == ',') {
        goto almost_done;
      }
      if (*first == ';') {
        ++first;
        // parse next link-param
        continue;
      }
      return {{{nullptr, nullptr}}, last};
    }
    // we are only interested in rel=preload parameter.  Others are
    // simply skipped.
    static constexpr char PL[] = "rel=preload";
    static constexpr size_t PLLEN = sizeof(PL) - 1;
    if (first + PLLEN == last) {
      if (std::equal(PL, PL + PLLEN, first, util::CaseCmp())) {
        // ok = true;
        // this is the end of sequence
        return {{{url_first, url_last}}, last};
      }
    } else if (first + PLLEN + 1 <= last) {
      switch (*(first + PLLEN)) {
      case ',':
        if (!std::equal(PL, PL + PLLEN, first, util::CaseCmp())) {
          break;
        }
        // ok = true;
        // skip including ','
        first += PLLEN + 1;
        return {{{url_first, url_last}}, first};
      case ';':
        if (!std::equal(PL, PL + PLLEN, first, util::CaseCmp())) {
          break;
        }
        ok = true;
        // skip including ';'
        first += PLLEN + 1;
        // continue parse next link-param
        continue;
      }
    }
    // we have to reject URI if we have nonempty anchor parameter.
    static constexpr char ANCHOR[] = "anchor=";
    static constexpr size_t ANCHORLEN = sizeof(ANCHOR) - 1;
    if (!ign && !check_link_param_empty(first, last, ANCHOR, ANCHORLEN)) {
      ign = true;
    }

    // reject URI if we have non-empty loadpolicy.  This could be
    // tightened up to just pick up "next" or "insert".
    static constexpr char LOADPOLICY[] = "loadpolicy=";
    static constexpr size_t LOADPOLICYLEN = sizeof(LOADPOLICY) - 1;
    if (!ign &&
        !check_link_param_empty(first, last, LOADPOLICY, LOADPOLICYLEN)) {
      ign = true;
    }

    auto param_first = first;
    for (; first != last;) {
      if (util::in_attr_char(*first)) {
        ++first;
        continue;
      }
      // '*' is only allowed at the end of parameter name and must be
      // followed by '='
      if (last - first >= 2 && first != param_first) {
        if (*first == '*' && *(first + 1) == '=') {
          ++first;
          break;
        }
      }
      if (*first == '=' || *first == ';' || *first == ',') {
        break;
      }
      return {{{nullptr, nullptr}}, last};
    }
    if (param_first == first) {
      // empty parmname
      return {{{nullptr, nullptr}}, last};
    }
    // link-param without value is acceptable (see link-extension) if
    // it is not followed by '='
    if (first == last || *first == ',') {
      goto almost_done;
    }
    if (*first == ';') {
      ++first;
      // parse next link-param
      continue;
    }
    // now parsing link-param value
    assert(*first == '=');
    ++first;
    if (first == last) {
      // empty value is not acceptable
      return {{{nullptr, nullptr}}, first};
    }
    if (*first == '"') {
      // quoted-string
      first = skip_to_right_dquote(first + 1, last);
      if (first == last) {
        return {{{nullptr, nullptr}}, first};
      }
      ++first;
      if (first == last || *first == ',') {
        goto almost_done;
      }
      if (*first == ';') {
        ++first;
        // parse next link-param
        continue;
      }
      return {{{nullptr, nullptr}}, last};
    }
    // not quoted-string, skip to next ',' or ';'
    if (*first == ',' || *first == ';') {
      // empty value
      return {{{nullptr, nullptr}}, last};
    }
    for (; first != last; ++first) {
      if (*first == ',' || *first == ';') {
        break;
      }
    }
    if (first == last || *first == ',') {
      goto almost_done;
    }
    assert(*first == ';');
    ++first;
    // parse next link-param
  }

almost_done:
  assert(first == last || *first == ',');

  if (first != last) {
    ++first;
  }
  if (ok && !ign) {
    return {{{url_first, url_last}}, first};
  }
  return {{{nullptr, nullptr}}, first};
}
} // namespace

std::vector<LinkHeader> parse_link_header(const char *src, size_t len) {
  auto first = src;
  auto last = src + len;
  std::vector<LinkHeader> res;
  for (; first != last;) {
    auto rv = parse_next_link_header_once(first, last);
    first = rv.second;
    if (rv.first.uri.first != nullptr && rv.first.uri.second != nullptr) {
      res.push_back(rv.first);
    }
  }
  return res;
}

namespace {
void eat_file(std::string &path) {
  if (path.empty()) {
    path = "/";
    return;
  }
  auto p = path.size() - 1;
  if (path[p] == '/') {
    return;
  }
  p = path.rfind('/', p);
  if (p == std::string::npos) {
    // this should not happend in normal case, where we expect path
    // starts with '/'
    path = "/";
    return;
  }
  path.erase(std::begin(path) + p + 1, std::end(path));
}
} // namespace

namespace {
void eat_dir(std::string &path) {
  if (path.empty()) {
    path = "/";
    return;
  }
  auto p = path.size() - 1;
  if (path[p] != '/') {
    p = path.rfind('/', p);
    if (p == std::string::npos) {
      // this should not happend in normal case, where we expect path
      // starts with '/'
      path = "/";
      return;
    }
  }
  if (path[p] == '/') {
    if (p == 0) {
      return;
    }
    --p;
  }
  p = path.rfind('/', p);
  if (p == std::string::npos) {
    // this should not happend in normal case, where we expect path
    // starts with '/'
    path = "/";
    return;
  }
  path.erase(std::begin(path) + p + 1, std::end(path));
}
} // namespace

std::string path_join(const char *base_path, size_t base_pathlen,
                      const char *base_query, size_t base_querylen,
                      const char *rel_path, size_t rel_pathlen,
                      const char *rel_query, size_t rel_querylen) {
  std::string res;
  if (rel_pathlen == 0) {
    if (base_pathlen == 0) {
      res = "/";
    } else {
      res.assign(base_path, base_pathlen);
    }
    if (rel_querylen == 0) {
      if (base_querylen) {
        res += '?';
        res.append(base_query, base_querylen);
      }
      return res;
    }
    res += '?';
    res.append(rel_query, rel_querylen);
    return res;
  }

  auto first = rel_path;
  auto last = rel_path + rel_pathlen;

  if (rel_path[0] == '/') {
    res = "/";
    ++first;
  } else if (base_pathlen == 0) {
    res = "/";
  } else {
    res.assign(base_path, base_pathlen);
  }

  for (; first != last;) {
    if (*first == '.') {
      if (first + 1 == last) {
        break;
      }
      if (*(first + 1) == '/') {
        first += 2;
        continue;
      }
      if (*(first + 1) == '.') {
        if (first + 2 == last) {
          eat_dir(res);
          break;
        }
        if (*(first + 2) == '/') {
          eat_dir(res);
          first += 3;
          continue;
        }
      }
    }
    if (res.back() != '/') {
      eat_file(res);
    }
    auto slash = std::find(first, last, '/');
    if (slash == last) {
      res.append(first, last);
      break;
    }
    res.append(first, slash + 1);
    first = slash + 1;
    for (; first != last && *first == '/'; ++first)
      ;
  }
  if (rel_querylen) {
    res += '?';
    res.append(rel_query, rel_querylen);
  }
  return res;
}

bool expect_response_body(int status_code) {
  return status_code / 100 != 1 && status_code != 304 && status_code != 204;
}

bool expect_response_body(const std::string &method, int status_code) {
  return method != "HEAD" && expect_response_body(status_code);
}

bool expect_response_body(int method_token, int status_code) {
  return method_token != HTTP_HEAD && expect_response_body(status_code);
}

int lookup_method_token(const std::string &name) {
  return lookup_method_token(reinterpret_cast<const uint8_t *>(name.c_str()),
                             name.size());
}

// This function was generated by genmethodfunc.py.
int lookup_method_token(const uint8_t *name, size_t namelen) {
  switch (namelen) {
  case 3:
    switch (name[2]) {
    case 'T':
      if (util::streq_l("GE", name, 2)) {
        return HTTP_GET;
      }
      if (util::streq_l("PU", name, 2)) {
        return HTTP_PUT;
      }
      break;
    }
    break;
  case 4:
    switch (name[3]) {
    case 'D':
      if (util::streq_l("HEA", name, 3)) {
        return HTTP_HEAD;
      }
      break;
    case 'E':
      if (util::streq_l("MOV", name, 3)) {
        return HTTP_MOVE;
      }
      break;
    case 'K':
      if (util::streq_l("LOC", name, 3)) {
        return HTTP_LOCK;
      }
      break;
    case 'T':
      if (util::streq_l("POS", name, 3)) {
        return HTTP_POST;
      }
      break;
    case 'Y':
      if (util::streq_l("COP", name, 3)) {
        return HTTP_COPY;
      }
      break;
    }
    break;
  case 5:
    switch (name[4]) {
    case 'E':
      if (util::streq_l("MERG", name, 4)) {
        return HTTP_MERGE;
      }
      if (util::streq_l("PURG", name, 4)) {
        return HTTP_PURGE;
      }
      if (util::streq_l("TRAC", name, 4)) {
        return HTTP_TRACE;
      }
      break;
    case 'H':
      if (util::streq_l("PATC", name, 4)) {
        return HTTP_PATCH;
      }
      break;
    case 'L':
      if (util::streq_l("MKCO", name, 4)) {
        return HTTP_MKCOL;
      }
      break;
    }
    break;
  case 6:
    switch (name[5]) {
    case 'E':
      if (util::streq_l("DELET", name, 5)) {
        return HTTP_DELETE;
      }
      break;
    case 'H':
      if (util::streq_l("SEARC", name, 5)) {
        return HTTP_SEARCH;
      }
      break;
    case 'K':
      if (util::streq_l("UNLOC", name, 5)) {
        return HTTP_UNLOCK;
      }
      break;
    case 'T':
      if (util::streq_l("REPOR", name, 5)) {
        return HTTP_REPORT;
      }
      break;
    case 'Y':
      if (util::streq_l("NOTIF", name, 5)) {
        return HTTP_NOTIFY;
      }
      break;
    }
    break;
  case 7:
    switch (name[6]) {
    case 'H':
      if (util::streq_l("MSEARC", name, 6)) {
        return HTTP_MSEARCH;
      }
      break;
    case 'S':
      if (util::streq_l("OPTION", name, 6)) {
        return HTTP_OPTIONS;
      }
      break;
    case 'T':
      if (util::streq_l("CONNEC", name, 6)) {
        return HTTP_CONNECT;
      }
      break;
    }
    break;
  case 8:
    switch (name[7]) {
    case 'D':
      if (util::streq_l("PROPFIN", name, 7)) {
        return HTTP_PROPFIND;
      }
      break;
    case 'T':
      if (util::streq_l("CHECKOU", name, 7)) {
        return HTTP_CHECKOUT;
      }
      break;
    }
    break;
  case 9:
    switch (name[8]) {
    case 'E':
      if (util::streq_l("SUBSCRIB", name, 8)) {
        return HTTP_SUBSCRIBE;
      }
      break;
    case 'H':
      if (util::streq_l("PROPPATC", name, 8)) {
        return HTTP_PROPPATCH;
      }
      break;
    }
    break;
  case 10:
    switch (name[9]) {
    case 'R':
      if (util::streq_l("MKCALENDA", name, 9)) {
        return HTTP_MKCALENDAR;
      }
      break;
    case 'Y':
      if (util::streq_l("MKACTIVIT", name, 9)) {
        return HTTP_MKACTIVITY;
      }
      break;
    }
    break;
  case 11:
    switch (name[10]) {
    case 'E':
      if (util::streq_l("UNSUBSCRIB", name, 10)) {
        return HTTP_UNSUBSCRIBE;
      }
      break;
    }
    break;
  }
  return -1;
}

const char *to_method_string(int method_token) {
  // we happened to use same value for method with http-parser.
  return http_method_str(static_cast<http_method>(method_token));
}

int get_pure_path_component(const char **base, size_t *baselen,
                            const std::string &uri) {
  int rv;

  http_parser_url u{};
  rv = http_parser_parse_url(uri.c_str(), uri.size(), 0, &u);
  if (rv != 0) {
    return -1;
  }

  if (u.field_set & (1 << UF_PATH)) {
    auto &f = u.field_data[UF_PATH];
    *base = uri.c_str() + f.off;
    *baselen = f.len;

    return 0;
  }

  *base = "/";
  *baselen = 1;

  return 0;
}

int construct_push_component(std::string &scheme, std::string &authority,
                             std::string &path, const char *base,
                             size_t baselen, const char *uri, size_t len) {
  int rv;
  const char *rel, *relq = nullptr;
  size_t rellen, relqlen = 0;

  http_parser_url u{};

  rv = http_parser_parse_url(uri, len, 0, &u);

  if (rv != 0) {
    if (uri[0] == '/') {
      return -1;
    }

    // treat link_url as relative URI.
    auto end = std::find(uri, uri + len, '#');
    auto q = std::find(uri, end, '?');

    rel = uri;
    rellen = q - uri;
    if (q != end) {
      relq = q + 1;
      relqlen = end - relq;
    }
  } else {
    if (u.field_set & (1 << UF_SCHEMA)) {
      http2::copy_url_component(scheme, &u, UF_SCHEMA, uri);
    }

    if (u.field_set & (1 << UF_HOST)) {
      http2::copy_url_component(authority, &u, UF_HOST, uri);
      if (u.field_set & (1 << UF_PORT)) {
        authority += ':';
        authority += util::utos(u.port);
      }
    }

    if (u.field_set & (1 << UF_PATH)) {
      auto &f = u.field_data[UF_PATH];
      rel = uri + f.off;
      rellen = f.len;
    } else {
      rel = "/";
      rellen = 1;
    }

    if (u.field_set & (1 << UF_QUERY)) {
      auto &f = u.field_data[UF_QUERY];
      relq = uri + f.off;
      relqlen = f.len;
    }
  }

  path =
      http2::path_join(base, baselen, nullptr, 0, rel, rellen, relq, relqlen);

  return 0;
}

} // namespace http2

} // namespace nghttp2
