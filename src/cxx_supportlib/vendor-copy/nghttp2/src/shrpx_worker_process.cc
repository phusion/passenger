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
#include "shrpx_worker_process.h"

#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H
#include <sys/resource.h>
#include <grp.h>

#include <cinttypes>
#include <cstdlib>

#include <openssl/rand.h>

#include <ev.h>

#include "shrpx_config.h"
#include "shrpx_connection_handler.h"
#include "shrpx_log_config.h"
#include "shrpx_worker.h"
#include "shrpx_accept_handler.h"
#include "shrpx_http2_upstream.h"
#include "shrpx_http2_session.h"
#include "shrpx_memcached_dispatcher.h"
#include "shrpx_memcached_request.h"
#include "shrpx_process.h"
#include "shrpx_ssl.h"
#include "util.h"
#include "app_helper.h"
#include "template.h"

using namespace nghttp2;

namespace shrpx {

namespace {
void drop_privileges(
#ifdef HAVE_NEVERBLEED
    neverbleed_t *nb
#endif // HAVE_NEVERBLEED
    ) {
  if (getuid() == 0 && get_config()->uid != 0) {
    if (initgroups(get_config()->user.get(), get_config()->gid) != 0) {
      auto error = errno;
      LOG(FATAL) << "Could not change supplementary groups: "
                 << strerror(error);
      exit(EXIT_FAILURE);
    }
    if (setgid(get_config()->gid) != 0) {
      auto error = errno;
      LOG(FATAL) << "Could not change gid: " << strerror(error);
      exit(EXIT_FAILURE);
    }
    if (setuid(get_config()->uid) != 0) {
      auto error = errno;
      LOG(FATAL) << "Could not change uid: " << strerror(error);
      exit(EXIT_FAILURE);
    }
    if (setuid(0) != -1) {
      LOG(FATAL) << "Still have root privileges?";
      exit(EXIT_FAILURE);
    }
#ifdef HAVE_NEVERBLEED
    if (nb) {
      neverbleed_setuidgid(nb, get_config()->user.get(), 1);
    }
#endif // HAVE_NEVERBLEED
  }
}
} // namespace

namespace {
void graceful_shutdown(ConnectionHandler *conn_handler) {
  if (conn_handler->get_graceful_shutdown()) {
    return;
  }

  LOG(NOTICE) << "Graceful shutdown signal received";

  conn_handler->set_graceful_shutdown(true);

  conn_handler->disable_acceptor();

  // After disabling accepting new connection, disptach incoming
  // connection in backlog.

  conn_handler->accept_pending_connection();

  conn_handler->graceful_shutdown_worker();

  if (get_config()->num_worker == 1) {
    if (conn_handler->get_single_worker()->get_worker_stat()->num_connections ==
        0) {
      ev_break(conn_handler->get_loop());
    }

    return;
  }
}
} // namespace

namespace {
void reopen_log(ConnectionHandler *conn_handler) {
  LOG(NOTICE) << "Reopening log files: worker process (thread main)";

  (void)reopen_log_files();
  redirect_stderr_to_errorlog();

  if (get_config()->num_worker > 1) {
    conn_handler->worker_reopen_log_files();
  }
}
} // namespace

namespace {
void ipc_readcb(struct ev_loop *loop, ev_io *w, int revents) {
  auto conn_handler = static_cast<ConnectionHandler *>(w->data);
  std::array<uint8_t, 1024> buf;
  ssize_t nread;
  while ((nread = read(w->fd, buf.data(), buf.size())) == -1 && errno == EINTR)
    ;
  if (nread == -1) {
    auto error = errno;
    LOG(ERROR) << "Failed to read data from ipc channel: errno=" << error;
    return;
  }

  if (nread == 0) {
    // IPC socket closed.  Perform immediate shutdown.
    LOG(FATAL) << "IPC socket is closed.  Perform immediate shutdown.";
    _Exit(EXIT_FAILURE);
  }

  for (ssize_t i = 0; i < nread; ++i) {
    switch (buf[i]) {
    case SHRPX_IPC_GRACEFUL_SHUTDOWN:
      graceful_shutdown(conn_handler);
      break;
    case SHRPX_IPC_REOPEN_LOG:
      reopen_log(conn_handler);
      break;
    }
  }
}
} // namespace

namespace {
int generate_ticket_key(TicketKey &ticket_key) {
  ticket_key.cipher = get_config()->tls.ticket.cipher;
  ticket_key.hmac = EVP_sha256();
  ticket_key.hmac_keylen = EVP_MD_size(ticket_key.hmac);

  assert(static_cast<size_t>(EVP_CIPHER_key_length(ticket_key.cipher)) <=
         ticket_key.data.enc_key.size());
  assert(ticket_key.hmac_keylen <= ticket_key.data.hmac_key.size());

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "enc_keylen=" << EVP_CIPHER_key_length(ticket_key.cipher)
              << ", hmac_keylen=" << ticket_key.hmac_keylen;
  }

