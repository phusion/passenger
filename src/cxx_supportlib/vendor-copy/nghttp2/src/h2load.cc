/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
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
#include "h2load.h"

#include <getopt.h>
#include <signal.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif // HAVE_NETINET_IN_H
#include <netinet/tcp.h>
#include <sys/stat.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif // HAVE_FCNTL_H

#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <thread>
#include <future>
#include <random>

#ifdef HAVE_SPDYLAY
#include <spdylay/spdylay.h>
#endif // HAVE_SPDYLAY

#include <openssl/err.h>

#include "http-parser/http_parser.h"

#include "h2load_http1_session.h"
#include "h2load_http2_session.h"
#ifdef HAVE_SPDYLAY
#include "h2load_spdy_session.h"
#endif // HAVE_SPDYLAY
#include "ssl.h"
#include "http2.h"
#include "util.h"
#include "template.h"

#ifndef O_BINARY
#define O_BINARY (0)
#endif // O_BINARY

using namespace nghttp2;

namespace h2load {

namespace {
bool recorded(const std::chrono::steady_clock::time_point &t) {
  return std::chrono::steady_clock::duration::zero() != t.time_since_epoch();
}
} // namespace

Config::Config()
    : data_length(-1), addrs(nullptr), nreqs(1), nclients(1), nthreads(1),
      max_concurrent_streams(-1), window_bits(30), connection_window_bits(30),
      rate(0), rate_period(1.0), conn_active_timeout(0.),
      conn_inactivity_timeout(0.), no_tls_proto(PROTO_HTTP2), data_fd(-1),
      port(0), default_port(0), verbose(false), timing_script(false) {}

Config::~Config() {
  if (base_uri_unix) {
    delete addrs;
  } else {
    freeaddrinfo(addrs);
  }

  if (data_fd != -1) {
    close(data_fd);
  }
}

bool Config::is_rate_mode() const { return (this->rate != 0); }
bool Config::has_base_uri() const { return (!this->base_uri.empty()); }
Config config;

namespace {
constexpr size_t MAX_SAMPLES = 1000000;
} // namespace

Stats::Stats(size_t req_todo, size_t nclients)
    : req_todo(req_todo), req_started(0), req_done(0), req_success(0),
      req_status_success(0), req_failed(0), req_error(0), req_timedout(0),
      bytes_total(0), bytes_head(0), bytes_head_decomp(0), bytes_body(0),
      status() {}

Stream::Stream() : req_stat{}, status_success(-1) {}

namespace {
std::random_device rd;
} // namespace

namespace {
std::mt19937 gen(rd());
} // namespace

namespace {
void sampling_init(Sampling &smp, size_t total, size_t max_samples) {
  smp.n = 0;

  if (total <= max_samples) {
    smp.interval = 0.;
    smp.point = 0.;
    return;
  }

  smp.interval = static_cast<double>(total) / max_samples;

  std::uniform_real_distribution<> dis(0., smp.interval);

  smp.point = dis(gen);
}
} // namespace

namespace {
bool sampling_should_pick(Sampling &smp) {
  return smp.interval == 0. || smp.n == ceil(smp.point);
}
} // namespace

namespace {
void sampling_advance_point(Sampling &smp) { smp.point += smp.interval; }
} // namespace

namespace {
void writecb(struct ev_loop *loop, ev_io *w, int revents) {
  auto client = static_cast<Client *>(w->data);
  client->restart_timeout();
  auto rv = client->do_write();
  if (rv == Client::ERR_CONNECT_FAIL) {
    client->disconnect();
    // Try next address
    client->current_addr = nullptr;
    rv = client->connect();
    if (rv != 0) {
      client->fail();
      delete client;
      return;
    }
    return;
  }
  if (rv != 0) {
    client->fail();
    delete client;
  }
}
} // namespace

namespace {
void readcb(struct ev_loop *loop, ev_io *w, int revents) {
  auto client = static_cast<Client *>(w->data);
  client->restart_timeout();
  if (client->do_read() != 0) {
    client->fail();
    delete client;
    return;
  }
  writecb(loop, &client->wev, revents);
  // client->disconnect() and client->fail() may be called
}
} // namespace

namespace {
// Called every rate_period when rate mode is being used
void rate_period_timeout_w_cb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto worker = static_cast<Worker *>(w->data);
  auto nclients_per_second = worker->rate;
  auto conns_remaining = worker->nclients - worker->nconns_made;
  auto nclients = std::min(nclients_per_second, conns_remaining);

  for (size_t i = 0; i < nclients; ++i) {
    auto req_todo = worker->nreqs_per_client;
    if (worker->nreqs_rem > 0) {
      ++req_todo;
      --worker->nreqs_rem;
    }
    auto client =
        make_unique<Client>(worker->next_client_id++, worker, req_todo);

    ++worker->nconns_made;

    if (client->connect() != 0) {
      std::cerr << "client could not connect to host" << std::endl;
      client->fail();
    } else {
      client.release();
    }
    worker->report_rate_progress();
  }
  if (worker->nconns_made >= worker->nclients) {
    ev_timer_stop(worker->loop, w);
  }
}
} // namespace

namespace {
// Called when an a connection has been inactive for a set period of time
// or a fixed amount of time after all requests have been made on a
// connection
void conn_timeout_cb(EV_P_ ev_timer *w, int revents) {
  auto client = static_cast<Client *>(w->data);

  ev_timer_stop(client->worker->loop, &client->conn_inactivity_watcher);
  ev_timer_stop(client->worker->loop, &client->conn_active_watcher);

  if (util::check_socket_connected(client->fd)) {
    client->timeout();
  }
}
} // namespace

namespace {
bool check_stop_client_request_timeout(Client *client, ev_timer *w) {
  auto nreq = client->req_todo - client->req_started;

  if (nreq == 0 ||
      client->streams.size() >= (size_t)config.max_concurrent_streams) {
    // no more requests to make, stop timer
    ev_timer_stop(client->worker->loop, w);
    return true;
  }

  return false;
}
} // namespace

namespace {
void client_request_timeout_cb(struct ev_loop *loop, ev_timer *w, int revents) {
  auto client = static_cast<Client *>(w->data);

  if (client->submit_request() != 0) {
    ev_timer_stop(client->worker->loop, w);
    client->process_request_failure();
    return;
  }
  client->signal_write();

  if (check_stop_client_request_timeout(client, w)) {
    return;
  }

  ev_tstamp duration =
      config.timings[client->reqidx] - config.timings[client->reqidx - 1];

  while (duration < 1e-9) {
    if (client->submit_request() != 0) {
      ev_timer_stop(client->worker->loop, w);
      client->process_request_failure();
      return;
    }
    client->signal_write();
    if (check_stop_client_request_timeout(client, w)) {
      return;
    }

    duration =
        config.timings[client->reqidx] - config.timings[client->reqidx - 1];
  }

  client->request_timeout_watcher.repeat = duration;
  ev_timer_again(client->worker->loop, &client->request_timeout_watcher);
}
} // namespace

Client::Client(uint32_t id, Worker *worker, size_t req_todo)
    : cstat{}, worker(worker), ssl(nullptr), next_addr(config.addrs),
      current_addr(nullptr), reqidx(0), state(CLIENT_IDLE), req_todo(req_todo),
      req_started(0), req_done(0), id(id), fd(-1),
      new_connection_requested(false) {
  ev_io_init(&wev, writecb, 0, EV_WRITE);
  ev_io_init(&rev, readcb, 0, EV_READ);

  wev.data = this;
  rev.data = this;

  ev_timer_init(&conn_inactivity_watcher, conn_timeout_cb, 0.,
                worker->config->conn_inactivity_timeout);
  conn_inactivity_watcher.data = this;

  ev_timer_init(&conn_active_watcher, conn_timeout_cb,
                worker->config->conn_active_timeout, 0.);
  conn_active_watcher.data = this;

  ev_timer_init(&request_timeout_watcher, client_request_timeout_cb, 0., 0.);
  request_timeout_watcher.data = this;
}

Client::~Client() {
  disconnect();

  if (ssl) {
    SSL_free(ssl);
  }

  if (sampling_should_pick(worker->client_smp)) {
    sampling_advance_point(worker->client_smp);
    worker->sample_client_stat(&cstat);
  }
  ++worker->client_smp.n;
}

int Client::do_read() { return readfn(*this); }
int Client::do_write() { return writefn(*this); }

int Client::make_socket(addrinfo *addr) {
  fd = util::create_nonblock_socket(addr->ai_family);
  if (fd == -1) {
    return -1;
  }
  if (config.scheme == "https") {
    if (!ssl) {
      ssl = SSL_new(worker->ssl_ctx);
    }

    auto config = worker->config;

    if (!util::numeric_host(config->host.c_str())) {
      SSL_set_tlsext_host_name(ssl, config->host.c_str());
    }

    SSL_set_fd(ssl, fd);
    SSL_set_connect_state(ssl);
  }

  auto rv = ::connect(fd, addr->ai_addr, addr->ai_addrlen);
  if (rv != 0 && errno != EINPROGRESS) {
    if (ssl) {
      SSL_free(ssl);
      ssl = nullptr;
    }
    close(fd);
    fd = -1;
    return -1;
  }
  return 0;
}

