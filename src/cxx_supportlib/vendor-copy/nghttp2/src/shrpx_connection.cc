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
#include "shrpx_connection.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H

#include <limits>

#include <openssl/err.h>

#include "shrpx_ssl.h"
#include "shrpx_memcached_request.h"
#include "memchunk.h"
#include "util.h"

using namespace nghttp2;

namespace shrpx {
Connection::Connection(struct ev_loop *loop, int fd, SSL *ssl,
                       MemchunkPool *mcpool, ev_tstamp write_timeout,
                       ev_tstamp read_timeout,
                       const RateLimitConfig &write_limit,
                       const RateLimitConfig &read_limit, IOCb writecb,
                       IOCb readcb, TimerCb timeoutcb, void *data,
                       size_t tls_dyn_rec_warmup_threshold,
                       ev_tstamp tls_dyn_rec_idle_timeout)
    : tls{DefaultMemchunks(mcpool), DefaultPeekMemchunks(mcpool)},
      wlimit(loop, &wev, write_limit.rate, write_limit.burst),
      rlimit(loop, &rev, read_limit.rate, read_limit.burst, this),
      writecb(writecb), readcb(readcb), timeoutcb(timeoutcb), loop(loop),
      data(data), fd(fd),
      tls_dyn_rec_warmup_threshold(tls_dyn_rec_warmup_threshold),
      tls_dyn_rec_idle_timeout(tls_dyn_rec_idle_timeout) {

  ev_io_init(&wev, writecb, fd, EV_WRITE);
  ev_io_init(&rev, readcb, fd, EV_READ);

  wev.data = this;
  rev.data = this;

  ev_timer_init(&wt, timeoutcb, 0., write_timeout);
  ev_timer_init(&rt, timeoutcb, 0., read_timeout);

  wt.data = this;
  rt.data = this;

  // set 0. to double field explicitly just in case
  tls.last_write_idle = 0.;

  if (ssl) {
    set_ssl(ssl);
  }
}

Connection::~Connection() {
  disconnect();

  if (tls.ssl) {
    SSL_free(tls.ssl);
  }
}

void Connection::disconnect() {
  if (tls.ssl) {
    SSL_set_shutdown(tls.ssl, SSL_RECEIVED_SHUTDOWN);
    ERR_clear_error();

    if (tls.cached_session) {
      SSL_SESSION_free(tls.cached_session);
      tls.cached_session = nullptr;
    }

    if (tls.cached_session_lookup_req) {
      tls.cached_session_lookup_req->canceled = true;
      tls.cached_session_lookup_req = nullptr;
    }

    // To reuse SSL/TLS session, we have to shutdown, and don't free
    // tls.ssl.
    if (SSL_shutdown(tls.ssl) != 1) {
      SSL_free(tls.ssl);
      tls.ssl = nullptr;
    }

    tls.wbuf.reset();
    tls.rbuf.reset();
    tls.last_write_idle = 0.;
    tls.warmup_writelen = 0;
    tls.last_writelen = 0;
    tls.last_readlen = 0;
    tls.handshake_state = 0;
    tls.initial_handshake_done = false;
    tls.reneg_started = false;
  }

  if (fd != -1) {
    shutdown(fd, SHUT_WR);
    close(fd);
    fd = -1;
  }

  // Stop watchers here because they could be activated in
  // SSL_shutdown().
  ev_timer_stop(loop, &rt);
  ev_timer_stop(loop, &wt);

  rlimit.stopw();
  wlimit.stopw();
}

void Connection::prepare_client_handshake() { SSL_set_connect_state(tls.ssl); }

void Connection::prepare_server_handshake() { SSL_set_accept_state(tls.ssl); }

// BIO implementation is inspired by openldap implementation:
// http://www.openldap.org/devel/cvsweb.cgi/~checkout~/libraries/libldap/tls_o.c
namespace {
int shrpx_bio_write(BIO *b, const char *buf, int len) {
  if (buf == nullptr || len <= 0) {
    return 0;
  }

  auto conn = static_cast<Connection *>(b->ptr);
  auto &wbuf = conn->tls.wbuf;

  BIO_clear_retry_flags(b);

  if (conn->tls.initial_handshake_done) {
    // After handshake finished, send |buf| of length |len| to the
    // socket directly.

    // Only when TLS session was prematurely ended before server sent
    // all handshake message, this condition is true.  This could be
    // alert from SSL_shutdown().  Since connection is already down,
    // just return error.
    if (wbuf.rleft()) {
      return -1;
    }
    auto nwrite = conn->write_clear(buf, len);
    if (nwrite < 0) {
      return -1;
    }

    if (nwrite == 0) {
      BIO_set_retry_write(b);
      return -1;
    }

    return nwrite;
  }

  wbuf.append(buf, len);

  return len;
}
} // namespace

namespace {
int shrpx_bio_read(BIO *b, char *buf, int len) {
  if (buf == nullptr || len <= 0) {
    return 0;
  }

  auto conn = static_cast<Connection *>(b->ptr);
  auto &rbuf = conn->tls.rbuf;

  BIO_clear_retry_flags(b);

  if (conn->tls.initial_handshake_done && rbuf.rleft() == 0) {
    auto nread = conn->read_clear(buf, len);
    if (nread < 0) {
      return -1;
    }
    if (nread == 0) {
      BIO_set_retry_read(b);
      return -1;
    }
    return nread;
  }

  if (rbuf.rleft() == 0) {
    BIO_set_retry_read(b);
    return -1;
  }

  return rbuf.remove(buf, len);
}
} // namespace

namespace {
int shrpx_bio_puts(BIO *b, const char *str) {
  return shrpx_bio_write(b, str, strlen(str));
}
} // namespace

namespace {
int shrpx_bio_gets(BIO *b, char *buf, int len) { return -1; }
} // namespace

namespace {
long shrpx_bio_ctrl(BIO *b, int cmd, long num, void *ptr) {
  switch (cmd) {
  case BIO_CTRL_FLUSH:
    return 1;
  }

  return 0;
}
} // namespace

namespace {
int shrpx_bio_create(BIO *b) {
  b->init = 1;
  b->num = 0;
  b->ptr = nullptr;
  b->flags = 0;
  return 1;
}
} // namespace

namespace {
int shrpx_bio_destroy(BIO *b) {
  if (b == nullptr) {
    return 0;
  }

  b->ptr = nullptr;
  b->init = 0;
  b->flags = 0;

  return 1;
}
} // namespace

namespace {
BIO_METHOD shrpx_bio_method = {
    BIO_TYPE_FD,    "nghttpx-bio",    shrpx_bio_write,
    shrpx_bio_read, shrpx_bio_puts,   shrpx_bio_gets,
    shrpx_bio_ctrl, shrpx_bio_create, shrpx_bio_destroy,
};
} // namespace

void Connection::set_ssl(SSL *ssl) {
  tls.ssl = ssl;
  auto bio = BIO_new(&shrpx_bio_method);
  bio->ptr = this;
  SSL_set_bio(tls.ssl, bio, bio);
  SSL_set_app_data(tls.ssl, this);
}

namespace {
// We should buffer at least full encrypted TLS record here.
// Theoretically, peer can send client hello in several TLS records,
// which could exeed this limit, but it is not portable, and we don't
// have to handle such exotic behaviour.
bool read_buffer_full(DefaultPeekMemchunks &rbuf) {
  return rbuf.rleft_buffered() >= 20_k;
}
} // namespace

int Connection::tls_handshake() {
  wlimit.stopw();
  ev_timer_stop(loop, &wt);

  if (ev_is_active(&rev)) {
    std::array<uint8_t, 8_k> buf;
    auto nread = read_clear(buf.data(), buf.size());
    if (nread < 0) {
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "tls: handshake read error";
      }
      return -1;
    }
    tls.rbuf.append(buf.data(), nread);
    if (read_buffer_full(tls.rbuf)) {
      rlimit.stopw();
    }
  }

