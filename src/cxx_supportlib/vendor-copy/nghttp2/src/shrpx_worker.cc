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
#include "shrpx_worker.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H

#include <memory>

#include "shrpx_ssl.h"
#include "shrpx_log.h"
#include "shrpx_client_handler.h"
#include "shrpx_http2_session.h"
#include "shrpx_log_config.h"
#include "shrpx_connect_blocker.h"
#include "shrpx_memcached_dispatcher.h"
#ifdef HAVE_MRUBY
#include "shrpx_mruby.h"
#endif // HAVE_MRUBY
#include "util.h"
#include "template.h"

namespace shrpx {

namespace {
void eventcb(struct ev_loop *loop, ev_async *w, int revents) {
  auto worker = static_cast<Worker *>(w->data);
  worker->process_events();
}
} // namespace

namespace {
void mcpool_clear_cb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto worker = static_cast<Worker *>(w->data);
  if (worker->get_worker_stat()->num_connections != 0) {
    return;
  }
  worker->get_mcpool()->clear();
}
} // namespace

namespace {
std::random_device rd;
} // namespace

Worker::Worker(struct ev_loop *loop, SSL_CTX *sv_ssl_ctx, SSL_CTX *cl_ssl_ctx,
               ssl::CertLookupTree *cert_tree,
               const std::shared_ptr<TicketKeys> &ticket_keys)
    : randgen_(rd()),
      dconn_pool_(get_config()->conn.downstream.addr_groups.size()),
      worker_stat_(get_config()->conn.downstream.addr_groups.size()),
      dgrps_(get_config()->conn.downstream.addr_groups.size()), loop_(loop),
      sv_ssl_ctx_(sv_ssl_ctx), cl_ssl_ctx_(cl_ssl_ctx), cert_tree_(cert_tree),
      ticket_keys_(ticket_keys),
      connect_blocker_(make_unique<ConnectBlocker>(loop_)),
      graceful_shutdown_(false) {
  ev_async_init(&w_, eventcb);
  w_.data = this;
  ev_async_start(loop_, &w_);

  ev_timer_init(&mcpool_clear_timer_, mcpool_clear_cb, 0., 0.);
  mcpool_clear_timer_.data = this;

  auto &session_cacheconf = get_config()->tls.session_cache;

  if (session_cacheconf.memcached.host) {
    session_cache_memcached_dispatcher_ = make_unique<MemcachedDispatcher>(
        &session_cacheconf.memcached.addr, loop);
  }

  auto &downstreamconf = get_config()->conn.downstream;

  if (downstreamconf.proto == PROTO_HTTP2) {
    auto n = get_config()->http2.downstream.connections_per_worker;
    size_t group = 0;
    for (auto &dgrp : dgrps_) {
      auto m = n;
      if (m == 0) {
        m = downstreamconf.addr_groups[group].addrs.size();
      }
      for (size_t idx = 0; idx < m; ++idx) {
        dgrp.http2sessions.push_back(make_unique<Http2Session>(
            loop_, cl_ssl_ctx, connect_blocker_.get(), this, group, idx));
      }
      ++group;
    }
  }
}

Worker::~Worker() {
  ev_async_stop(loop_, &w_);
  ev_timer_stop(loop_, &mcpool_clear_timer_);
}

void Worker::schedule_clear_mcpool() {
  // libev manual says: "If the watcher is already active nothing will
  // happen."  Since we don't change any timeout here, we don't have
  // to worry about querying ev_is_active.
  ev_timer_start(loop_, &mcpool_clear_timer_);
}

void Worker::wait() {
#ifndef NOTHREADS
  fut_.get();
#endif // !NOTHREADS
}

void Worker::run_async() {
#ifndef NOTHREADS
  fut_ = std::async(std::launch::async, [this] {
    (void)reopen_log_files();
    ev_run(loop_);
    delete log_config();
  });
#endif // !NOTHREADS
}

void Worker::send(const WorkerEvent &event) {
  {
    std::lock_guard<std::mutex> g(m_);

    q_.push_back(event);
  }

  ev_async_send(loop_, &w_);
}