  if (RAND_bytes(reinterpret_cast<unsigned char *>(&ticket_key.data),
                 sizeof(ticket_key.data)) == 0) {
    return -1;
  }

  return 0;
}
} // namespace

namespace {
void renew_ticket_key_cb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto conn_handler = static_cast<ConnectionHandler *>(w->data);
  const auto &old_ticket_keys = conn_handler->get_ticket_keys();

  auto ticket_keys = std::make_shared<TicketKeys>();
  LOG(NOTICE) << "Renew new ticket keys";

  // If old_ticket_keys is not empty, it should contain at least 2
  // keys: one for encryption, and last one for the next encryption
  // key but decryption only.  The keys in between are old keys and
  // decryption only.  The next key is provided to ensure to mitigate
  // possible problem when one worker encrypt new key, but one worker,
  // which did not take the that key yet, and cannot decrypt it.
  //
  // We keep keys for get_config()->tls_session_timeout seconds.  The
  // default is 12 hours.  Thus the maximum ticket vector size is 12.
  if (old_ticket_keys) {
    auto &old_keys = old_ticket_keys->keys;
    auto &new_keys = ticket_keys->keys;

    assert(!old_keys.empty());

    auto max_tickets =
        static_cast<size_t>(std::chrono::duration_cast<std::chrono::hours>(
                                get_config()->tls.session_timeout).count());

    new_keys.resize(std::min(max_tickets, old_keys.size() + 1));
    std::copy_n(std::begin(old_keys), new_keys.size() - 1,
                std::begin(new_keys) + 1);
  } else {
    ticket_keys->keys.resize(1);
  }

  auto &new_key = ticket_keys->keys[0];

  if (generate_ticket_key(new_key) != 0) {
    if (LOG_ENABLED(INFO)) {
      LOG(INFO) << "failed to generate ticket key";
    }
    conn_handler->set_ticket_keys(nullptr);
    conn_handler->set_ticket_keys_to_worker(nullptr);
    return;
  }

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "ticket keys generation done";
    assert(ticket_keys->keys.size() >= 1);
    LOG(INFO) << 0 << " enc+dec: "
              << util::format_hex(ticket_keys->keys[0].data.name);
    for (size_t i = 1; i < ticket_keys->keys.size(); ++i) {
      auto &key = ticket_keys->keys[i];
      LOG(INFO) << i << " dec: " << util::format_hex(key.data.name);
    }
  }

  conn_handler->set_ticket_keys(ticket_keys);
  conn_handler->set_ticket_keys_to_worker(ticket_keys);
}
} // namespace