  if (tls.initial_handshake_done) {
    return write_tls_pending_handshake();
  }

  switch (tls.handshake_state) {
  case TLS_CONN_WAIT_FOR_SESSION_CACHE:
    return SHRPX_ERR_INPROGRESS;
  case TLS_CONN_GOT_SESSION_CACHE: {
    // Use the same trick invented by @kazuho in h2o project.

    // Discard all outgoing data.
    tls.wbuf.reset();
    // Rewind buffered incoming data to replay client hello.
    tls.rbuf.disable_peek(false);

    auto ssl_ctx = SSL_get_SSL_CTX(tls.ssl);
    auto ssl_opts = SSL_get_options(tls.ssl);
    SSL_free(tls.ssl);

    auto ssl = ssl::create_ssl(ssl_ctx);
    if (!ssl) {
      return -1;
    }
    if (ssl_opts & SSL_OP_NO_TICKET) {
      SSL_set_options(ssl, SSL_OP_NO_TICKET);
    }

    set_ssl(ssl);

    SSL_set_accept_state(tls.ssl);

    tls.handshake_state = TLS_CONN_NORMAL;
    break;
  }
  case TLS_CONN_CANCEL_SESSION_CACHE:
    tls.handshake_state = TLS_CONN_NORMAL;
    break;
  }

