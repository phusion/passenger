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
#include "shrpx_connection_handler.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H
#include <sys/types.h>
#include <sys/wait.h>

#include <cerrno>
#include <thread>
#include <random>

#include "shrpx_client_handler.h"
#include "shrpx_ssl.h"
#include "shrpx_worker.h"
#include "shrpx_config.h"
#include "shrpx_http2_session.h"
#include "shrpx_connect_blocker.h"
#include "shrpx_downstream_connection.h"
#include "shrpx_accept_handler.h"
#include "shrpx_memcached_dispatcher.h"
#include "shrpx_signal.h"
#include "util.h"
#include "template.h"

using namespace nghttp2;

namespace shrpx {

namespace {
void acceptor_disable_cb(struct ev_loop *loop, ev_timer *w, int revent) {
  auto h = static_cast<ConnectionHandler *>(w->data);

  // If we are in graceful shutdown period, we must not enable
  // acceptors again.
  if (h->get_graceful_shutdown()) {
    return;
  }

  h->enable_acceptor();
}
} // namespace

namespace {
void ocsp_cb(struct ev_loop *loop, ev_timer *w, int revent) {
  auto h = static_cast<ConnectionHandler *>(w->data);

  // If we are in graceful shutdown period, we won't do ocsp query.
  if (h->get_graceful_shutdown()) {
    return;
  }

  LOG(NOTICE) << "Start ocsp update";

  h->proceed_next_cert_ocsp();
}
} // namespace

namespace {
void ocsp_read_cb(struct ev_loop *loop, ev_io *w, int revent) {
  auto h = static_cast<ConnectionHandler *>(w->data);

  h->read_ocsp_chunk();
}
} // namespace

namespace {
void ocsp_chld_cb(struct ev_loop *loop, ev_child *w, int revent) {
  auto h = static_cast<ConnectionHandler *>(w->data);

  h->handle_ocsp_complete();
}
} // namespace

namespace {
void thread_join_async_cb(struct ev_loop *loop, ev_async *w, int revent) {
  ev_break(loop);
}
} // namespace

namespace {
std::random_device rd;
} // namespace

ConnectionHandler::ConnectionHandler(struct ev_loop *loop)
    : gen_(rd()), single_worker_(nullptr), loop_(loop),
      tls_ticket_key_memcached_get_retry_count_(0),
      tls_ticket_key_memcached_fail_count_(0), worker_round_robin_cnt_(0),
      graceful_shutdown_(false) {
  ev_timer_init(&disable_acceptor_timer_, acceptor_disable_cb, 0., 0.);
  disable_acceptor_timer_.data = this;

  ev_timer_init(&ocsp_timer_, ocsp_cb, 0., 0.);
  ocsp_timer_.data = this;

  ev_io_init(&ocsp_.rev, ocsp_read_cb, -1, EV_READ);
  ocsp_.rev.data = this;

  ev_async_init(&thread_join_asyncev_, thread_join_async_cb);

  ev_child_init(&ocsp_.chldev, ocsp_chld_cb, 0, 0);
  ocsp_.chldev.data = this;

  ocsp_.next = 0;
  ocsp_.fd = -1;

  reset_ocsp();
}

ConnectionHandler::~ConnectionHandler() {
  ev_child_stop(loop_, &ocsp_.chldev);
  ev_async_stop(loop_, &thread_join_asyncev_);
  ev_io_stop(loop_, &ocsp_.rev);
  ev_timer_stop(loop_, &ocsp_timer_);
  ev_timer_stop(loop_, &disable_acceptor_timer_);

  for (auto ssl_ctx : all_ssl_ctx_) {
    auto tls_ctx_data =
        static_cast<ssl::TLSContextData *>(SSL_CTX_get_app_data(ssl_ctx));
    if (tls_ctx_data) {
      delete tls_ctx_data;
    }
    SSL_CTX_free(ssl_ctx);
  }

  // Free workers before destroying ev_loop
  workers_.clear();

  for (auto loop : worker_loops_) {
    ev_loop_destroy(loop);
  }
}

void ConnectionHandler::set_ticket_keys_to_worker(
    const std::shared_ptr<TicketKeys> &ticket_keys) {
  for (auto &worker : workers_) {
    worker->set_ticket_keys(ticket_keys);
  }
}

void ConnectionHandler::worker_reopen_log_files() {
  WorkerEvent wev{};

  wev.type = REOPEN_LOG;

  for (auto &worker : workers_) {
    worker->send(wev);
  }
}

int ConnectionHandler::create_single_worker() {
  auto cert_tree = ssl::create_cert_lookup_tree();
  auto sv_ssl_ctx = ssl::setup_server_ssl_context(all_ssl_ctx_, cert_tree
#ifdef HAVE_NEVERBLEED
                                                  ,
                                                  nb_.get()
#endif // HAVE_NEVERBLEED
                                                      );
  auto cl_ssl_ctx = ssl::setup_client_ssl_context(
#ifdef HAVE_NEVERBLEED
      nb_.get()
#endif // HAVE_NEVERBLEED
          );

  if (cl_ssl_ctx) {
    all_ssl_ctx_.push_back(cl_ssl_ctx);
  }

  single_worker_ = make_unique<Worker>(loop_, sv_ssl_ctx, cl_ssl_ctx, cert_tree,
                                       ticket_keys_);
#ifdef HAVE_MRUBY
  if (single_worker_->create_mruby_context() != 0) {
    return -1;
  }
#endif // HAVE_MRUBY

  return 0;
}

int ConnectionHandler::create_worker_thread(size_t num) {
#ifndef NOTHREADS
  assert(workers_.size() == 0);

  auto cert_tree = ssl::create_cert_lookup_tree();
  auto sv_ssl_ctx = ssl::setup_server_ssl_context(all_ssl_ctx_, cert_tree
#ifdef HAVE_NEVERBLEED
                                                  ,
                                                  nb_.get()
#endif // HAVE_NEVERBLEED
                                                      );
  auto cl_ssl_ctx = ssl::setup_client_ssl_context(
#ifdef HAVE_NEVERBLEED
      nb_.get()
#endif // HAVE_NEVERBLEED
          );

  if (cl_ssl_ctx) {
    all_ssl_ctx_.push_back(cl_ssl_ctx);
  }

  for (size_t i = 0; i < num; ++i) {
    auto loop = ev_loop_new(0);

    auto worker = make_unique<Worker>(loop, sv_ssl_ctx, cl_ssl_ctx, cert_tree,
                                      ticket_keys_);
#ifdef HAVE_MRUBY
    if (worker->create_mruby_context() != 0) {
      return -1;
    }
#endif // HAVE_MRUBY

    workers_.push_back(std::move(worker));
    worker_loops_.push_back(loop);

    LLOG(NOTICE, this) << "Created worker thread #" << workers_.size() - 1;
  }

  for (auto &worker : workers_) {
    worker->run_async();
  }
#endif // NOTHREADS

  return 0;
}

void ConnectionHandler::join_worker() {
#ifndef NOTHREADS
  int n = 0;

  if (LOG_ENABLED(INFO)) {
    LLOG(INFO, this) << "Waiting for worker thread to join: n="
                     << workers_.size();
  }

  for (auto &worker : workers_) {
    worker->wait();
    if (LOG_ENABLED(INFO)) {
      LLOG(INFO, this) << "Thread #" << n << " joined";
    }
    ++n;
  }
#endif // NOTHREADS
}

void ConnectionHandler::graceful_shutdown_worker() {
  if (get_config()->num_worker == 1) {
    return;
  }

  WorkerEvent wev{};
  wev.type = GRACEFUL_SHUTDOWN;

  if (LOG_ENABLED(INFO)) {
    LLOG(INFO, this) << "Sending graceful shutdown signal to worker";
  }

  for (auto &worker : workers_) {
    worker->send(wev);
  }

#ifndef NOTHREADS
  ev_async_start(loop_, &thread_join_asyncev_);

  thread_join_fut_ = std::async(std::launch::async, [this]() {
    (void)reopen_log_files();
    join_worker();
    ev_async_send(get_loop(), &thread_join_asyncev_);
    delete log_config();
  });
#endif // NOTHREADS
}

int ConnectionHandler::handle_connection(int fd, sockaddr *addr, int addrlen) {
  if (LOG_ENABLED(INFO)) {
    LLOG(INFO, this) << "Accepted connection. fd=" << fd;
  }

  if (get_config()->num_worker == 1) {
    auto &upstreamconf = get_config()->conn.upstream;
    if (single_worker_->get_worker_stat()->num_connections >=
        upstreamconf.worker_connections) {

      if (LOG_ENABLED(INFO)) {
        LLOG(INFO, this) << "Too many connections >="
                         << upstreamconf.worker_connections;
      }

      close(fd);
      return -1;
    }

    auto client =
        ssl::accept_connection(single_worker_.get(), fd, addr, addrlen);
    if (!client) {
      LLOG(ERROR, this) << "ClientHandler creation failed";

      close(fd);
      return -1;
    }

    return 0;
  }

  size_t idx = worker_round_robin_cnt_ % workers_.size();
  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "Dispatch connection to worker #" << idx;
  }
  ++worker_round_robin_cnt_;
  WorkerEvent wev{};
  wev.type = NEW_CONNECTION;
  wev.client_fd = fd;
  memcpy(&wev.client_addr, addr, addrlen);
  wev.client_addrlen = addrlen;

