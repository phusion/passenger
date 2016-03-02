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
#ifndef SHRPX_SSL_H
#define SHRPX_SSL_H

#include "shrpx.h"

#include <vector>
#include <mutex>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <ev.h>

#ifdef HAVE_NEVERBLEED
#include <neverbleed.h>
#endif // HAVE_NEVERBLEED

namespace shrpx {

class ClientHandler;
class Worker;
class DownstreamConnectionPool;
struct DownstreamAddr;

namespace ssl {

// This struct stores the additional information per SSL_CTX.  This is
// attached to SSL_CTX using SSL_CTX_set_app_data().
struct TLSContextData {
  // Protects ocsp_data;
  std::mutex mu;
  // OCSP response
  std::shared_ptr<std::vector<uint8_t>> ocsp_data;

  // Path to certificate file
  const char *cert_file;
};

// Create server side SSL_CTX
SSL_CTX *create_ssl_context(const char *private_key_file, const char *cert_file
#ifdef HAVE_NEVERBLEED
                            ,
                            neverbleed_t *nb
#endif // HAVE_NEVERBLEED
                            );

// Create client side SSL_CTX
SSL_CTX *create_ssl_client_context(
#ifdef HAVE_NEVERBLEED
    neverbleed_t *nb
#endif // HAVE_NEVERBLEED
    );

ClientHandler *accept_connection(Worker *worker, int fd, sockaddr *addr,
                                 int addrlen);

// Check peer's certificate against first downstream address in
// Config::downstream_addrs.  We only consider first downstream since
// we use this function for HTTP/2 downstream link only.
int check_cert(SSL *ssl, const DownstreamAddr *addr);

// Retrieves DNS and IP address in subjectAltNames and commonName from
// the |cert|.
void get_altnames(X509 *cert, std::vector<std::string> &dns_names,
                  std::vector<std::string> &ip_addrs, std::string &common_name);

// CertLookupTree forms lookup tree to get SSL_CTX whose DNS or
// commonName matches hostname in query. The tree is patricia trie
// data structure formed from the tail of the hostname pattern. Each
// CertNode contains part of hostname str member in range [first,
// last) member and the next member contains the following CertNode
// pointers ('following' means character before the current one). The
// CertNode where a hostname pattern ends contains its SSL_CTX pointer
// in the ssl_ctx member.  For wildcard hostname pattern, we store the
// its pattern and SSL_CTX in CertNode one before first "*" found from
// the tail.
//
// When querying SSL_CTX with particular hostname, we match from its
// tail in our lookup tree. If the query goes to the first character
// of the hostname and current CertNode has non-NULL ssl_ctx member,
// then it is the exact match. The ssl_ctx member is returned.  Along
// the way, if CertNode which contains non-empty wildcard_certs member
// is encountered, wildcard hostname matching is performed against
// them. If there is a match, its SSL_CTX is returned. If none
// matches, query is continued to the next character.

struct WildcardCert {
  SSL_CTX *ssl_ctx;
  char *hostname;
  size_t hostnamelen;
};

struct CertNode {
  // list of wildcard domain name and its SSL_CTX pair, the wildcard
  // '*' appears in this position.
  std::vector<WildcardCert> wildcard_certs;
  // Next CertNode index of CertLookupTree::nodes
  std::vector<std::unique_ptr<CertNode>> next;
  // SSL_CTX for exact match
  SSL_CTX *ssl_ctx;
  char *str;
  // [first, last) in the reverse direction in str, first >=
  // last. This indices only work for str member.
  int first, last;
};

class CertLookupTree {
public:
  CertLookupTree();

  // Adds |ssl_ctx| with hostname pattern |hostname| with length |len|
  // to the lookup tree.  The |hostname| must be NULL-terminated.
  void add_cert(SSL_CTX *ssl_ctx, const char *hostname, size_t len);

  // Looks up SSL_CTX using the given |hostname| with length |len|.
  // If more than one SSL_CTX which matches the query, it is undefined
  // which one is returned.  The |hostname| must be NULL-terminated.
  // If no matching SSL_CTX found, returns NULL.
  SSL_CTX *lookup(const char *hostname, size_t len);

private:
  CertNode root_;
  // Stores pointers to copied hostname when adding hostname and
  // ssl_ctx pair.
  std::vector<std::unique_ptr<char[]>> hosts_;
};

// Adds |ssl_ctx| to lookup tree |lt| using hostnames read from
// |certfile|. The subjectAltNames and commonName are considered as
// eligible hostname. This function returns 0 if it succeeds, or -1.
// Even if no ssl_ctx is added to tree, this function returns 0.
int cert_lookup_tree_add_cert_from_file(CertLookupTree *lt, SSL_CTX *ssl_ctx,
                                        const char *certfile);

// Returns true if |needle| which has |len| bytes is included in the
// protocol list |protos|.
bool in_proto_list(const std::vector<std::string> &protos,
                   const unsigned char *needle, size_t len);

// Returns true if security requirement for HTTP/2 is fulfilled.
bool check_http2_requirement(SSL *ssl);

// Returns SSL/TLS option mask to disable SSL/TLS protocol version not
// included in |tls_proto_list|.  The returned mask can be directly
// passed to SSL_CTX_set_options().
long int create_tls_proto_mask(const std::vector<std::string> &tls_proto_list);

std::vector<unsigned char>
set_alpn_prefs(const std::vector<std::string> &protos);

// Setups server side SSL_CTX.  This function inspects get_config()
// and if upstream_no_tls is true, returns nullptr.  Otherwise
// construct default SSL_CTX.  If subcerts are available
// (get_config()->subcerts), caller should provide CertLookupTree
// object as |cert_tree| parameter, otherwise SNI does not work.  All
// the created SSL_CTX is stored into |all_ssl_ctx|.
SSL_CTX *setup_server_ssl_context(std::vector<SSL_CTX *> &all_ssl_ctx,
                                  CertLookupTree *cert_tree
#ifdef HAVE_NEVERBLEED
                                  ,
                                  neverbleed_t *nb
#endif // HAVE_NEVERBLEED
                                  );

// Setups client side SSL_CTX.  This function inspects get_config()
// and if downstream_no_tls is true, returns nullptr.  Otherwise, only
// construct SSL_CTX if either client_mode or http2_bridge is true.
SSL_CTX *setup_client_ssl_context(
#ifdef HAVE_NEVERBLEED
    neverbleed_t *nb
#endif // HAVE_NEVERBLEED
    );

// Creates CertLookupTree.  If frontend is configured not to use TLS,
// this function returns nullptr.
CertLookupTree *create_cert_lookup_tree();

SSL *create_ssl(SSL_CTX *ssl_ctx);

// Returns true if SSL/TLS is enabled on downstream
bool downstream_tls_enabled();

// Performs TLS hostname match.  |pattern| of length |plen| can
// contain wildcard character '*', which matches prefix of target
// hostname.  There are several restrictions to make wildcard work.
// The matching algorithm is based on RFC 6125.
bool tls_hostname_match(const char *pattern, size_t plen, const char *hostname,
                        size_t hlen);

} // namespace ssl

} // namespace shrpx

#endif // SHRPX_SSL_H