  auto rv = SSL_do_handshake(tls.ssl);

  if (rv <= 0) {
    auto err = SSL_get_error(tls.ssl, rv);
    switch (err) {
    case SSL_ERROR_WANT_READ:
      if (read_buffer_full(tls.rbuf)) {
        if (LOG_ENABLED(INFO)) {
          LOG(INFO) << "tls: handshake message is too large";
        }
        return -1;
      }
      break;
    case SSL_ERROR_WANT_WRITE:
      break;
    case SSL_ERROR_SSL:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "tls: handshake libssl error: "
                  << ERR_error_string(ERR_get_error(), nullptr);
      }
      return SHRPX_ERR_NETWORK;
    default:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "tls: handshake libssl error " << err;
      }
      return SHRPX_ERR_NETWORK;
    }
  }

  if (tls.handshake_state == TLS_CONN_WAIT_FOR_SESSION_CACHE) {
    if (LOG_ENABLED(INFO)) {
      LOG(INFO) << "tls: handshake is still in progress";
    }
    return SHRPX_ERR_INPROGRESS;
  }

  // Don't send handshake data if handshake was completed in OpenSSL
  // routine.  We have to check HTTP/2 requirement if HTTP/2 was
  // negotiated before sending finished message to the peer.
  if (rv != 1 && tls.wbuf.rleft()) {
    // First write indicates that resumption stuff has done.
    if (tls.handshake_state != TLS_CONN_WRITE_STARTED) {
      tls.handshake_state = TLS_CONN_WRITE_STARTED;
      // If peek has already disabled, this is noop.
      tls.rbuf.disable_peek(true);
    }
    std::array<struct iovec, 4> iov;
    auto iovcnt = tls.wbuf.riovec(iov.data(), iov.size());
    auto nwrite = writev_clear(iov.data(), iovcnt);
    if (nwrite < 0) {
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "tls: handshake write error";
      }
      return -1;
    }
    tls.wbuf.drain(nwrite);

    if (tls.wbuf.rleft()) {
      wlimit.startw();
      ev_timer_again(loop, &wt);
    }
  }

  if (!read_buffer_full(tls.rbuf)) {
    // We may have stopped reading
    rlimit.startw();
  }

  if (rv != 1) {
    if (LOG_ENABLED(INFO)) {
      LOG(INFO) << "tls: handshake is still in progress";
    }
    return SHRPX_ERR_INPROGRESS;
  }

  // Handshake was done

  rv = check_http2_requirement();
  if (rv != 0) {
    return -1;
  }

  // Just in case
  tls.rbuf.disable_peek(true);

  tls.initial_handshake_done = true;

  return write_tls_pending_handshake();
}