  workers_[idx]->send(wev);

  return 0;
}

struct ev_loop *ConnectionHandler::get_loop() const {
  return loop_;
}

Worker *ConnectionHandler::get_single_worker() const {
  return single_worker_.get();
}

void ConnectionHandler::set_acceptor(std::unique_ptr<AcceptHandler> h) {
  acceptor_ = std::move(h);
}

AcceptHandler *ConnectionHandler::get_acceptor() const {
  return acceptor_.get();
}

void ConnectionHandler::set_acceptor6(std::unique_ptr<AcceptHandler> h) {
  acceptor6_ = std::move(h);
}

AcceptHandler *ConnectionHandler::get_acceptor6() const {
  return acceptor6_.get();
}

void ConnectionHandler::enable_acceptor() {
  if (acceptor_) {
    acceptor_->enable();
  }

  if (acceptor6_) {
    acceptor6_->enable();
  }
}

void ConnectionHandler::disable_acceptor() {
  if (acceptor_) {
    acceptor_->disable();
  }

  if (acceptor6_) {
    acceptor6_->disable();
  }
}

void ConnectionHandler::sleep_acceptor(ev_tstamp t) {
  if (t == 0. || ev_is_active(&disable_acceptor_timer_)) {
    return;
  }

  disable_acceptor();

  ev_timer_set(&disable_acceptor_timer_, t, 0.);
  ev_timer_start(loop_, &disable_acceptor_timer_);
}