int Client::connect() {
  int rv;

  record_client_start_time();
  clear_connect_times();
  record_connect_start_time();

  if (worker->config->conn_inactivity_timeout > 0.) {
    ev_timer_again(worker->loop, &conn_inactivity_watcher);
  }

  if (current_addr) {
    rv = make_socket(current_addr);
    if (rv == -1) {
      return -1;
    }
  } else {
    addrinfo *addr = nullptr;
    while (next_addr) {
      addr = next_addr;
      next_addr = next_addr->ai_next;
      rv = make_socket(addr);
      if (rv == 0) {
        break;
      }
    }

    if (fd == -1) {
      return -1;
    }

    assert(addr);

    current_addr = addr;
  }

  writefn = &Client::connected;

  ev_io_set(&rev, fd, EV_READ);
  ev_io_set(&wev, fd, EV_WRITE);

  ev_io_start(worker->loop, &wev);

  return 0;
}

void Client::timeout() {
  process_timedout_streams();

  disconnect();
}

void Client::restart_timeout() {
  if (worker->config->conn_inactivity_timeout > 0.) {
    ev_timer_again(worker->loop, &conn_inactivity_watcher);
  }
}

void Client::fail() {
  disconnect();

  if (new_connection_requested) {
    new_connection_requested = false;
    if (req_started < req_todo) {
      // At the moment, we don't have a facility to re-start request
      // already in in-flight.  Make them fail.
      auto req_abandoned = req_started - req_done;

      worker->stats.req_failed += req_abandoned;
      worker->stats.req_error += req_abandoned;
      worker->stats.req_done += req_abandoned;

      req_done = req_started;

      // Keep using current address
      if (connect() == 0) {
        return;
      }
      std::cerr << "client could not connect to host" << std::endl;
    }
  }

  process_abandoned_streams();
}

void Client::disconnect() {
  record_client_end_time();

  ev_timer_stop(worker->loop, &conn_inactivity_watcher);
  ev_timer_stop(worker->loop, &conn_active_watcher);
  ev_timer_stop(worker->loop, &request_timeout_watcher);
  streams.clear();
  session.reset();
  wb.reset();
  state = CLIENT_IDLE;
  ev_io_stop(worker->loop, &wev);
  ev_io_stop(worker->loop, &rev);
  if (ssl) {
    SSL_set_shutdown(ssl, SSL_RECEIVED_SHUTDOWN);
    ERR_clear_error();

    if (SSL_shutdown(ssl) != 1) {
      SSL_free(ssl);
      ssl = nullptr;
    }
  }
  if (fd != -1) {
    shutdown(fd, SHUT_WR);
    close(fd);
    fd = -1;
  }
}

int Client::submit_request() {
  ++worker->stats.req_started;
  if (session->submit_request() != 0) {
    return -1;
  }

  ++req_started;

  // if an active timeout is set and this is the last request to be submitted
  // on this connection, start the active timeout.
  if (worker->config->conn_active_timeout > 0. && req_started >= req_todo) {
    ev_timer_start(worker->loop, &conn_active_watcher);
  }

  return 0;
}

void Client::process_timedout_streams() {
  for (auto &req_stat : worker->stats.req_stats) {
    if (!req_stat.completed) {
      req_stat.stream_close_time = std::chrono::steady_clock::now();
    }
  }

  auto req_timed_out = req_todo - req_done;
  worker->stats.req_timedout += req_timed_out;

  process_abandoned_streams();
}

void Client::process_abandoned_streams() {
  auto req_abandoned = req_todo - req_done;

  worker->stats.req_failed += req_abandoned;
  worker->stats.req_error += req_abandoned;
  worker->stats.req_done += req_abandoned;

  req_done = req_todo;
}

void Client::process_request_failure() {
  auto req_abandoned = req_todo - req_started;

  worker->stats.req_failed += req_abandoned;
  worker->stats.req_error += req_abandoned;
  worker->stats.req_done += req_abandoned;

  req_done += req_abandoned;

  if (req_done == req_todo) {
    terminate_session();
    return;
  }
}

namespace {
void print_server_tmp_key(SSL *ssl) {
// libressl does not have SSL_get_server_tmp_key
#if OPENSSL_VERSION_NUMBER >= 0x10002000L && defined(SSL_get_server_tmp_key)
  EVP_PKEY *key;

  if (!SSL_get_server_tmp_key(ssl, &key)) {
    return;
  }

  auto key_del = defer(EVP_PKEY_free, key);

  std::cout << "Server Temp Key: ";

  switch (EVP_PKEY_id(key)) {
  case EVP_PKEY_RSA:
    std::cout << "RSA " << EVP_PKEY_bits(key) << " bits" << std::endl;
    break;
  case EVP_PKEY_DH:
    std::cout << "DH " << EVP_PKEY_bits(key) << " bits" << std::endl;
    break;
  case EVP_PKEY_EC: {
    auto ec = EVP_PKEY_get1_EC_KEY(key);
    auto ec_del = defer(EC_KEY_free, ec);
    auto nid = EC_GROUP_get_curve_name(EC_KEY_get0_group(ec));
    auto cname = EC_curve_nid2nist(nid);
    if (!cname) {
      cname = OBJ_nid2sn(nid);
    }

    std::cout << "ECDH " << cname << " " << EVP_PKEY_bits(key) << " bits"
              << std::endl;
    break;
  }
  }
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L
}
} // namespace

void Client::report_tls_info() {
  if (worker->id == 0 && !worker->tls_info_report_done) {
    worker->tls_info_report_done = true;
    auto cipher = SSL_get_current_cipher(ssl);
    std::cout << "TLS Protocol: " << ssl::get_tls_protocol(ssl) << "\n"
              << "Cipher: " << SSL_CIPHER_get_name(cipher) << std::endl;
    print_server_tmp_key(ssl);
  }
}

void Client::report_app_info() {
  if (worker->id == 0 && !worker->app_info_report_done) {
    worker->app_info_report_done = true;
    std::cout << "Application protocol: " << selected_proto << std::endl;
  }
}

void Client::terminate_session() {
  session->terminate();
  // http1 session needs writecb to tear down session.
  signal_write();
}

void Client::on_request(int32_t stream_id) { streams[stream_id] = Stream(); }

void Client::on_header(int32_t stream_id, const uint8_t *name, size_t namelen,
                       const uint8_t *value, size_t valuelen) {
  auto itr = streams.find(stream_id);
  if (itr == std::end(streams)) {
    return;
  }
  auto &stream = (*itr).second;
  if (stream.status_success == -1 && namelen == 7 &&
      util::streq_l(":status", name, namelen)) {
    int status = 0;
    for (size_t i = 0; i < valuelen; ++i) {
      if ('0' <= value[i] && value[i] <= '9') {
        status *= 10;
        status += value[i] - '0';
        if (status > 999) {
          stream.status_success = 0;
          return;
        }
      } else {
        break;
      }
    }

    if (status >= 200 && status < 300) {
      ++worker->stats.status[2];
      stream.status_success = 1;
    } else if (status < 400) {
      ++worker->stats.status[3];
      stream.status_success = 1;
    } else if (status < 600) {
      ++worker->stats.status[status / 100];
      stream.status_success = 0;
    } else {
      stream.status_success = 0;
    }
  }
}

void Client::on_status_code(int32_t stream_id, uint16_t status) {
  auto itr = streams.find(stream_id);
  if (itr == std::end(streams)) {
    return;
  }
  auto &stream = (*itr).second;

  if (status >= 200 && status < 300) {
    ++worker->stats.status[2];
    stream.status_success = 1;
  } else if (status < 400) {
    ++worker->stats.status[3];
    stream.status_success = 1;
  } else if (status < 600) {
    ++worker->stats.status[status / 100];
    stream.status_success = 0;
  } else {
    stream.status_success = 0;
  }
}

void Client::on_stream_close(int32_t stream_id, bool success, bool final) {
  auto req_stat = get_req_stat(stream_id);
  if (!req_stat) {
    return;
  }

  req_stat->stream_close_time = std::chrono::steady_clock::now();
  if (success) {
    req_stat->completed = true;
    ++worker->stats.req_success;
    ++cstat.req_success;

    if (streams[stream_id].status_success == 1) {
      ++worker->stats.req_status_success;
    } else {
      ++worker->stats.req_failed;
    }

    if (sampling_should_pick(worker->request_times_smp)) {
      sampling_advance_point(worker->request_times_smp);
      worker->sample_req_stat(req_stat);
    }

    // Count up in successful cases only
    ++worker->request_times_smp.n;
  } else {
    ++worker->stats.req_failed;
    ++worker->stats.req_error;
  }

  ++worker->stats.req_done;
  ++req_done;

  worker->report_progress();
  streams.erase(stream_id);
  if (req_done == req_todo) {
    terminate_session();
    return;
  }

  if (!config.timing_script && !final) {
    if (req_started < req_todo) {
      if (submit_request() != 0) {
        process_request_failure();
      }
      return;
    }
  }
}