namespace {
void memcached_get_ticket_key_cb(struct ev_loop *loop, ev_timer *w,
                                 int revents) {
  auto conn_handler = static_cast<ConnectionHandler *>(w->data);
  auto dispatcher = conn_handler->get_tls_ticket_key_memcached_dispatcher();

  auto req = make_unique<MemcachedRequest>();
  req->key = "nghttpx:tls-ticket-key";
  req->op = MEMCACHED_OP_GET;
  req->cb = [conn_handler, dispatcher, w](MemcachedRequest *req,
                                          MemcachedResult res) {
    switch (res.status_code) {
    case MEMCACHED_ERR_NO_ERROR:
      break;
    case MEMCACHED_ERR_EXT_NETWORK_ERROR:
      conn_handler->on_tls_ticket_key_network_error(w);
      return;
    default:
      conn_handler->on_tls_ticket_key_not_found(w);
      return;
    }

    // |version (4bytes)|len (2bytes)|key (variable length)|...
    // (len, key) pairs are repeated as necessary.

    auto &value = res.value;
    if (value.size() < 4) {
      LOG(WARN) << "Memcached: tls ticket key value is too small: got "
                << value.size();
      conn_handler->on_tls_ticket_key_not_found(w);
      return;
    }
    auto p = value.data();
    auto version = util::get_uint32(p);
    // Currently supported version is 1.
    if (version != 1) {
      LOG(WARN) << "Memcached: tls ticket key version: want 1, got " << version;
      conn_handler->on_tls_ticket_key_not_found(w);
      return;
    }

    auto end = p + value.size();
    p += 4;

    auto &ticketconf = get_config()->tls.ticket;

    size_t expectedlen;
    size_t enc_keylen;
    size_t hmac_keylen;
    if (ticketconf.cipher == EVP_aes_128_cbc()) {
      expectedlen = 48;
      enc_keylen = 16;
      hmac_keylen = 16;
    } else if (ticketconf.cipher == EVP_aes_256_cbc()) {
      expectedlen = 80;
      enc_keylen = 32;
      hmac_keylen = 32;
    } else {
      return;
    }

    auto ticket_keys = std::make_shared<TicketKeys>();

    for (; p != end;) {
      if (end - p < 2) {
        LOG(WARN) << "Memcached: tls ticket key data is too small";
        conn_handler->on_tls_ticket_key_not_found(w);
        return;
      }
      auto len = util::get_uint16(p);
      p += 2;
      if (len != expectedlen) {
        LOG(WARN) << "Memcached: wrong tls ticket key size: want "
                  << expectedlen << ", got " << len;
        conn_handler->on_tls_ticket_key_not_found(w);
        return;
      }
      if (p + len > end) {
        LOG(WARN) << "Memcached: too short tls ticket key payload: want " << len
                  << ", got " << (end - p);
        conn_handler->on_tls_ticket_key_not_found(w);
        return;
      }
      auto key = TicketKey();
      key.cipher = ticketconf.cipher;
      key.hmac = EVP_sha256();
      key.hmac_keylen = hmac_keylen;

      std::copy_n(p, key.data.name.size(), key.data.name.data());
      p += key.data.name.size();

      std::copy_n(p, enc_keylen, key.data.enc_key.data());
      p += enc_keylen;

      std::copy_n(p, hmac_keylen, key.data.hmac_key.data());
      p += hmac_keylen;

      ticket_keys->keys.push_back(std::move(key));
    }

    conn_handler->on_tls_ticket_key_get_success(ticket_keys, w);
  };

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "Memcached: tls ticket key get request sent";
  }

  dispatcher->add_request(std::move(req));
}

} // namespace

#ifdef HAVE_NEVERBLEED
namespace {
void nb_child_cb(struct ev_loop *loop, ev_child *w, int revents) {
  log_chld(w->rpid, w->rstatus, "neverbleed process");

  ev_child_stop(loop, w);

  LOG(FATAL) << "neverbleed process exitted; aborting now";

  _Exit(EXIT_FAILURE);
}
} // namespace
#endif // HAVE_NEVERBLEED

