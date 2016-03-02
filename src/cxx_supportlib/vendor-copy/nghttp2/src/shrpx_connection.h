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
#ifndef SHRPX_CONNECTION_H
#define SHRPX_CONNECTION_H

#include "shrpx_config.h"

#include <sys/uio.h>

#include <ev.h>

#include <openssl/ssl.h>

#include "shrpx_rate_limit.h"
#include "shrpx_error.h"
#include "memchunk.h"

namespace shrpx {

struct MemcachedRequest;

enum {
  TLS_CONN_NORMAL,
  TLS_CONN_WAIT_FOR_SESSION_CACHE,
  TLS_CONN_GOT_SESSION_CACHE,
  TLS_CONN_CANCEL_SESSION_CACHE,
  TLS_CONN_WRITE_STARTED,
};

struct TLSConnection {
  DefaultMemchunks wbuf;
  DefaultPeekMemchunks rbuf;
  SSL *ssl;
  SSL_SESSION *cached_session;
  MemcachedRequest *cached_session_lookup_req;
  ev_tstamp last_write_idle;
  size_t warmup_writelen;
  // length passed to SSL_write and SSL_read last time.  This is
  // required since these functions require the exact same parameters
  // on non-blocking I/O.
  size_t last_writelen, last_readlen;
  int handshake_state;
  bool initial_handshake_done;
  bool reneg_started;
};

template <typename T> using EVCb = void (*)(struct ev_loop *, T *, int);

using IOCb = EVCb<ev_io>;
using TimerCb = EVCb<ev_timer>;

struct Connection {
  Connection(struct ev_loop *loop, int fd, SSL *ssl, MemchunkPool *mcpool,
             ev_tstamp write_timeout, ev_tstamp read_timeout,
             const RateLimitConfig &write_limit,
             const RateLimitConfig &read_limit, IOCb writecb, IOCb readcb,
             TimerCb timeoutcb, void *data, size_t tls_dyn_rec_warmup_threshold,
             ev_tstamp tls_dyn_rec_idle_timeout);
  ~Connection();

  void disconnect();

  void prepare_client_handshake();
  void prepare_server_handshake();

  int tls_handshake();
  int write_tls_pending_handshake();

  int check_http2_requirement();

  // All write_* and writev_clear functions return number of bytes
  // written.  If nothing cannot be written (e.g., there is no
  // allowance in RateLimit or underlying connection blocks), return
  // 0.  SHRPX_ERR_NETWORK is returned in case of error.
  //
  // All read_* functions return number of bytes read.  If nothing
  // cannot be read (e.g., there is no allowance in Ratelimit or
  // underlying connection blocks), return 0.  SHRPX_ERR_EOF is
  // returned in case of EOF and no data was read.  Otherwise
  // SHRPX_ERR_NETWORK is return in case of error.
  ssize_t write_tls(const void *data, size_t len);
  ssize_t read_tls(void *data, size_t len);

  size_t get_tls_write_limit();
  // Updates the number of bytes written in warm up period.
  void update_tls_warmup_writelen(size_t n);
  // Tells there is no immediate write now.  This triggers timer to
  // determine fallback to short record size mode.
  void start_tls_write_idle();

  ssize_t write_clear(const void *data, size_t len);
  ssize_t writev_clear(struct iovec *iov, int iovcnt);
  ssize_t read_clear(void *data, size_t len);

  void handle_tls_pending_read();

  void set_ssl(SSL *ssl);

  TLSConnection tls;
  ev_io wev;
  ev_io rev;
  ev_timer wt;
  ev_timer rt;
  RateLimit wlimit;
  RateLimit rlimit;
  IOCb writecb;
  IOCb readcb;
  TimerCb timeoutcb;
  struct ev_loop *loop;
  void *data;
  int fd;
  size_t tls_dyn_rec_warmup_threshold;
  ev_tstamp tls_dyn_rec_idle_timeout;
};

} // namespace shrpx

#endif // SHRPX_CONNECTION_H