RequestStat *Client::get_req_stat(int32_t stream_id) {
  auto it = streams.find(stream_id);
  if (it == std::end(streams)) {
    return nullptr;
  }

  return &(*it).second.req_stat;
}

int Client::connection_made() {
  if (ssl) {
    report_tls_info();

    const unsigned char *next_proto = nullptr;
    unsigned int next_proto_len;

    SSL_get0_next_proto_negotiated(ssl, &next_proto, &next_proto_len);
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
    if (next_proto == nullptr) {
      SSL_get0_alpn_selected(ssl, &next_proto, &next_proto_len);
    }
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L

    if (next_proto) {
      if (util::check_h2_is_selected(next_proto, next_proto_len)) {
        session = make_unique<Http2Session>(this);
      } else if (util::streq_l(NGHTTP2_H1_1, next_proto, next_proto_len)) {
        session = make_unique<Http1Session>(this);
      }
#ifdef HAVE_SPDYLAY
      else {
        auto spdy_version = spdylay_npn_get_version(next_proto, next_proto_len);
        if (spdy_version) {
          session = make_unique<SpdySession>(this, spdy_version);
        }
      }
#endif // HAVE_SPDYLAY

      // Just assign next_proto to selected_proto anyway to show the
      // negotiation result.
      selected_proto.assign(next_proto, next_proto + next_proto_len);
    } else {
      std::cout << "No protocol negotiated. Fallback behaviour may be activated"
                << std::endl;

      for (const auto &proto : config.npn_list) {
        if (std::equal(NGHTTP2_H1_1_ALPN,
                       NGHTTP2_H1_1_ALPN + str_size(NGHTTP2_H1_1_ALPN),
                       proto.c_str())) {
          std::cout
              << "Server does not support NPN/ALPN. Falling back to HTTP/1.1."
              << std::endl;
          session = make_unique<Http1Session>(this);
          selected_proto = NGHTTP2_H1_1;
          break;
        }
      }
    }

    if (!selected_proto.empty()) {
      report_app_info();
    }

    if (!session) {
      std::cout
          << "No supported protocol was negotiated. Supported protocols were:"
          << std::endl;
      for (const auto &proto : config.npn_list) {
        std::cout << proto.substr(1) << std::endl;
      }
      disconnect();
      return -1;
    }
  } else {
    switch (config.no_tls_proto) {
    case Config::PROTO_HTTP2:
      session = make_unique<Http2Session>(this);
      selected_proto = NGHTTP2_CLEARTEXT_PROTO_VERSION_ID;
      break;
    case Config::PROTO_HTTP1_1:
      session = make_unique<Http1Session>(this);
      selected_proto = NGHTTP2_H1_1;
      break;
#ifdef HAVE_SPDYLAY
    case Config::PROTO_SPDY2:
      session = make_unique<SpdySession>(this, SPDYLAY_PROTO_SPDY2);
      selected_proto = "spdy/2";
      break;
    case Config::PROTO_SPDY3:
      session = make_unique<SpdySession>(this, SPDYLAY_PROTO_SPDY3);
      selected_proto = "spdy/3";
      break;
    case Config::PROTO_SPDY3_1:
      session = make_unique<SpdySession>(this, SPDYLAY_PROTO_SPDY3_1);
      selected_proto = "spdy/3.1";
      break;
#endif // HAVE_SPDYLAY
    default:
      // unreachable
      assert(0);
    }

    report_app_info();
  }

  state = CLIENT_CONNECTED;

  session->on_connect();

  record_connect_time();

  if (!config.timing_script) {
    auto nreq =
        std::min(req_todo - req_started, (size_t)config.max_concurrent_streams);
    for (; nreq > 0; --nreq) {
      if (submit_request() != 0) {
        process_request_failure();
        break;
      }
    }
  } else {

    ev_tstamp duration = config.timings[reqidx];

    while (duration < 1e-9) {
      if (submit_request() != 0) {
        process_request_failure();
        break;
      }
      duration = config.timings[reqidx];
      if (reqidx == 0) {
        // if reqidx wraps around back to 0, we uses up all lines and
        // should break
        break;
      }
    }

    if (duration >= 1e-9) {
      // double check since we may have break due to reqidx wraps
      // around back to 0
      request_timeout_watcher.repeat = duration;
      ev_timer_again(worker->loop, &request_timeout_watcher);
    }
  }
  signal_write();

  return 0;
}

int Client::on_read(const uint8_t *data, size_t len) {
  auto rv = session->on_read(data, len);
  if (rv != 0) {
    return -1;
  }
  worker->stats.bytes_total += len;
  signal_write();
  return 0;
}

int Client::on_write() {
  if (session->on_write() != 0) {
    return -1;
  }
  return 0;
}

int Client::read_clear() {
  uint8_t buf[8_k];

  for (;;) {
    ssize_t nread;
    while ((nread = read(fd, buf, sizeof(buf))) == -1 && errno == EINTR)
      ;
    if (nread == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
      }
      return -1;
    }

    if (nread == 0) {
      return -1;
    }

    if (on_read(buf, nread) != 0) {
      return -1;
    }
  }

  return 0;
}

int Client::write_clear() {
  for (;;) {
    if (wb.rleft() > 0) {
      ssize_t nwrite;
      while ((nwrite = write(fd, wb.pos, wb.rleft())) == -1 && errno == EINTR)
        ;
      if (nwrite == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          ev_io_start(worker->loop, &wev);
          return 0;
        }
        return -1;
      }
      wb.drain(nwrite);
      continue;
    }
    wb.reset();
    if (on_write() != 0) {
      return -1;
    }
    if (wb.rleft() == 0) {
      break;
    }
  }

  ev_io_stop(worker->loop, &wev);

  return 0;
}

int Client::connected() {
  if (!util::check_socket_connected(fd)) {
    return ERR_CONNECT_FAIL;
  }
  ev_io_start(worker->loop, &rev);
  ev_io_stop(worker->loop, &wev);

  if (ssl) {
    readfn = &Client::tls_handshake;
    writefn = &Client::tls_handshake;

    return do_write();
  }

  readfn = &Client::read_clear;
  writefn = &Client::write_clear;

  if (connection_made() != 0) {
    return -1;
  }

  return 0;
}

int Client::tls_handshake() {
  ERR_clear_error();

  auto rv = SSL_do_handshake(ssl);

  if (rv <= 0) {
    auto err = SSL_get_error(ssl, rv);
    switch (err) {
    case SSL_ERROR_WANT_READ:
      ev_io_stop(worker->loop, &wev);
      return 0;
    case SSL_ERROR_WANT_WRITE:
      ev_io_start(worker->loop, &wev);
      return 0;
    default:
      return -1;
    }
  }

  ev_io_stop(worker->loop, &wev);

  readfn = &Client::read_tls;
  writefn = &Client::write_tls;

  if (connection_made() != 0) {
    return -1;
  }

  return 0;
}

int Client::read_tls() {
  uint8_t buf[8_k];

  ERR_clear_error();

  for (;;) {
    auto rv = SSL_read(ssl, buf, sizeof(buf));

    if (rv <= 0) {
      auto err = SSL_get_error(ssl, rv);
      switch (err) {
      case SSL_ERROR_WANT_READ:
        return 0;
      case SSL_ERROR_WANT_WRITE:
        // renegotiation started
        return -1;
      default:
        return -1;
      }
    }

    if (on_read(buf, rv) != 0) {
      return -1;
    }
  }
}

int Client::write_tls() {
  ERR_clear_error();

  for (;;) {
    if (wb.rleft() > 0) {
      auto rv = SSL_write(ssl, wb.pos, wb.rleft());

      if (rv <= 0) {
        auto err = SSL_get_error(ssl, rv);
        switch (err) {
        case SSL_ERROR_WANT_READ:
          // renegotiation started
          return -1;
        case SSL_ERROR_WANT_WRITE:
          ev_io_start(worker->loop, &wev);
          return 0;
        default:
          return -1;
        }
      }

      wb.drain(rv);

      continue;
    }
    wb.reset();
    if (on_write() != 0) {
      return -1;
    }
    if (wb.rleft() == 0) {
      break;
    }
  }

  ev_io_stop(worker->loop, &wev);

  return 0;
}

void Client::record_request_time(RequestStat *req_stat) {
  req_stat->request_time = std::chrono::steady_clock::now();
}

void Client::record_connect_start_time() {
  cstat.connect_start_time = std::chrono::steady_clock::now();
}

void Client::record_connect_time() {
  cstat.connect_time = std::chrono::steady_clock::now();
}

void Client::record_ttfb() {
  if (recorded(cstat.ttfb)) {
    return;
  }

  cstat.ttfb = std::chrono::steady_clock::now();
}

void Client::clear_connect_times() {
  cstat.connect_start_time = std::chrono::steady_clock::time_point();
  cstat.connect_time = std::chrono::steady_clock::time_point();
  cstat.ttfb = std::chrono::steady_clock::time_point();
}

void Client::record_client_start_time() {
  // Record start time only once at the very first connection is going
  // to be made.
  if (recorded(cstat.client_start_time)) {
    return;
  }

  cstat.client_start_time = std::chrono::steady_clock::now();
}

