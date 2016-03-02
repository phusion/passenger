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
#ifndef SHRPX_CLIENT_HANDLER_H
#define SHRPX_CLIENT_HANDLER_H

#include "shrpx.h"

#include <memory>

#include <ev.h>

#include <openssl/ssl.h>

#include "shrpx_rate_limit.h"
#include "shrpx_connection.h"
#include "buffer.h"
#include "memchunk.h"

using namespace nghttp2;

namespace shrpx {

class Upstream;
class DownstreamConnection;
class HttpsUpstream;
class ConnectBlocker;
class DownstreamConnectionPool;
class Worker;
struct WorkerStat;

class ClientHandler {
public:
  ClientHandler(Worker *worker, int fd, SSL *ssl, const char *ipaddr,
                const char *port);
  ~ClientHandler();

  int noop();
  // Performs clear text I/O
  int read_clear();
  int write_clear();
  // Performs TLS handshake
  int tls_handshake();
  // Performs TLS I/O
  int read_tls();
  int write_tls();

  int upstream_noop();
  int upstream_read();
  int upstream_http2_connhd_read();
  int upstream_http1_connhd_read();
  int upstream_write();

  int proxy_protocol_read();
  int on_proxy_protocol_finish();

  // Performs I/O operation.  Internally calls on_read()/on_write().
  int do_read();
  int do_write();

  // Processes buffers.  No underlying I/O operation will be done.
  int on_read();
  int on_write();

  struct ev_loop *get_loop() const;
  void reset_upstream_read_timeout(ev_tstamp t);
  void reset_upstream_write_timeout(ev_tstamp t);
  int validate_next_proto();
  const std::string &get_ipaddr() const;
  const std::string &get_port() const;
  bool get_should_close_after_write() const;
  void set_should_close_after_write(bool f);
  Upstream *get_upstream();

  void pool_downstream_connection(std::unique_ptr<DownstreamConnection> dconn);
  void remove_downstream_connection(DownstreamConnection *dconn);
  std::unique_ptr<DownstreamConnection>
  get_downstream_connection(Downstream *downstream);
  MemchunkPool *get_mcpool();
  SSL *get_ssl() const;
  ConnectBlocker *get_connect_blocker() const;
  // Call this function when HTTP/2 connection header is received at
  // the start of the connection.
  void direct_http2_upgrade();
  // Performs HTTP/2 Upgrade from the connection managed by
  // |http|. If this function fails, the connection must be
  // terminated. This function returns 0 if it succeeds, or -1.
  int perform_http2_upgrade(HttpsUpstream *http);
  bool get_http2_upgrade_allowed() const;
  // Returns upstream scheme, either "http" or "https"
  std::string get_upstream_scheme() const;
  void start_immediate_shutdown();

  // Writes upstream accesslog using |downstream|.  The |downstream|
  // must not be nullptr.
  void write_accesslog(Downstream *downstream);

  // Writes upstream accesslog.  This function is used if
  // corresponding Downstream object is not available.
  void write_accesslog(int major, int minor, unsigned int status,
                       int64_t body_bytes_sent);
  Worker *get_worker() const;

  using ReadBuf = Buffer<8_k>;

  ReadBuf *get_rb();

  RateLimit *get_rlimit();
  RateLimit *get_wlimit();

  void signal_write();
  ev_io *get_wev();

  void setup_upstream_io_callback();

  // Returns string suitable for use in "by" parameter of Forwarded
  // header field.
  const std::string &get_forwarded_by();
  // Returns string suitable for use in "for" parameter of Forwarded
  // header field.
  const std::string &get_forwarded_for() const;

private:
  Connection conn_;
  ev_timer reneg_shutdown_timer_;
  std::unique_ptr<Upstream> upstream_;
  std::unique_ptr<std::vector<ssize_t>> pinned_http2sessions_;
  // IP address of client.  If UNIX domain socket is used, this is
  // "localhost".
  std::string ipaddr_;
  std::string port_;
  // The ALPN identifier negotiated for this connection.
  std::string alpn_;
  // Host and port of this socket (e.g., "[::1]:8443")
  std::string local_hostport_;
  // The obfuscated version of client address used in "for" parameter
  // of Forwarded header field.
  std::string forwarded_for_obfuscated_;
  std::function<int(ClientHandler &)> read_, write_;
  std::function<int(ClientHandler &)> on_read_, on_write_;
  Worker *worker_;
  // The number of bytes of HTTP/2 client connection header to read
  size_t left_connhd_len_;
  bool should_close_after_write_;
  ReadBuf rb_;
};

} // namespace shrpx

#endif // SHRPX_CLIENT_HANDLER_H