void ConnectionHandler::accept_pending_connection() {
  if (acceptor_) {
    acceptor_->accept_connection();
  }
  if (acceptor6_) {
    acceptor6_->accept_connection();
  }
}

void ConnectionHandler::set_ticket_keys(
    std::shared_ptr<TicketKeys> ticket_keys) {
  ticket_keys_ = std::move(ticket_keys);
  if (single_worker_) {
    single_worker_->set_ticket_keys(ticket_keys_);
  }
}

const std::shared_ptr<TicketKeys> &ConnectionHandler::get_ticket_keys() const {
  return ticket_keys_;
}

void ConnectionHandler::set_graceful_shutdown(bool f) {
  graceful_shutdown_ = f;
  if (single_worker_) {
    single_worker_->set_graceful_shutdown(f);
  }
}

bool ConnectionHandler::get_graceful_shutdown() const {
  return graceful_shutdown_;
}

void ConnectionHandler::cancel_ocsp_update() {
  if (ocsp_.pid == 0) {
    return;
  }

  kill(ocsp_.pid, SIGTERM);
}

// inspired by h2o_read_command function from h2o project:
// https://github.com/h2o/h2o
int ConnectionHandler::start_ocsp_update(const char *cert_file) {
  int rv;
  int pfd[2];

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "Start ocsp update for " << cert_file;
  }

  assert(!ev_is_active(&ocsp_.rev));
  assert(!ev_is_active(&ocsp_.chldev));

  char *const argv[] = {
      const_cast<char *>(get_config()->tls.ocsp.fetch_ocsp_response_file.get()),
      const_cast<char *>(cert_file), nullptr};
  char *const envp[] = {nullptr};

#ifdef O_CLOEXEC
  if (pipe2(pfd, O_CLOEXEC) == -1) {
    return -1;
  }
#else  // !O_CLOEXEC
  if (pipe(pfd) == -1) {
    return -1;
  }
  util::make_socket_closeonexec(pfd[0]);
  util::make_socket_closeonexec(pfd[1]);