void Client::record_client_end_time() {
  // Unlike client_start_time, we overwrite client_end_time.  This
  // handles multiple connect/disconnect for HTTP/1.1 benchmark.
  cstat.client_end_time = std::chrono::steady_clock::now();
}

void Client::signal_write() { ev_io_start(worker->loop, &wev); }

void Client::try_new_connection() { new_connection_requested = true; }

Worker::Worker(uint32_t id, SSL_CTX *ssl_ctx, size_t req_todo, size_t nclients,
               size_t rate, size_t max_samples, Config *config)
    : stats(req_todo, nclients), loop(ev_loop_new(0)), ssl_ctx(ssl_ctx),
      config(config), id(id), tls_info_report_done(false),
      app_info_report_done(false), nconns_made(0), nclients(nclients),
      nreqs_per_client(req_todo / nclients), nreqs_rem(req_todo % nclients),
      rate(rate), max_samples(max_samples), next_client_id(0) {
  if (!config->is_rate_mode()) {
    progress_interval = std::max(static_cast<size_t>(1), req_todo / 10);
  } else {
    progress_interval = std::max(static_cast<size_t>(1), nclients / 10);
  }

  // create timer that will go off every rate_period
  ev_timer_init(&timeout_watcher, rate_period_timeout_w_cb, 0.,
                config->rate_period);
  timeout_watcher.data = this;

  stats.req_stats.reserve(std::min(req_todo, max_samples));
  stats.client_stats.reserve(std::min(nclients, max_samples));

  sampling_init(request_times_smp, req_todo, max_samples);
  sampling_init(client_smp, nclients, max_samples);
}

Worker::~Worker() {
  ev_timer_stop(loop, &timeout_watcher);
  ev_loop_destroy(loop);
}

void Worker::run() {
  if (!config->is_rate_mode()) {
    for (size_t i = 0; i < nclients; ++i) {
      auto req_todo = nreqs_per_client;
      if (nreqs_rem > 0) {
        ++req_todo;
        --nreqs_rem;
      }
      auto client = make_unique<Client>(next_client_id++, this, req_todo);
      if (client->connect() != 0) {
        std::cerr << "client could not connect to host" << std::endl;
        client->fail();
      } else {
        client.release();
      }
    }
  } else {
    ev_timer_again(loop, &timeout_watcher);

    // call callback so that we don't waste the first rate_period
    rate_period_timeout_w_cb(loop, &timeout_watcher, 0);
  }
  ev_run(loop, 0);
}

void Worker::sample_req_stat(RequestStat *req_stat) {
  stats.req_stats.push_back(*req_stat);
  assert(stats.req_stats.size() <= max_samples);
}

void Worker::sample_client_stat(ClientStat *cstat) {
  stats.client_stats.push_back(*cstat);
  assert(stats.client_stats.size() <= max_samples);
}

void Worker::report_progress() {
  if (id != 0 || config->is_rate_mode() || stats.req_done % progress_interval) {
    return;
  }

  std::cout << "progress: " << stats.req_done * 100 / stats.req_todo << "% done"
            << std::endl;
}

void Worker::report_rate_progress() {
  if (id != 0 || nconns_made % progress_interval) {
    return;
  }

  std::cout << "progress: " << nconns_made * 100 / nclients
            << "% of clients started" << std::endl;
}

namespace {
// Returns percentage of number of samples within mean +/- sd.
double within_sd(const std::vector<double> &samples, double mean, double sd) {
  if (samples.size() == 0) {
    return 0.0;
  }
  auto lower = mean - sd;
  auto upper = mean + sd;
  auto m = std::count_if(
      std::begin(samples), std::end(samples),
      [&lower, &upper](double t) { return lower <= t && t <= upper; });
  return (m / static_cast<double>(samples.size())) * 100;
}
} // namespace

namespace {
// Computes statistics using |samples|. The min, max, mean, sd, and
// percentage of number of samples within mean +/- sd are computed.
// If |sampling| is true, this computes sample variance.  Otherwise,
// population variance.
SDStat compute_time_stat(const std::vector<double> &samples,
                         bool sampling = false) {
  if (samples.empty()) {
    return {0.0, 0.0, 0.0, 0.0, 0.0};
  }
  // standard deviation calculated using Rapid calculation method:
  // https://en.wikipedia.org/wiki/Standard_deviation#Rapid_calculation_methods
  double a = 0, q = 0;
  size_t n = 0;
  double sum = 0;
  auto res = SDStat{std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::min()};
  for (const auto &t : samples) {
    ++n;
    res.min = std::min(res.min, t);
    res.max = std::max(res.max, t);
    sum += t;

    auto na = a + (t - a) / n;
    q += (t - a) * (t - na);
    a = na;
  }

  assert(n > 0);
  res.mean = sum / n;
  res.sd = sqrt(q / (sampling && n > 1 ? n - 1 : n));
  res.within_sd = within_sd(samples, res.mean, res.sd);

  return res;
}
} // namespace

namespace {
SDStats
process_time_stats(const std::vector<std::unique_ptr<Worker>> &workers) {
  auto request_times_sampling = false;
  auto client_times_sampling = false;
  size_t nrequest_times = 0;
  size_t nclient_times = 0;
  for (const auto &w : workers) {
    nrequest_times += w->stats.req_stats.size();
    if (w->request_times_smp.interval != 0.) {
      request_times_sampling = true;
    }

    nclient_times += w->stats.client_stats.size();
    if (w->client_smp.interval != 0.) {
      client_times_sampling = true;
    }
  }

  std::vector<double> request_times;
  request_times.reserve(nrequest_times);

  std::vector<double> connect_times, ttfb_times, rps_values;
  connect_times.reserve(nclient_times);
  ttfb_times.reserve(nclient_times);
  rps_values.reserve(nclient_times);

  for (const auto &w : workers) {
    for (const auto &req_stat : w->stats.req_stats) {
      if (!req_stat.completed) {
        continue;
      }
      request_times.push_back(
          std::chrono::duration_cast<std::chrono::duration<double>>(
              req_stat.stream_close_time - req_stat.request_time).count());
    }

    const auto &stat = w->stats;

    for (const auto &cstat : stat.client_stats) {
      if (recorded(cstat.client_start_time) &&
          recorded(cstat.client_end_time)) {
        auto t = std::chrono::duration_cast<std::chrono::duration<double>>(
                     cstat.client_end_time - cstat.client_start_time).count();
        if (t > 1e-9) {
          rps_values.push_back(cstat.req_success / t);
        }
      }

      // We will get connect event before FFTB.
      if (!recorded(cstat.connect_start_time) ||
          !recorded(cstat.connect_time)) {
        continue;
      }

      connect_times.push_back(
          std::chrono::duration_cast<std::chrono::duration<double>>(
              cstat.connect_time - cstat.connect_start_time).count());

      if (!recorded(cstat.ttfb)) {
        continue;
      }

      ttfb_times.push_back(
          std::chrono::duration_cast<std::chrono::duration<double>>(
              cstat.ttfb - cstat.connect_start_time).count());
    }
  }

  return {compute_time_stat(request_times, request_times_sampling),
          compute_time_stat(connect_times, client_times_sampling),
          compute_time_stat(ttfb_times, client_times_sampling),
          compute_time_stat(rps_values, client_times_sampling)};
}
} // namespace

namespace {
void resolve_host() {
  if (config.base_uri_unix) {
    auto res = make_unique<addrinfo>();
    res->ai_family = config.unix_addr.sun_family;
    res->ai_socktype = SOCK_STREAM;
    res->ai_addrlen = sizeof(config.unix_addr);
    res->ai_addr =
        static_cast<struct sockaddr *>(static_cast<void *>(&config.unix_addr));

    config.addrs = res.release();
    return;
  };

  int rv;
  addrinfo hints{}, *res;

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;

  rv = getaddrinfo(config.host.c_str(), util::utos(config.port).c_str(), &hints,
                   &res);
  if (rv != 0) {
    std::cerr << "getaddrinfo() failed: " << gai_strerror(rv) << std::endl;
    exit(EXIT_FAILURE);
  }
  if (res == nullptr) {
    std::cerr << "No address returned" << std::endl;
    exit(EXIT_FAILURE);
  }
  config.addrs = res;
}
} // namespace

namespace {
std::string get_reqline(const char *uri, const http_parser_url &u) {
  std::string reqline;

  if (util::has_uri_field(u, UF_PATH)) {
    reqline = util::get_uri_field(uri, u, UF_PATH);
  } else {
    reqline = "/";
  }

  if (util::has_uri_field(u, UF_QUERY)) {
    reqline += '?';
    reqline += util::get_uri_field(uri, u, UF_QUERY);
  }

  return reqline;
}
} // namespace

namespace {
int client_select_next_proto_cb(SSL *ssl, unsigned char **out,
                                unsigned char *outlen, const unsigned char *in,
                                unsigned int inlen, void *arg) {
  if (util::select_protocol(const_cast<const unsigned char **>(out), outlen, in,
                            inlen, config.npn_list)) {
    return SSL_TLSEXT_ERR_OK;
  }

  // OpenSSL will terminate handshake with fatal alert if we return
  // NOACK.  So there is no way to fallback.
  return SSL_TLSEXT_ERR_NOACK;
}
} // namespace