int worker_process_event_loop(WorkerProcessConfig *wpconf) {
  if (reopen_log_files() != 0) {
    LOG(FATAL) << "Failed to open log file";
    return -1;
  }

  auto loop = EV_DEFAULT;

  ConnectionHandler conn_handler(loop);

  if (wpconf->server_fd6 != -1) {
    conn_handler.set_acceptor6(
        make_unique<AcceptHandler>(wpconf->server_fd6, &conn_handler));
  }
  if (wpconf->server_fd != -1) {
    conn_handler.set_acceptor(
        make_unique<AcceptHandler>(wpconf->server_fd, &conn_handler));
  }

  auto &upstreamconf = get_config()->conn.upstream;

#ifdef HAVE_NEVERBLEED
  if (!upstreamconf.no_tls || ssl::downstream_tls_enabled()) {
    std::array<char, NEVERBLEED_ERRBUF_SIZE> errbuf;
    auto nb = make_unique<neverbleed_t>();
    if (neverbleed_init(nb.get(), errbuf.data()) != 0) {
      LOG(FATAL) << "neverbleed_init failed: " << errbuf.data();
      return -1;
    }

    LOG(NOTICE) << "neverbleed process [" << nb->daemon_pid << "] spawned";

    conn_handler.set_neverbleed(std::move(nb));
  }

  auto nb = conn_handler.get_neverbleed();

  ev_child nb_childev;
  if (nb) {
    ev_child_init(&nb_childev, nb_child_cb, nb->daemon_pid, 0);
    nb_childev.data = nullptr;
    ev_child_start(loop, &nb_childev);
  }

#endif // HAVE_NEVERBLEED

  ev_timer renew_ticket_key_timer;
  if (!upstreamconf.no_tls) {
    auto &ticketconf = get_config()->tls.ticket;

    if (ticketconf.memcached.host) {
      conn_handler.set_tls_ticket_key_memcached_dispatcher(
          make_unique<MemcachedDispatcher>(&ticketconf.memcached.addr, loop));

      ev_timer_init(&renew_ticket_key_timer, memcached_get_ticket_key_cb, 0.,
                    0.);
      renew_ticket_key_timer.data = &conn_handler;
      // Get first ticket keys.
      memcached_get_ticket_key_cb(loop, &renew_ticket_key_timer, 0);
    } else {
      bool auto_tls_ticket_key = true;
      if (!ticketconf.files.empty()) {
        if (!ticketconf.cipher_given) {
          LOG(WARN)
              << "It is strongly recommended to specify "
                 "--tls-ticket-key-cipher=aes-128-cbc (or "
                 "tls-ticket-key-cipher=aes-128-cbc in configuration file) "
                 "when --tls-ticket-key-file is used for the smooth "
                 "transition when the default value of --tls-ticket-key-cipher "
                 "becomes aes-256-cbc";
        }
        auto ticket_keys = read_tls_ticket_key_file(
            ticketconf.files, ticketconf.cipher, EVP_sha256());
        if (!ticket_keys) {
          LOG(WARN) << "Use internal session ticket key generator";
        } else {
          conn_handler.set_ticket_keys(std::move(ticket_keys));
          auto_tls_ticket_key = false;
        }
      }
      if (auto_tls_ticket_key) {
        // Generate new ticket key every 1hr.
        ev_timer_init(&renew_ticket_key_timer, renew_ticket_key_cb, 0., 1_h);
        renew_ticket_key_timer.data = &conn_handler;
        ev_timer_again(loop, &renew_ticket_key_timer);

        // Generate first session ticket key before running workers.
        renew_ticket_key_cb(loop, &renew_ticket_key_timer, 0);
      }
    }
  }

  int rv;

  if (get_config()->num_worker == 1) {
    rv = conn_handler.create_single_worker();
    if (rv != 0) {
      return -1;
    }
  } else {
#ifndef NOTHREADS
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);

    rv = pthread_sigmask(SIG_BLOCK, &set, nullptr);
    if (rv != 0) {
      LOG(ERROR) << "Blocking SIGCHLD failed: " << strerror(rv);
      return -1;
    }
#endif // !NOTHREADS

    rv = conn_handler.create_worker_thread(get_config()->num_worker);
    if (rv != 0) {
      return -1;
    }

#ifndef NOTHREADS
    rv = pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
    if (rv != 0) {
      LOG(ERROR) << "Unblocking SIGCHLD failed: " << strerror(rv);
      return -1;
    }
#endif // !NOTHREADS
  }

  drop_privileges(
#ifdef HAVE_NEVERBLEED
      nb
#endif // HAVE_NEVERBLEED
      );

  ev_io ipcev;
  ev_io_init(&ipcev, ipc_readcb, wpconf->ipc_fd, EV_READ);
  ipcev.data = &conn_handler;
  ev_io_start(loop, &ipcev);

  if (!upstreamconf.no_tls && !get_config()->tls.ocsp.disabled) {
    conn_handler.proceed_next_cert_ocsp();
  }

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "Entering event loop";
  }

  ev_run(loop, 0);

  conn_handler.cancel_ocsp_update();

#ifdef HAVE_NEVERBLEED
  if (nb && nb->daemon_pid != -1) {
    kill(nb->daemon_pid, SIGTERM);
  }
#endif // HAVE_NEVERBLEED

  return 0;
}

} // namespace shrpx
