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
#ifndef SSL_H
#define SSL_H

#include "nghttp2_config.h"

#include <cinttypes>

#include <openssl/ssl.h>

namespace nghttp2 {

namespace ssl {

// Acquire OpenSSL global lock to share SSL_CTX across multiple
// threads. The constructor acquires lock and destructor unlocks.
class LibsslGlobalLock {
public:
  LibsslGlobalLock();
  ~LibsslGlobalLock();
  LibsslGlobalLock(const LibsslGlobalLock &) = delete;
  LibsslGlobalLock &operator=(const LibsslGlobalLock &) = delete;
};

extern const char *const DEFAULT_CIPHER_LIST;

const char *get_tls_protocol(SSL *ssl);

struct TLSSessionInfo {
  const char *cipher;
  const char *protocol;
  const uint8_t *session_id;
  bool session_reused;
  size_t session_id_length;
};

TLSSessionInfo *get_tls_session_info(TLSSessionInfo *tls_info, SSL *ssl);

// Returns true if SSL/TLS requirement for HTTP/2 is fulfilled.
// To fulfill the requirement, the following 2 terms must be hold:
//
// 1. The negotiated protocol must be TLSv1.2.
// 2. The negotiated cipher cuite is not listed in the black list
//    described in RFC 7540.
bool check_http2_requirement(SSL *ssl);

// Initializes OpenSSL library
void libssl_init();

} // namespace ssl

} // namespace nghttp2

#endif // SSL_H