namespace {
constexpr char UNIX_PATH_PREFIX[] = "unix:";
} // namespace

namespace {
bool parse_base_uri(std::string base_uri) {
  http_parser_url u{};
  if (http_parser_parse_url(base_uri.c_str(), base_uri.size(), 0, &u) != 0 ||
      !util::has_uri_field(u, UF_SCHEMA) || !util::has_uri_field(u, UF_HOST)) {
    return false;
  }

  config.scheme = util::get_uri_field(base_uri.c_str(), u, UF_SCHEMA);
  config.host = util::get_uri_field(base_uri.c_str(), u, UF_HOST);
  config.default_port = util::get_default_port(base_uri.c_str(), u);
  if (util::has_uri_field(u, UF_PORT)) {
    config.port = u.port;
  } else {
    config.port = config.default_port;
  }

  return true;
}
} // namespace
namespace {
// Use std::vector<std::string>::iterator explicitly, without that,
// http_parser_url u{} fails with clang-3.4.
std::vector<std::string> parse_uris(std::vector<std::string>::iterator first,
                                    std::vector<std::string>::iterator last) {
  std::vector<std::string> reqlines;

  if (first == last) {
    std::cerr << "no URI available" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (!config.has_base_uri()) {

    if (!parse_base_uri(*first)) {
      std::cerr << "invalid URI: " << *first << std::endl;
      exit(EXIT_FAILURE);
    }

    config.base_uri = *first;
  }

  for (; first != last; ++first) {
    http_parser_url u{};

    auto uri = (*first).c_str();

    if (http_parser_parse_url(uri, (*first).size(), 0, &u) != 0) {
      std::cerr << "invalid URI: " << uri << std::endl;
      exit(EXIT_FAILURE);
    }

    reqlines.push_back(get_reqline(uri, u));
  }

  return reqlines;
}
} // namespace

namespace {
std::vector<std::string> read_uri_from_file(std::istream &infile) {
  std::vector<std::string> uris;
  std::string line_uri;
  while (std::getline(infile, line_uri)) {
    uris.push_back(line_uri);
  }

  return uris;
}
} // namespace

namespace {
void read_script_from_file(std::istream &infile,
                           std::vector<ev_tstamp> &timings,
                           std::vector<std::string> &uris) {
  std::string script_line;
  int line_count = 0;
  while (std::getline(infile, script_line)) {
    line_count++;
    if (script_line.empty()) {
      std::cerr << "Empty line detected at line " << line_count
                << ". Ignoring and continuing." << std::endl;
      continue;
    }

    std::size_t pos = script_line.find("\t");
    if (pos == std::string::npos) {
      std::cerr << "Invalid line format detected, no tab character at line "
                << line_count << ". \n\t" << script_line << std::endl;
      exit(EXIT_FAILURE);
    }

    const char *start = script_line.c_str();
    char *end;
    auto v = std::strtod(start, &end);

    errno = 0;
    if (v < 0.0 || !std::isfinite(v) || end == start || errno != 0) {
      auto error = errno;
      std::cerr << "Time value error at line " << line_count << ". \n\t"
                << "value = " << script_line.substr(0, pos) << std::endl;
      if (error != 0) {
        std::cerr << "\t" << strerror(error) << std::endl;
      }
      exit(EXIT_FAILURE);
    }

    timings.push_back(v / 1000.0);
    uris.push_back(script_line.substr(pos + 1, script_line.size()));
  }
}
} // namespace

namespace {
std::unique_ptr<Worker> create_worker(uint32_t id, SSL_CTX *ssl_ctx,
                                      size_t nreqs, size_t nclients,
                                      size_t rate, size_t max_samples) {
  std::stringstream rate_report;
  if (config.is_rate_mode() && nclients > rate) {
    rate_report << "Up to " << rate << " client(s) will be created every "
                << util::duration_str(config.rate_period) << " ";
  }

  std::cout << "spawning thread #" << id << ": " << nclients
            << " total client(s). " << rate_report.str() << nreqs
            << " total requests" << std::endl;

  return make_unique<Worker>(id, ssl_ctx, nreqs, nclients, rate, max_samples,
                             &config);
}
} // namespace

namespace {
void print_version(std::ostream &out) {
  out << "h2load nghttp2/" NGHTTP2_VERSION << std::endl;
}
} // namespace

namespace {
void print_usage(std::ostream &out) {
  out << R"(Usage: h2load [OPTIONS]... [URI]...
benchmarking tool for HTTP/2 and SPDY server)" << std::endl;
}
} // namespace

namespace {
constexpr char DEFAULT_NPN_LIST[] = "h2,h2-16,h2-14"
#ifdef HAVE_SPDYLAY
                                    ",spdy/3.1,spdy/3,spdy/2"
#endif // HAVE_SPDYLAY
                                    ",http/1.1";
} // namespace

namespace {
void print_help(std::ostream &out) {
  print_usage(out);

  auto config = Config();

  out << R"(
  <URI>       Specify URI to access.   Multiple URIs can be specified.
              URIs are used  in this order for each  client.  All URIs
              are used, then  first URI is used and then  2nd URI, and
              so  on.  The  scheme, host  and port  in the  subsequent
              URIs, if present,  are ignored.  Those in  the first URI
              are used solely.  Definition of a base URI overrides all
              scheme, host or port values.
Options:
  -n, --requests=<N>
              Number of  requests across all  clients.  If it  is used
              with --timing-script-file option,  this option specifies
              the number of requests  each client performs rather than
              the number of requests across all clients.
              Default: )" << config.nreqs << R"(
  -c, --clients=<N>
              Number  of concurrent  clients.   With  -r option,  this
              specifies the maximum number of connections to be made.
              Default: )" << config.nclients << R"(
  -t, --threads=<N>
              Number of native threads.
              Default: )" << config.nthreads << R"(
  -i, --input-file=<PATH>
              Path of a file with multiple URIs are separated by EOLs.
              This option will disable URIs getting from command-line.
              If '-' is given as <PATH>, URIs will be read from stdin.
              URIs are used  in this order for each  client.  All URIs
              are used, then  first URI is used and then  2nd URI, and
              so  on.  The  scheme, host  and port  in the  subsequent
              URIs, if present,  are ignored.  Those in  the first URI
              are used solely.  Definition of a base URI overrides all
              scheme, host or port values.
  -m, --max-concurrent-streams=<N>
              Max  concurrent  streams  to issue  per  session.   When
              http/1.1  is used,  this  specifies the  number of  HTTP
              pipelining requests in-flight.
              Default: 1
  -w, --window-bits=<N>
              Sets the stream level initial window size to (2**<N>)-1.
              For SPDY, 2**<N> is used instead.
              Default: )" << config.window_bits << R"(
  -W, --connection-window-bits=<N>
              Sets  the  connection  level   initial  window  size  to
              (2**<N>)-1.  For SPDY, if <N>  is strictly less than 16,
              this option  is ignored.   Otherwise 2**<N> is  used for
              SPDY.
              Default: )" << config.connection_window_bits << R"(
  -H, --header=<HEADER>
              Add/Override a header to the requests.
  --ciphers=<SUITE>
              Set allowed  cipher list.  The  format of the  string is
              described in OpenSSL ciphers(1).
  -p, --no-tls-proto=<PROTOID>
              Specify ALPN identifier of the  protocol to be used when
              accessing http URI without SSL/TLS.)";

#ifdef HAVE_SPDYLAY
  out << R"(
              Available protocols: spdy/2, spdy/3, spdy/3.1, )";
#else  // !HAVE_SPDYLAY
  out << R"(
              Available protocols: )";