int Connection::write_tls_pending_handshake() {
  // Send handshake data left in the buffer
  while (tls.wbuf.rleft()) {
    std::array<struct iovec, 4> iov;
    auto iovcnt = tls.wbuf.riovec(iov.data(), iov.size());
    auto nwrite = writev_clear(iov.data(), iovcnt);
    if (nwrite < 0) {
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "tls: handshake write error";
      }
      return -1;
    }
    if (nwrite == 0) {
      wlimit.startw();
      ev_timer_again(loop, &wt);

      return SHRPX_ERR_INPROGRESS;
    }
    tls.wbuf.drain(nwrite);
  }

  // We have to start read watcher, since later stage of code expects
  // this.
  rlimit.startw();

  // We may have whole request in tls.rbuf.  This means that we don't
  // get notified further read event.  This is especially true for
  // HTTP/1.1.
  handle_tls_pending_read();

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "SSL/TLS handshake completed";
    if (SSL_session_reused(tls.ssl)) {
      LOG(INFO) << "SSL/TLS session reused";
    }
  }

  return 0;
}

int Connection::check_http2_requirement() {
  const unsigned char *next_proto = nullptr;
  unsigned int next_proto_len;

  SSL_get0_next_proto_negotiated(tls.ssl, &next_proto, &next_proto_len);
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
  if (next_proto == nullptr) {
    SSL_get0_alpn_selected(tls.ssl, &next_proto, &next_proto_len);
  }
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L
  if (next_proto == nullptr ||
      !util::check_h2_is_selected(next_proto, next_proto_len)) {
    return 0;
  }
  if (!nghttp2::ssl::check_http2_requirement(tls.ssl)) {
    if (LOG_ENABLED(INFO)) {
      LOG(INFO) << "TLSv1.2 and/or black listed cipher suite was negotiated. "
                   "HTTP/2 must not be used.";
    }
    return -1;
  }

  return 0;
}

namespace {
const size_t SHRPX_SMALL_WRITE_LIMIT = 1300;
} // namespace

size_t Connection::get_tls_write_limit() {

  if (tls_dyn_rec_warmup_threshold == 0) {
    return std::numeric_limits<ssize_t>::max();
  }

  auto t = ev_now(loop);

  if (tls.last_write_idle >= 0. &&
      t - tls.last_write_idle > tls_dyn_rec_idle_timeout) {
    // Time out, use small record size
    tls.warmup_writelen = 0;
    return SHRPX_SMALL_WRITE_LIMIT;
  }

  if (tls.warmup_writelen >= tls_dyn_rec_warmup_threshold) {
    return std::numeric_limits<ssize_t>::max();
  }

  return SHRPX_SMALL_WRITE_LIMIT;
}

void Connection::update_tls_warmup_writelen(size_t n) {
  if (tls.warmup_writelen < tls_dyn_rec_warmup_threshold) {
    tls.warmup_writelen += n;
  }
}

void Connection::start_tls_write_idle() {
  if (tls.last_write_idle < 0.) {
    tls.last_write_idle = ev_now(loop);
  }
}

ssize_t Connection::write_tls(const void *data, size_t len) {
  // SSL_write requires the same arguments (buf pointer and its
  // length) on SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE.
  // get_write_limit() may return smaller length than previously
  // passed to SSL_write, which violates OpenSSL assumption.  To avoid
  // this, we keep last legnth passed to SSL_write to
  // tls.last_writelen if SSL_write indicated I/O blocking.
  if (tls.last_writelen == 0) {
    len = std::min(len, wlimit.avail());
    len = std::min(len, get_tls_write_limit());
    if (len == 0) {
      return 0;
    }
  } else {
    len = tls.last_writelen;
    tls.last_writelen = 0;
  }

  tls.last_write_idle = -1.;

  auto rv = SSL_write(tls.ssl, data, len);

  if (rv <= 0) {
    auto err = SSL_get_error(tls.ssl, rv);
    switch (err) {
    case SSL_ERROR_WANT_READ:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "Close connection due to TLS renegotiation";
      }
      return SHRPX_ERR_NETWORK;
    case SSL_ERROR_WANT_WRITE:
      tls.last_writelen = len;
      // starting write watcher and timer is done in write_clear via
      // bio.
      return 0;
    case SSL_ERROR_SSL:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "SSL_write: " << ERR_error_string(ERR_get_error(),
                                                       nullptr);
      }
      return SHRPX_ERR_NETWORK;
    default:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "SSL_write: SSL_get_error returned " << err;
      }
      return SHRPX_ERR_NETWORK;
    }
  }

  wlimit.drain(rv);

  update_tls_warmup_writelen(rv);

  return rv;
}

