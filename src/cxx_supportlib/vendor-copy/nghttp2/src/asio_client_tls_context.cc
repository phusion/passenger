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
#include "asio_client_tls_context.h"

#include <openssl/ssl.h>

#include <boost/asio/ssl.hpp>

#include "ssl.h"
#include "util.h"

namespace nghttp2 {
namespace asio_http2 {
namespace client {

namespace {
int client_select_next_proto_cb(SSL *ssl, unsigned char **out,
                                unsigned char *outlen, const unsigned char *in,
                                unsigned int inlen, void *arg) {
  if (!util::select_h2(const_cast<const unsigned char **>(out), outlen, in,
                       inlen)) {
    return SSL_TLSEXT_ERR_NOACK;
  }
  return SSL_TLSEXT_ERR_OK;
}
} // namespace

boost::system::error_code
configure_tls_context(boost::system::error_code &ec,
                      boost::asio::ssl::context &tls_ctx) {
  ec.clear();

  auto ctx = tls_ctx.native_handle();

  SSL_CTX_set_next_proto_select_cb(ctx, client_select_next_proto_cb, nullptr);

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
  auto proto_list = util::get_default_alpn();

  SSL_CTX_set_alpn_protos(ctx, proto_list.data(), proto_list.size());
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L

  return ec;
}

} // namespace client
} // namespace asio_http2
} // namespace nghttp2
