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
#include "asio_server_tls_context.h"

#include <openssl/ssl.h>

#include <boost/asio/ssl.hpp>

#include "ssl.h"
#include "util.h"

namespace nghttp2 {
namespace asio_http2 {
namespace server {

namespace {
std::vector<unsigned char> &get_alpn_token() {
  static auto alpn_token = util::get_default_alpn();
  return alpn_token;
}
} // namespace

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
namespace {
int alpn_select_proto_cb(SSL *ssl, const unsigned char **out,
                         unsigned char *outlen, const unsigned char *in,
                         unsigned int inlen, void *arg) {
  if (!util::select_h2(out, outlen, in, inlen)) {
    return SSL_TLSEXT_ERR_NOACK;
  }
  return SSL_TLSEXT_ERR_OK;
}
} // namespace
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L

boost::system::error_code
configure_tls_context_easy(boost::system::error_code &ec,
                           boost::asio::ssl::context &tls_context) {
  ec.clear();

  auto ctx = tls_context.native_handle();

  auto ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
                  SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION |
                  SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION |
                  SSL_OP_SINGLE_ECDH_USE | SSL_OP_NO_TICKET |
                  SSL_OP_CIPHER_SERVER_PREFERENCE;

  SSL_CTX_set_options(ctx, ssl_opts);
  SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
  SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);

  SSL_CTX_set_cipher_list(ctx, ssl::DEFAULT_CIPHER_LIST);

#ifndef OPENSSL_NO_EC
  auto ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (ecdh) {
    SSL_CTX_set_tmp_ecdh(ctx, ecdh);
    EC_KEY_free(ecdh);
  }
#endif /* OPENSSL_NO_EC */

  SSL_CTX_set_next_protos_advertised_cb(
      ctx,
      [](SSL *s, const unsigned char **data, unsigned int *len, void *arg) {
        auto &token = get_alpn_token();

        *data = token.data();
        *len = token.size();

        return SSL_TLSEXT_ERR_OK;
      },
      nullptr);

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
  // ALPN selection callback
  SSL_CTX_set_alpn_select_cb(ctx, alpn_select_proto_cb, nullptr);
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L

  return ec;
}

} // namespace server
} // namespace asio_http2
} // namespace nghttp2