#endif // !HAVE_SPDYLAY
  out << NGHTTP2_CLEARTEXT_PROTO_VERSION_ID << R"( and
              )" << NGHTTP2_H1_1 << R"(
              Default: )" << NGHTTP2_CLEARTEXT_PROTO_VERSION_ID << R"(
  -d, --data=<PATH>
              Post FILE to  server.  The request method  is changed to
              POST.
  -r, --rate=<N>
              Specifies  the  fixed  rate  at  which  connections  are
              created.   The   rate  must   be  a   positive  integer,
              representing the  number of  connections to be  made per
              rate period.   The maximum  number of connections  to be
              made  is  given  in  -c   option.   This  rate  will  be
              distributed among  threads as  evenly as  possible.  For
              example,  with   -t2  and   -r4,  each  thread   gets  2
              connections per period.  When the rate is 0, the program
              will run  as it  normally does, creating  connections at
              whatever variable rate it  wants.  The default value for
              this option is 0.
  --rate-period=<DURATION>
              Specifies the time  period between creating connections.
              The period  must be a positive  number, representing the
              length of the period in time.  This option is ignored if
              the rate option is not used.  The default value for this
              option is 1s.
  -T, --connection-active-timeout=<DURATION>
              Specifies  the maximum  time that  h2load is  willing to
              keep a  connection open,  regardless of the  activity on
              said connection.  <DURATION> must be a positive integer,
              specifying the amount of time  to wait.  When no timeout
              value is  set (either  active or inactive),  h2load will
              keep  a  connection  open indefinitely,  waiting  for  a
              response.
  -N, --connection-inactivity-timeout=<DURATION>
              Specifies the amount  of time that h2load  is willing to
              wait to see activity  on a given connection.  <DURATION>
              must  be a  positive integer,  specifying the  amount of
              time  to wait.   When no  timeout value  is set  (either
              active or inactive), h2load  will keep a connection open
              indefinitely, waiting for a response.
  --timing-script-file=<PATH>
              Path of a file containing one or more lines separated by
              EOLs.  Each script line is composed of two tab-separated
              fields.  The first field represents the time offset from
              the start of execution, expressed as a positive value of
              milliseconds  with microsecond  resolution.  The  second
              field represents the URI.  This option will disable URIs
              getting from  command-line.  If '-' is  given as <PATH>,
              script lines will be read  from stdin.  Script lines are
              used in order for each client.   If -n is given, it must
              be less  than or  equal to the  number of  script lines,
              larger values are clamped to the number of script lines.
              If -n is not given,  the number of requests will default
              to the  number of  script lines.   The scheme,  host and
              port defined in  the first URI are  used solely.  Values
              contained  in  other  URIs,  if  present,  are  ignored.
              Definition of a  base URI overrides all  scheme, host or
              port values.
  -B, --base-uri=(<URI>|unix:<PATH>)
              Specify URI from which the scheme, host and port will be
              used  for  all requests.   The  base  URI overrides  all
              values  defined either  at  the command  line or  inside
              input files.  If argument  starts with "unix:", then the
              rest  of the  argument will  be treated  as UNIX  domain
              socket path.   The connection is made  through that path
              instead of TCP.   In this case, scheme  is inferred from
              the first  URI appeared  in the  command line  or inside
              input files as usual.
  --npn-list=<LIST>
              Comma delimited list of  ALPN protocol identifier sorted
              in the  order of preference.  That  means most desirable
              protocol comes  first.  This  is used  in both  ALPN and
              NPN.  The parameter must be  delimited by a single comma
              only  and any  white spaces  are  treated as  a part  of
              protocol string.
              Default: )" << DEFAULT_NPN_LIST << R"(
  --h1        Short        hand         for        --npn-list=http/1.1
              --no-tls-proto=http/1.1,    which   effectively    force
              http/1.1 for both http and https URI.
  -v, --verbose
              Output debug information.
  --version   Display version information and exit.
  -h, --help  Display this help and exit.

--

  The <DURATION> argument is an integer and an optional unit (e.g., 1s
  is 1 second and 500ms is 500 milliseconds).  Units are h, m, s or ms
  (hours, minutes, seconds and milliseconds, respectively).  If a unit
  is omitted, a second is used as unit.)" << std::endl;
}
} // namespace