#endif // !O_CLOEXEC

  auto closer = defer([&pfd]() {
    if (pfd[0] != -1) {
      close(pfd[0]);
    }

    if (pfd[1] != -1) {
      close(pfd[1]);
    }
  });

  sigset_t oldset;

  rv = shrpx_signal_block_all(&oldset);
  if (rv != 0) {
    auto error = errno;
    LOG(ERROR) << "Blocking all signals failed: " << strerror(error);

    return -1;
  }

  auto pid = fork();

  if (pid == 0) {
    // child process
    shrpx_signal_unset_worker_proc_ign_handler();

    rv = shrpx_signal_unblock_all();
    if (rv != 0) {
      auto error = errno;
      LOG(FATAL) << "Unblocking all signals failed: " << strerror(error);

      _Exit(EXIT_FAILURE);
    }

    dup2(pfd[1], 1);
    close(pfd[0]);

    rv = execve(argv[0], argv, envp);
    if (rv == -1) {
      auto error = errno;
      LOG(ERROR) << "Could not execute ocsp query command: " << argv[0]
                 << ", execve() faild, errno=" << error;
      _Exit(EXIT_FAILURE);
    }
    // unreachable
  }

  // parent process
  if (pid == -1) {
    auto error = errno;
    LOG(ERROR) << "Could not execute ocsp query command for " << cert_file
               << ": " << argv[0] << ", fork() failed, errno=" << error;
  }

  rv = shrpx_signal_set(&oldset);
  if (rv != 0) {
    auto error = errno;
    LOG(FATAL) << "Restoring all signals failed: " << strerror(error);

    _Exit(EXIT_FAILURE);
  }

  if (pid == -1) {
    return -1;
  }

  close(pfd[1]);
  pfd[1] = -1;

  ocsp_.pid = pid;
  ocsp_.fd = pfd[0];
  pfd[0] = -1;

  util::make_socket_nonblocking(ocsp_.fd);
  ev_io_set(&ocsp_.rev, ocsp_.fd, EV_READ);
  ev_io_start(loop_, &ocsp_.rev);

  ev_child_set(&ocsp_.chldev, ocsp_.pid, 0);
  ev_child_start(loop_, &ocsp_.chldev);

  return 0;
}

void ConnectionHandler::read_ocsp_chunk() {
  std::array<uint8_t, 4_k> buf;
  for (;;) {
    ssize_t n;
    while ((n = read(ocsp_.fd, buf.data(), buf.size())) == -1 && errno == EINTR)
      ;

    if (n == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      auto error = errno;
      LOG(WARN) << "Reading from ocsp query command failed: errno=" << error;
      ocsp_.error = error;

      break;
    }

    if (n == 0) {
      break;
    }

    std::copy_n(std::begin(buf), n, std::back_inserter(ocsp_.resp));
  }

  ev_io_stop(loop_, &ocsp_.rev);
}

void ConnectionHandler::handle_ocsp_complete() {
  ev_io_stop(loop_, &ocsp_.rev);
  ev_child_stop(loop_, &ocsp_.chldev);

  assert(ocsp_.next < all_ssl_ctx_.size());

  auto ssl_ctx = all_ssl_ctx_[ocsp_.next];
  auto tls_ctx_data =
      static_cast<ssl::TLSContextData *>(SSL_CTX_get_app_data(ssl_ctx));

  auto rstatus = ocsp_.chldev.rstatus;
  auto status = WEXITSTATUS(rstatus);
  if (ocsp_.error || !WIFEXITED(rstatus) || status != 0) {
    LOG(WARN) << "ocsp query command for " << tls_ctx_data->cert_file
              << " failed: error=" << ocsp_.error << ", rstatus=" << rstatus
              << ", status=" << status;
    ++ocsp_.next;
    proceed_next_cert_ocsp();
    return;
  }

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "ocsp update for " << tls_ctx_data->cert_file
              << " finished successfully";
  }

#ifndef OPENSSL_IS_BORINGSSL
  {
    std::lock_guard<std::mutex> g(tls_ctx_data->mu);
    tls_ctx_data->ocsp_data =
        std::make_shared<std::vector<uint8_t>>(std::move(ocsp_.resp));
  }
#else  // OPENSSL_IS_BORINGSSL
  SSL_CTX_set_ocsp_response(ssl_ctx, ocsp_.resp.data(), ocsp_.resp.size());
#endif // OPENSSL_IS_BORINGSSL

  ++ocsp_.next;
  proceed_next_cert_ocsp();
}

void ConnectionHandler::reset_ocsp() {
  if (ocsp_.fd != -1) {
    close(ocsp_.fd);
  }

  ocsp_.fd = -1;
  ocsp_.pid = 0;
  ocsp_.error = 0;
  ocsp_.resp = std::vector<uint8_t>();
}