void Worker::process_events() {
  std::vector<WorkerEvent> q;
  {
    std::lock_guard<std::mutex> g(m_);
    q.swap(q_);
  }

  auto worker_connections = get_config()->conn.upstream.worker_connections;

  for (auto &wev : q) {
    switch (wev.type) {
    case NEW_CONNECTION: {
      if (LOG_ENABLED(INFO)) {
        WLOG(INFO, this) << "WorkerEvent: client_fd=" << wev.client_fd
                         << ", addrlen=" << wev.client_addrlen;
      }

      if (worker_stat_.num_connections >= worker_connections) {

        if (LOG_ENABLED(INFO)) {
          WLOG(INFO, this) << "Too many connections >= " << worker_connections;
        }

        close(wev.client_fd);

        break;
      }

      auto client_handler = ssl::accept_connection(
          this, wev.client_fd, &wev.client_addr.sa, wev.client_addrlen);
      if (!client_handler) {
        if (LOG_ENABLED(INFO)) {
          WLOG(ERROR, this) << "ClientHandler creation failed";
        }
        close(wev.client_fd);
        break;
      }

      if (LOG_ENABLED(INFO)) {
        WLOG(INFO, this) << "CLIENT_HANDLER:" << client_handler << " created ";
      }

      break;
    }
    case REOPEN_LOG:
      WLOG(NOTICE, this) << "Reopening log files: worker process (thread "
                         << this << ")";

      reopen_log_files();

      break;
    case GRACEFUL_SHUTDOWN:
      WLOG(NOTICE, this) << "Graceful shutdown commencing";

      graceful_shutdown_ = true;

      if (worker_stat_.num_connections == 0) {
        ev_break(loop_);

        return;
      }

      break;
    default:
      if (LOG_ENABLED(INFO)) {
        WLOG(INFO, this) << "unknown event type " << wev.type;
      }
    }
  }
}

ssl::CertLookupTree *Worker::get_cert_lookup_tree() const { return cert_tree_; }

std::shared_ptr<TicketKeys> Worker::get_ticket_keys() {
  std::lock_guard<std::mutex> g(m_);
  return ticket_keys_;
}

void Worker::set_ticket_keys(std::shared_ptr<TicketKeys> ticket_keys) {
  std::lock_guard<std::mutex> g(m_);
  ticket_keys_ = std::move(ticket_keys);
}

WorkerStat *Worker::get_worker_stat() { return &worker_stat_; }

DownstreamConnectionPool *Worker::get_dconn_pool() { return &dconn_pool_; }

Http2Session *Worker::next_http2_session(size_t group) {
  auto &dgrp = dgrps_[group];
  auto &http2sessions = dgrp.http2sessions;
  if (http2sessions.empty()) {
    return nullptr;
  }

  auto res = http2sessions[dgrp.next_http2session].get();
  ++dgrp.next_http2session;
  if (dgrp.next_http2session >= http2sessions.size()) {
    dgrp.next_http2session = 0;
  }

  return res;
}

ConnectBlocker *Worker::get_connect_blocker() const {
  return connect_blocker_.get();
}

struct ev_loop *Worker::get_loop() const {
  return loop_;
}

SSL_CTX *Worker::get_sv_ssl_ctx() const { return sv_ssl_ctx_; }

SSL_CTX *Worker::get_cl_ssl_ctx() const { return cl_ssl_ctx_; }

void Worker::set_graceful_shutdown(bool f) { graceful_shutdown_ = f; }

bool Worker::get_graceful_shutdown() const { return graceful_shutdown_; }

MemchunkPool *Worker::get_mcpool() { return &mcpool_; }

DownstreamGroup *Worker::get_dgrp(size_t group) {
  assert(group < dgrps_.size());
  return &dgrps_[group];
}

MemcachedDispatcher *Worker::get_session_cache_memcached_dispatcher() {
  return session_cache_memcached_dispatcher_.get();
}

std::mt19937 &Worker::get_randgen() { return randgen_; }

#ifdef HAVE_MRUBY
int Worker::create_mruby_context() {
  auto mruby_file = get_config()->mruby_file.get();
  mruby_ctx_ = mruby::create_mruby_context(mruby_file);
  if (!mruby_ctx_) {
    return -1;
  }

  return 0;
}

mruby::MRubyContext *Worker::get_mruby_context() const {
  return mruby_ctx_.get();
}
#endif // HAVE_MRUBY

} // namespace shrpx