int main(int argc, char **argv) {
  ssl::libssl_init();

#ifndef NOTHREADS
  ssl::LibsslGlobalLock lock;
#endif // NOTHREADS

  std::string datafile;
  bool nreqs_set_manually = false;
  while (1) {
    static int flag = 0;
    static option long_options[] = {
        {"requests", required_argument, nullptr, 'n'},
        {"clients", required_argument, nullptr, 'c'},
        {"data", required_argument, nullptr, 'd'},
        {"threads", required_argument, nullptr, 't'},
        {"max-concurrent-streams", required_argument, nullptr, 'm'},
        {"window-bits", required_argument, nullptr, 'w'},
        {"connection-window-bits", required_argument, nullptr, 'W'},
        {"input-file", required_argument, nullptr, 'i'},
        {"header", required_argument, nullptr, 'H'},
        {"no-tls-proto", required_argument, nullptr, 'p'},
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, &flag, 1},
        {"ciphers", required_argument, &flag, 2},
        {"rate", required_argument, nullptr, 'r'},
        {"connection-active-timeout", required_argument, nullptr, 'T'},
        {"connection-inactivity-timeout", required_argument, nullptr, 'N'},
        {"timing-script-file", required_argument, &flag, 3},
        {"base-uri", required_argument, nullptr, 'B'},
        {"npn-list", required_argument, &flag, 4},
        {"rate-period", required_argument, &flag, 5},
        {"h1", no_argument, &flag, 6},
        {nullptr, 0, nullptr, 0}};
    int option_index = 0;
    auto c = getopt_long(argc, argv, "hvW:c:d:m:n:p:t:w:H:i:r:T:N:B:",
                         long_options, &option_index);
    if (c == -1) {
      break;
    }
    switch (c) {
    case 'n':
      config.nreqs = strtoul(optarg, nullptr, 10);
      nreqs_set_manually = true;
      break;
    case 'c':
      config.nclients = strtoul(optarg, nullptr, 10);
      break;
    case 'd':
      datafile = optarg;
      break;
    case 't':
#ifdef NOTHREADS
      std::cerr << "-t: WARNING: Threading disabled at build time, "
                << "no threads created." << std::endl;
#else
      config.nthreads = strtoul(optarg, nullptr, 10);
#endif // NOTHREADS
      break;
    case 'm':
      config.max_concurrent_streams = strtoul(optarg, nullptr, 10);
      break;
    case 'w':
    case 'W': {
      errno = 0;
      char *endptr = nullptr;
      auto n = strtoul(optarg, &endptr, 10);
      if (errno == 0 && *endptr == '\0' && n < 31) {
        if (c == 'w') {
          config.window_bits = n;
        } else {
          config.connection_window_bits = n;
        }
      } else {
        std::cerr << "-" << static_cast<char>(c)
                  << ": specify the integer in the range [0, 30], inclusive"
                  << std::endl;
        exit(EXIT_FAILURE);
      }
      break;
    }
    case 'H': {
      char *header = optarg;
      // Skip first possible ':' in the header name
      char *value = strchr(optarg + 1, ':');
      if (!value || (header[0] == ':' && header + 1 == value)) {
        std::cerr << "-H: invalid header: " << optarg << std::endl;
        exit(EXIT_FAILURE);
      }
      *value = 0;
      value++;
      while (isspace(*value)) {
        value++;
      }
      if (*value == 0) {
        // This could also be a valid case for suppressing a header
        // similar to curl
        std::cerr << "-H: invalid header - value missing: " << optarg
                  << std::endl;
        exit(EXIT_FAILURE);
      }
      // Note that there is no processing currently to handle multiple
      // message-header fields with the same field name
      config.custom_headers.emplace_back(header, value);
      util::inp_strlower(config.custom_headers.back().name);
      break;
    }
    case 'i':
      config.ifile = optarg;
      break;
    case 'p':
      if (util::strieq(NGHTTP2_CLEARTEXT_PROTO_VERSION_ID, optarg)) {
        config.no_tls_proto = Config::PROTO_HTTP2;
      } else if (util::strieq(NGHTTP2_H1_1, optarg)) {
        config.no_tls_proto = Config::PROTO_HTTP1_1;
#ifdef HAVE_SPDYLAY
      } else if (util::strieq("spdy/2", optarg)) {
        config.no_tls_proto = Config::PROTO_SPDY2;
      } else if (util::strieq("spdy/3", optarg)) {
        config.no_tls_proto = Config::PROTO_SPDY3;
      } else if (util::strieq("spdy/3.1", optarg)) {
        config.no_tls_proto = Config::PROTO_SPDY3_1;
#endif // HAVE_SPDYLAY
      } else {
        std::cerr << "-p: unsupported protocol " << optarg << std::endl;
        exit(EXIT_FAILURE);
      }
      break;
    case 'r':
      config.rate = strtoul(optarg, nullptr, 10);
      if (config.rate == 0) {
        std::cerr << "-r: the rate at which connections are made "
                  << "must be positive." << std::endl;
        exit(EXIT_FAILURE);
      }
      break;
    case 'T':
      config.conn_active_timeout = util::parse_duration_with_unit(optarg);
      if (!std::isfinite(config.conn_active_timeout)) {
        std::cerr << "-T: bad value for the conn_active_timeout wait time: "
                  << optarg << std::endl;
        exit(EXIT_FAILURE);
      }
      break;
    case 'N':
      config.conn_inactivity_timeout = util::parse_duration_with_unit(optarg);
      if (!std::isfinite(config.conn_inactivity_timeout)) {
        std::cerr << "-N: bad value for the conn_inactivity_timeout wait time: "
                  << optarg << std::endl;
        exit(EXIT_FAILURE);
      }
      break;
    case 'B': {
      config.base_uri = "";
      config.base_uri_unix = false;

      if (util::istarts_with_l(optarg, UNIX_PATH_PREFIX)) {
        // UNIX domain socket path
        sockaddr_un un;

        auto path = optarg + str_size(UNIX_PATH_PREFIX);
        auto pathlen = strlen(optarg) - str_size(UNIX_PATH_PREFIX);

        if (pathlen == 0 || pathlen + 1 > sizeof(un.sun_path)) {
          std::cerr << "--base-uri: invalid UNIX domain socket path: " << optarg
                    << std::endl;
          exit(EXIT_FAILURE);
        }

        config.base_uri_unix = true;

        auto &unix_addr = config.unix_addr;
        std::copy_n(path, pathlen + 1, unix_addr.sun_path);
        unix_addr.sun_family = AF_UNIX;

        break;
      }

      if (!parse_base_uri(optarg)) {
        std::cerr << "--base-uri: invalid base URI: " << optarg << std::endl;
        exit(EXIT_FAILURE);
      }

      config.base_uri = optarg;
      break;
    }
    case 'v':
      config.verbose = true;
      break;
    case 'h':
      print_help(std::cout);
      exit(EXIT_SUCCESS);
    case '?':
      util::show_candidates(argv[optind - 1], long_options);
      exit(EXIT_FAILURE);
    case 0:
      switch (flag) {
      case 1:
        // version option
        print_version(std::cout);
        exit(EXIT_SUCCESS);
      case 2:
        // ciphers option
        config.ciphers = optarg;
        break;
      case 3:
        // timing-script option
        config.ifile = optarg;
        config.timing_script = true;
        break;
      case 4:
        // npn-list option
        config.npn_list = util::parse_config_str_list(optarg);
        break;
      case 5:
        // rate-period
        config.rate_period = util::parse_duration_with_unit(optarg);
        if (!std::isfinite(config.rate_period)) {
          std::cerr << "--rate-period: value error " << optarg << std::endl;
          exit(EXIT_FAILURE);
        }
        break;
      case 6:
        // --h1
        config.npn_list = util::parse_config_str_list("http/1.1");
        config.no_tls_proto = Config::PROTO_HTTP1_1;
        break;
      }
      break;
    default:
      break;
    }
  }

  if (argc == optind) {
    if (config.ifile.empty()) {
      std::cerr << "no URI or input file given" << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  if (config.nclients == 0) {
    std::cerr << "-c: the number of clients must be strictly greater than 0."
              << std::endl;
    exit(EXIT_FAILURE);
  }

  if (config.npn_list.empty()) {
    config.npn_list = util::parse_config_str_list(DEFAULT_NPN_LIST);
  }

  // serialize the APLN tokens
  for (auto &proto : config.npn_list) {
    proto.insert(proto.begin(), static_cast<unsigned char>(proto.size()));
  }

  std::vector<std::string> reqlines;

  if (config.ifile.empty()) {
    std::vector<std::string> uris;
    std::copy(&argv[optind], &argv[argc], std::back_inserter(uris));
    reqlines = parse_uris(std::begin(uris), std::end(uris));
  } else {
    std::vector<std::string> uris;
    if (!config.timing_script) {
      if (config.ifile == "-") {
        uris = read_uri_from_file(std::cin);
      } else {
        std::ifstream infile(config.ifile);
        if (!infile) {
          std::cerr << "cannot read input file: " << config.ifile << std::endl;
          exit(EXIT_FAILURE);
        }

        uris = read_uri_from_file(infile);
      }
    } else {
      if (config.ifile == "-") {
        read_script_from_file(std::cin, config.timings, uris);
      } else {
        std::ifstream infile(config.ifile);
        if (!infile) {
          std::cerr << "cannot read input file: " << config.ifile << std::endl;
          exit(EXIT_FAILURE);
        }

        read_script_from_file(infile, config.timings, uris);
      }

      if (nreqs_set_manually) {
        if (config.nreqs > uris.size()) {
          std::cerr << "-n: the number of requests must be less than or equal "
                       "to the number of timing script entries. Setting number "
                       "of requests to " << uris.size() << std::endl;

          config.nreqs = uris.size();
        }
      } else {
        config.nreqs = uris.size();
      }
    }

    reqlines = parse_uris(std::begin(uris), std::end(uris));
  }

  if (reqlines.empty()) {
    std::cerr << "No URI given" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (config.nreqs == 0) {
    std::cerr << "-n: the number of requests must be strictly greater than 0."
              << std::endl;
    exit(EXIT_FAILURE);
  }

  if (config.max_concurrent_streams == 0) {
    std::cerr << "-m: the max concurrent streams must be strictly greater "
              << "than 0." << std::endl;
    exit(EXIT_FAILURE);
  }

  if (config.nthreads == 0) {
    std::cerr << "-t: the number of threads must be strictly greater than 0."
              << std::endl;
    exit(EXIT_FAILURE);
  }

  if (config.nthreads > std::thread::hardware_concurrency()) {
    std::cerr << "-t: warning: the number of threads is greater than hardware "
              << "cores." << std::endl;
  }

  // With timing script, we don't distribute config.nreqs to each
  // client or thread.
  if (!config.timing_script && config.nreqs < config.nclients) {
    std::cerr << "-n, -c: the number of requests must be greater than or "
              << "equal to the clients." << std::endl;
    exit(EXIT_FAILURE);
  }

  if (config.nclients < config.nthreads) {
    std::cerr << "-c, -t: the number of clients must be greater than or equal "
                 "to the number of threads." << std::endl;
    exit(EXIT_FAILURE);
  }

  if (config.is_rate_mode()) {
    if (config.rate < config.nthreads) {
      std::cerr << "-r, -t: the connection rate must be greater than or equal "
                << "to the number of threads." << std::endl;
      exit(EXIT_FAILURE);
    }

    if (config.rate > config.nclients) {
      std::cerr << "-r, -c: the connection rate must be smaller than or equal "
                   "to the number of clients." << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  if (!datafile.empty()) {
    config.data_fd = open(datafile.c_str(), O_RDONLY | O_BINARY);
    if (config.data_fd == -1) {
      std::cerr << "-d: Could not open file " << datafile << std::endl;
      exit(EXIT_FAILURE);
    }
    struct stat data_stat;
    if (fstat(config.data_fd, &data_stat) == -1) {
      std::cerr << "-d: Could not stat file " << datafile << std::endl;
      exit(EXIT_FAILURE);
    }
    config.data_length = data_stat.st_size;
  }

  struct sigaction act {};
  act.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &act, nullptr);

  auto ssl_ctx = SSL_CTX_new(SSLv23_client_method());
  if (!ssl_ctx) {
    std::cerr << "Failed to create SSL_CTX: "
              << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
    exit(EXIT_FAILURE);
  }

  auto ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
                  SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION |
                  SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;

  SSL_CTX_set_options(ssl_ctx, ssl_opts);
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_RELEASE_BUFFERS);

  const char *ciphers;
  if (config.ciphers.empty()) {
    ciphers = ssl::DEFAULT_CIPHER_LIST;
  } else {
    ciphers = config.ciphers.c_str();
  }

  if (SSL_CTX_set_cipher_list(ssl_ctx, ciphers) == 0) {
    std::cerr << "SSL_CTX_set_cipher_list with " << ciphers
              << " failed: " << ERR_error_string(ERR_get_error(), nullptr)
              << std::endl;
    exit(EXIT_FAILURE);
  }

  SSL_CTX_set_next_proto_select_cb(ssl_ctx, client_select_next_proto_cb,
                                   nullptr);

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
  std::vector<unsigned char> proto_list;
  for (const auto &proto : config.npn_list) {
    std::copy_n(proto.c_str(), proto.size(), std::back_inserter(proto_list));
  }

  SSL_CTX_set_alpn_protos(ssl_ctx, proto_list.data(), proto_list.size());
#endif // OPENSSL_VERSION_NUMBER >= 0x10002000L

  std::string user_agent = "h2load nghttp2/" NGHTTP2_VERSION;
  Headers shared_nva;
  shared_nva.emplace_back(":scheme", config.scheme);
  if (config.port != config.default_port) {
    shared_nva.emplace_back(":authority",
                            config.host + ":" + util::utos(config.port));
  } else {
    shared_nva.emplace_back(":authority", config.host);
  }
  shared_nva.emplace_back(":method", config.data_fd == -1 ? "GET" : "POST");
  shared_nva.emplace_back("user-agent", user_agent);

  // list overridalbe headers
  auto override_hdrs = make_array<std::string>(":authority", ":host", ":method",
                                               ":scheme", "user-agent");

  for (auto &kv : config.custom_headers) {
    if (std::find(std::begin(override_hdrs), std::end(override_hdrs),
                  kv.name) != std::end(override_hdrs)) {
      // override header
      for (auto &nv : shared_nva) {
        if ((nv.name == ":authority" && kv.name == ":host") ||
            (nv.name == kv.name)) {
          nv.value = kv.value;
        }
      }
    } else {
      // add additional headers
      shared_nva.push_back(kv);
    }
  }

  auto method_it =
      std::find_if(std::begin(shared_nva), std::end(shared_nva),
                   [](const Header &nv) { return nv.name == ":method"; });
  assert(method_it != std::end(shared_nva));

  config.h1reqs.reserve(reqlines.size());
  config.nva.reserve(reqlines.size());
  config.nv.reserve(reqlines.size());

  for (auto &req : reqlines) {
    // For HTTP/1.1
    auto h1req = (*method_it).value;
    h1req += ' ';
    h1req += req;
    h1req += " HTTP/1.1\r\n";
    for (auto &nv : shared_nva) {
      if (nv.name == ":authority") {
        h1req += "Host: ";
        h1req += nv.value;
        h1req += "\r\n";
        continue;
      }
      if (nv.name[0] == ':') {
        continue;
      }
      h1req += nv.name;
      h1req += ": ";
      h1req += nv.value;
      h1req += "\r\n";
    }
    h1req += "\r\n";

    config.h1reqs.push_back(std::move(h1req));

    // For nghttp2
    std::vector<nghttp2_nv> nva;
    // 1 for :path
    nva.reserve(1 + shared_nva.size());

    nva.push_back(http2::make_nv_ls(":path", req));

    for (auto &nv : shared_nva) {
      nva.push_back(http2::make_nv(nv.name, nv.value, false));
    }

    config.nva.push_back(std::move(nva));

    // For spdylay
    std::vector<const char *> cva;
    // 2 for :path and :version, 1 for terminal nullptr
    cva.reserve(2 * (2 + shared_nva.size()) + 1);

    cva.push_back(":path");
    cva.push_back(req.c_str());

    for (auto &nv : shared_nva) {
      if (nv.name == ":authority") {
        cva.push_back(":host");
      } else {
        cva.push_back(nv.name.c_str());
      }
      cva.push_back(nv.value.c_str());
    }
    cva.push_back(":version");
    cva.push_back("HTTP/1.1");
    cva.push_back(nullptr);

    config.nv.push_back(std::move(cva));
  }

  // Don't DOS our server!
  if (config.host == "nghttp2.org") {
    std::cerr << "Using h2load against public server " << config.host
              << " should be prohibited." << std::endl;
    exit(EXIT_FAILURE);
  }

  resolve_host();

  std::cout << "starting benchmark..." << std::endl;

  std::vector<std::unique_ptr<Worker>> workers;
  workers.reserve(config.nthreads);

#ifndef NOTHREADS
  size_t nreqs_per_thread = 0;
  ssize_t nreqs_rem = 0;

  if (!config.timing_script) {
    nreqs_per_thread = config.nreqs / config.nthreads;
    nreqs_rem = config.nreqs % config.nthreads;
  }

  size_t nclients_per_thread = config.nclients / config.nthreads;
  ssize_t nclients_rem = config.nclients % config.nthreads;

  size_t rate_per_thread = config.rate / config.nthreads;
  ssize_t rate_per_thread_rem = config.rate % config.nthreads;

  size_t max_samples_per_thread =
      std::max(static_cast<size_t>(256), MAX_SAMPLES / config.nthreads);

  std::mutex mu;
  std::condition_variable cv;
  auto ready = false;

  std::vector<std::future<void>> futures;
  for (size_t i = 0; i < config.nthreads; ++i) {
    auto rate = rate_per_thread;
    if (rate_per_thread_rem > 0) {
      --rate_per_thread_rem;
      ++rate;
    }
    auto nclients = nclients_per_thread;
    if (nclients_rem > 0) {
      --nclients_rem;
      ++nclients;
    }

    size_t nreqs;
    if (config.timing_script) {
      // With timing script, each client issues config.nreqs requests.
      // We divide nreqs by number of clients in Worker ctor to
      // distribute requests to those clients evenly, so multiply
      // config.nreqs here by config.nclients.
      nreqs = config.nreqs * nclients;
    } else {
      nreqs = nreqs_per_thread;
      if (nreqs_rem > 0) {
        --nreqs_rem;
        ++nreqs;
      }
    }

    workers.push_back(create_worker(i, ssl_ctx, nreqs, nclients, rate,
                                    max_samples_per_thread));
    auto &worker = workers.back();
    futures.push_back(
        std::async(std::launch::async, [&worker, &mu, &cv, &ready]() {
          {
            std::unique_lock<std::mutex> ulk(mu);
            cv.wait(ulk, [&ready] { return ready; });
          }
          worker->run();
        }));
  }

  {
    std::lock_guard<std::mutex> lg(mu);
    ready = true;
    cv.notify_all();
  }

  auto start = std::chrono::steady_clock::now();

  for (auto &fut : futures) {
    fut.get();
  }

#else  // NOTHREADS
  auto rate = config.rate;
  auto nclients = config.nclients;
  auto nreqs =
      config.timing_script ? config.nreqs * config.nclients : config.nreqs;

  workers.push_back(
      create_worker(0, ssl_ctx, nreqs, nclients, rate, MAX_SAMPLES));

  auto start = std::chrono::steady_clock::now();

  workers.back()->run();
#endif // NOTHREADS

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  Stats stats(0, 0);
  for (const auto &w : workers) {
    const auto &s = w->stats;

    stats.req_todo += s.req_todo;
    stats.req_started += s.req_started;
    stats.req_done += s.req_done;
    stats.req_timedout += s.req_timedout;
    stats.req_success += s.req_success;
    stats.req_status_success += s.req_status_success;
    stats.req_failed += s.req_failed;
    stats.req_error += s.req_error;
    stats.bytes_total += s.bytes_total;
    stats.bytes_head += s.bytes_head;
    stats.bytes_head_decomp += s.bytes_head_decomp;
    stats.bytes_body += s.bytes_body;

    for (size_t i = 0; i < stats.status.size(); ++i) {
      stats.status[i] += s.status[i];
    }
  }

  auto ts = process_time_stats(workers);

  // Requests which have not been issued due to connection errors, are
  // counted towards req_failed and req_error.
  auto req_not_issued =
      stats.req_todo - stats.req_status_success - stats.req_failed;
  stats.req_failed += req_not_issued;
  stats.req_error += req_not_issued;

  // UI is heavily inspired by weighttp[1] and wrk[2]
  //
  // [1] https://github.com/lighttpd/weighttp
  // [2] https://github.com/wg/wrk
  double rps = 0;
  int64_t bps = 0;
  if (duration.count() > 0) {
    auto secd = std::chrono::duration_cast<
        std::chrono::duration<double, std::chrono::seconds::period>>(duration);
    rps = stats.req_success / secd.count();
    bps = stats.bytes_total / secd.count();
  }

  double header_space_savings = 0.;
  if (stats.bytes_head_decomp > 0) {
    header_space_savings =
        1. - static_cast<double>(stats.bytes_head) / stats.bytes_head_decomp;
  }

  std::cout << std::fixed << std::setprecision(2) << R"(
finished in )" << util::format_duration(duration) << ", " << rps << " req/s, "
            << util::utos_funit(bps) << R"(B/s
requests: )" << stats.req_todo << " total, " << stats.req_started
            << " started, " << stats.req_done << " done, "
            << stats.req_status_success << " succeeded, " << stats.req_failed
            << " failed, " << stats.req_error << " errored, "
            << stats.req_timedout << R"( timeout
status codes: )" << stats.status[2] << " 2xx, " << stats.status[3] << " 3xx, "
            << stats.status[4] << " 4xx, " << stats.status[5] << R"( 5xx