void ConnectionHandler::proceed_next_cert_ocsp() {
  for (;;) {
    reset_ocsp();
    if (ocsp_.next == all_ssl_ctx_.size()) {
      ocsp_.next = 0;
      // We have updated all ocsp response, and schedule next update.
      ev_timer_set(&ocsp_timer_, get_config()->tls.ocsp.update_interval, 0.);
      ev_timer_start(loop_, &ocsp_timer_);
      return;
    }

    auto ssl_ctx = all_ssl_ctx_[ocsp_.next];
    auto tls_ctx_data =
        static_cast<ssl::TLSContextData *>(SSL_CTX_get_app_data(ssl_ctx));

    // client SSL_CTX is also included in all_ssl_ctx_, but has no
    // tls_ctx_data.
    if (!tls_ctx_data) {
      ++ocsp_.next;
      continue;
    }

    auto cert_file = tls_ctx_data->cert_file;

    if (start_ocsp_update(cert_file) != 0) {
      ++ocsp_.next;
      continue;
    }

    break;
  }
}

void ConnectionHandler::set_tls_ticket_key_memcached_dispatcher(
    std::unique_ptr<MemcachedDispatcher> dispatcher) {
  tls_ticket_key_memcached_dispatcher_ = std::move(dispatcher);
}

MemcachedDispatcher *
ConnectionHandler::get_tls_ticket_key_memcached_dispatcher() const {
  return tls_ticket_key_memcached_dispatcher_.get();
}

void ConnectionHandler::on_tls_ticket_key_network_error(ev_timer *w) {
  if (++tls_ticket_key_memcached_get_retry_count_ >=
      get_config()->tls.ticket.memcached.max_retry) {
    LOG(WARN) << "Memcached: tls ticket get retry all failed "
              << tls_ticket_key_memcached_get_retry_count_ << " times.";

    on_tls_ticket_key_not_found(w);
    return;
  }

  auto dist = std::uniform_int_distribution<int>(
      1, std::min(60, 1 << tls_ticket_key_memcached_get_retry_count_));
  auto t = dist(gen_);

  LOG(WARN)
      << "Memcached: tls ticket get failed due to network error, retrying in "
      << t << " seconds";

  ev_timer_set(w, t, 0.);
  ev_timer_start(loop_, w);
}

void ConnectionHandler::on_tls_ticket_key_not_found(ev_timer *w) {
  tls_ticket_key_memcached_get_retry_count_ = 0;

  if (++tls_ticket_key_memcached_fail_count_ >=
      get_config()->tls.ticket.memcached.max_fail) {
    LOG(WARN) << "Memcached: could not get tls ticket; disable tls ticket";

    tls_ticket_key_memcached_fail_count_ = 0;

    set_ticket_keys(nullptr);
    set_ticket_keys_to_worker(nullptr);
  }

  LOG(WARN) << "Memcached: tls ticket get failed, schedule next";
  schedule_next_tls_ticket_key_memcached_get(w);
}

void ConnectionHandler::on_tls_ticket_key_get_success(
    const std::shared_ptr<TicketKeys> &ticket_keys, ev_timer *w) {
  LOG(NOTICE) << "Memcached: tls ticket get success";

  tls_ticket_key_memcached_get_retry_count_ = 0;
  tls_ticket_key_memcached_fail_count_ = 0;

  schedule_next_tls_ticket_key_memcached_get(w);

  if (!ticket_keys || ticket_keys->keys.empty()) {
    LOG(WARN) << "Memcached: tls ticket keys are empty; tls ticket disabled";
    set_ticket_keys(nullptr);
    set_ticket_keys_to_worker(nullptr);
    return;
  }

  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "ticket keys get done";
    LOG(INFO) << 0 << " enc+dec: "
              << util::format_hex(ticket_keys->keys[0].data.name);
    for (size_t i = 1; i < ticket_keys->keys.size(); ++i) {
      auto &key = ticket_keys->keys[i];
      LOG(INFO) << i << " dec: " << util::format_hex(key.data.name);
    }
  }

  set_ticket_keys(ticket_keys);
  set_ticket_keys_to_worker(ticket_keys);
}

void ConnectionHandler::schedule_next_tls_ticket_key_memcached_get(
    ev_timer *w) {
  ev_timer_set(w, get_config()->tls.ticket.memcached.interval, 0.);
  ev_timer_start(loop_, w);
}

#ifdef HAVE_NEVERBLEED
void ConnectionHandler::set_neverbleed(std::unique_ptr<neverbleed_t> nb) {
  nb_ = std::move(nb);
}

neverbleed_t *ConnectionHandler::get_neverbleed() const { return nb_.get(); }

#endif // HAVE_NEVERBLEED

} // namespace shrpx