ssize_t Connection::read_tls(void *data, size_t len) {
  // SSL_read requires the same arguments (buf pointer and its
  // length) on SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE.
  // rlimit_.avail() or rlimit_.avail() may return different length
  // than the length previously passed to SSL_read, which violates
  // OpenSSL assumption.  To avoid this, we keep last legnth passed
  // to SSL_read to tls_last_readlen_ if SSL_read indicated I/O
  // blocking.
  if (tls.last_readlen == 0) {
    len = std::min(len, rlimit.avail());
    if (len == 0) {
      return 0;
    }
  } else {
    len = tls.last_readlen;
    tls.last_readlen = 0;
  }

  auto rv = SSL_read(tls.ssl, data, len);

  if (rv <= 0) {
    auto err = SSL_get_error(tls.ssl, rv);
    switch (err) {
    case SSL_ERROR_WANT_READ:
      tls.last_readlen = len;
      return 0;
    case SSL_ERROR_WANT_WRITE:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "Close connection due to TLS renegotiation";
      }
      return SHRPX_ERR_NETWORK;
    case SSL_ERROR_ZERO_RETURN:
      return SHRPX_ERR_EOF;
    case SSL_ERROR_SSL:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "SSL_read: " << ERR_error_string(ERR_get_error(), nullptr);
      }
      return SHRPX_ERR_NETWORK;
    default:
      if (LOG_ENABLED(INFO)) {
        LOG(INFO) << "SSL_read: SSL_get_error returned " << err;
      }
      return SHRPX_ERR_NETWORK;
    }
  }

  rlimit.drain(rv);

  return rv;
}

ssize_t Connection::write_clear(const void *data, size_t len) {
  len = std::min(len, wlimit.avail());
  if (len == 0) {
    return 0;
  }

  ssize_t nwrite;
  while ((nwrite = write(fd, data, len)) == -1 && errno == EINTR)
    ;
  if (nwrite == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      wlimit.startw();
      ev_timer_again(loop, &wt);
      return 0;
    }
    return SHRPX_ERR_NETWORK;
  }

  wlimit.drain(nwrite);

  return nwrite;
}

ssize_t Connection::writev_clear(struct iovec *iov, int iovcnt) {
  iovcnt = limit_iovec(iov, iovcnt, wlimit.avail());
  if (iovcnt == 0) {
    return 0;
  }

  ssize_t nwrite;
  while ((nwrite = writev(fd, iov, iovcnt)) == -1 && errno == EINTR)
    ;
  if (nwrite == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      wlimit.startw();
      ev_timer_again(loop, &wt);
      return 0;
    }
    return SHRPX_ERR_NETWORK;
  }

  wlimit.drain(nwrite);

  return nwrite;
}

ssize_t Connection::read_clear(void *data, size_t len) {
  len = std::min(len, rlimit.avail());
  if (len == 0) {
    return 0;
  }

  ssize_t nread;
  while ((nread = read(fd, data, len)) == -1 && errno == EINTR)
    ;
  if (nread == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    return SHRPX_ERR_NETWORK;
  }

  if (nread == 0) {
    return SHRPX_ERR_EOF;
  }

  rlimit.drain(nread);

  return nread;
}

void Connection::handle_tls_pending_read() {
  if (!ev_is_active(&rev)) {
    return;
  }
  rlimit.handle_tls_pending_read();
}

} // namespace shrpx