traffic: )" << util::utos_funit(stats.bytes_total) << "B (" << stats.bytes_total
            << ") total, " << util::utos_funit(stats.bytes_head) << "B ("
            << stats.bytes_head << ") headers (space savings "
            << header_space_savings * 100 << "%), "
            << util::utos_funit(stats.bytes_body) << "B (" << stats.bytes_body
            << R"() data
                     min         max         mean         sd        +/- sd
time for request: )" << std::setw(10) << util::format_duration(ts.request.min)
            << "  " << std::setw(10) << util::format_duration(ts.request.max)
            << "  " << std::setw(10) << util::format_duration(ts.request.mean)
            << "  " << std::setw(10) << util::format_duration(ts.request.sd)
            << std::setw(9) << util::dtos(ts.request.within_sd) << "%"
            << "\ntime for connect: " << std::setw(10)
            << util::format_duration(ts.connect.min) << "  " << std::setw(10)
            << util::format_duration(ts.connect.max) << "  " << std::setw(10)
            << util::format_duration(ts.connect.mean) << "  " << std::setw(10)
            << util::format_duration(ts.connect.sd) << std::setw(9)
            << util::dtos(ts.connect.within_sd) << "%"
            << "\ntime to 1st byte: " << std::setw(10)
            << util::format_duration(ts.ttfb.min) << "  " << std::setw(10)
            << util::format_duration(ts.ttfb.max) << "  " << std::setw(10)
            << util::format_duration(ts.ttfb.mean) << "  " << std::setw(10)
            << util::format_duration(ts.ttfb.sd) << std::setw(9)
            << util::dtos(ts.ttfb.within_sd) << "%"
            << "\nreq/s           : " << std::setw(10) << ts.rps.min << "  "
            << std::setw(10) << ts.rps.max << "  " << std::setw(10)
            << ts.rps.mean << "  " << std::setw(10) << ts.rps.sd << std::setw(9)
            << util::dtos(ts.rps.within_sd) << "%" << std::endl;

  SSL_CTX_free(ssl_ctx);

  return 0;
}

} // namespace h2load

int main(int argc, char **argv) { return h2load::main(argc, argv); }
