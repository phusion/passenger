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
#ifndef H2LOAD_H
#define H2LOAD_H

#include "nghttp2_config.h"

#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif // HAVE_SYS_SOCKET_H
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif // HAVE_NETDB_H
#include <sys/un.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <array>

#include <nghttp2/nghttp2.h>

#include <ev.h>

#include <openssl/ssl.h>

#include "http2.h"
#include "buffer.h"
#include "template.h"

using namespace nghttp2;

namespace h2load {

class Session;
struct Worker;

struct Config {
  std::vector<std::vector<nghttp2_nv>> nva;
  std::vector<std::vector<const char *>> nv;
  std::vector<std::string> h1reqs;
  std::vector<ev_tstamp> timings;
  nghttp2::Headers custom_headers;
  std::string scheme;
  std::string host;
  std::string ifile;
  std::string ciphers;
  // length of upload data
  int64_t data_length;
  addrinfo *addrs;
  size_t nreqs;
  size_t nclients;
  size_t nthreads;
  // The maximum number of concurrent streams per session.
  ssize_t max_concurrent_streams;
  size_t window_bits;
  size_t connection_window_bits;
  // rate at which connections should be made
  size_t rate;
  ev_tstamp rate_period;
  // amount of time to wait for activity on a given connection
  ev_tstamp conn_active_timeout;
  // amount of time to wait after the last request is made on a connection
  ev_tstamp conn_inactivity_timeout;
  enum {
    PROTO_HTTP2,
    PROTO_SPDY2,
    PROTO_SPDY3,
    PROTO_SPDY3_1,
    PROTO_HTTP1_1
  } no_tls_proto;
  // file descriptor for upload data
  int data_fd;
  uint16_t port;
  uint16_t default_port;
  bool verbose;
  bool timing_script;
  std::string base_uri;
  // true if UNIX domain socket is used.  In this case, base_uri is
  // not used in usual way.
  bool base_uri_unix;
  // used when UNIX domain socket is used (base_uri_unix is true).
  sockaddr_un unix_addr;
  // list of supported NPN/ALPN protocol strings in the order of
  // preference.
  std::vector<std::string> npn_list;

  Config();
  ~Config();

  bool is_rate_mode() const;
  bool has_base_uri() const;
};

struct RequestStat {
  // time point when request was sent
  std::chrono::steady_clock::time_point request_time;
  // time point when stream was closed
  std::chrono::steady_clock::time_point stream_close_time;
  // upload data length sent so far
  int64_t data_offset;
  // true if stream was successfully closed.  This means stream was
  // not reset, but it does not mean HTTP level error (e.g., 404).
  bool completed;
};

struct ClientStat {
  // time client started (i.e., first connect starts)
  std::chrono::steady_clock::time_point client_start_time;
  // time client end (i.e., client somehow processed all requests it
  // is responsible for, and disconnected)
  std::chrono::steady_clock::time_point client_end_time;
  // The number of requests completed successfull, but not necessarily
  // means successful HTTP status code.
  size_t req_success;

  // The following 3 numbers are overwritten each time when connection
  // is made.

  // time connect starts
  std::chrono::steady_clock::time_point connect_start_time;
  // time to connect
  std::chrono::steady_clock::time_point connect_time;
  // time to first byte (TTFB)
  std::chrono::steady_clock::time_point ttfb;
};

struct SDStat {
  // min, max, mean and sd (standard deviation)
  double min, max, mean, sd;
  // percentage of samples inside mean -/+ sd
  double within_sd;
};

struct SDStats {
  // time for request
  SDStat request;
  // time for connect
  SDStat connect;
  // time to first byte (TTFB)
  SDStat ttfb;
  // request per second for each client
  SDStat rps;
};

struct Stats {
  Stats(size_t req_todo, size_t nclients);
  // The total number of requests
  size_t req_todo;
  // The number of requests issued so far
  size_t req_started;
  // The number of requests finished
  size_t req_done;
  // The number of requests completed successfull, but not necessarily
  // means successful HTTP status code.
  size_t req_success;
  // The number of requests marked as success.  HTTP status code is
  // also considered as success. This is subset of req_done.
  size_t req_status_success;
  // The number of requests failed. This is subset of req_done.
  size_t req_failed;
  // The number of requests failed due to network errors. This is
  // subset of req_failed.
  size_t req_error;
  // The number of requests that failed due to timeout.
  size_t req_timedout;
  // The number of bytes received on the "wire". If SSL/TLS is used,
  // this is the number of decrypted bytes the application received.
  int64_t bytes_total;
  // The number of bytes received for header fields.  This is
  // compressed version.
  int64_t bytes_head;
  // The number of bytes received for header fields after they are
  // decompressed.
  int64_t bytes_head_decomp;
  // The number of bytes received in DATA frame.
  int64_t bytes_body;
  // The number of each HTTP status category, status[i] is status code
  // in the range [i*100, (i+1)*100).
  std::array<size_t, 6> status;
  // The statistics per request
  std::vector<RequestStat> req_stats;
  // THe statistics per client
  std::vector<ClientStat> client_stats;
};

enum ClientState { CLIENT_IDLE, CLIENT_CONNECTED };

struct Client;

// We use systematic sampling method
struct Sampling {
  // sampling interval
  double interval;
  // cumulative value of interval, and the next point is the integer
  // rounded up from this value.
  double point;
  // number of samples seen, including discarded samples.
  size_t n;
};

struct Worker {
  Stats stats;
  Sampling request_times_smp;
  Sampling client_smp;
  struct ev_loop *loop;
  SSL_CTX *ssl_ctx;
  Config *config;
  size_t progress_interval;
  uint32_t id;
  bool tls_info_report_done;
  bool app_info_report_done;
  size_t nconns_made;
  // number of clients this worker handles
  size_t nclients;
  // number of requests each client issues
  size_t nreqs_per_client;
  // at most nreqs_rem clients get an extra request
  size_t nreqs_rem;
  size_t rate;
  // maximum number of samples in this worker thread
  size_t max_samples;
  ev_timer timeout_watcher;
  // The next client ID this worker assigns
  uint32_t next_client_id;

  Worker(uint32_t id, SSL_CTX *ssl_ctx, size_t nreq_todo, size_t nclients,
         size_t rate, size_t max_samples, Config *config);
  ~Worker();
  Worker(Worker &&o) = default;
  void run();
  void sample_req_stat(RequestStat *req_stat);
  void sample_client_stat(ClientStat *cstat);
  void report_progress();
  void report_rate_progress();
};

struct Stream {
  RequestStat req_stat;
  int status_success;
  Stream();
};

struct Client {
  std::unordered_map<int32_t, Stream> streams;
  ClientStat cstat;
  std::unique_ptr<Session> session;
  ev_io wev;
  ev_io rev;
  std::function<int(Client &)> readfn, writefn;
  Worker *worker;
  SSL *ssl;
  ev_timer request_timeout_watcher;
  addrinfo *next_addr;
  // Address for the current address.  When try_new_connection() is
  // used and current_addr is not nullptr, it is used instead of
  // trying next address though next_addr.  To try new address, set
  // nullptr to current_addr before calling connect().
  addrinfo *current_addr;
  size_t reqidx;
  ClientState state;
  // The number of requests this client has to issue.
  size_t req_todo;
  // The number of requests this client has issued so far.
  size_t req_started;
  // The number of requests this client has done so far.
  size_t req_done;
  // The client id per worker
  uint32_t id;
  int fd;
  Buffer<64_k> wb;
  ev_timer conn_active_watcher;
  ev_timer conn_inactivity_watcher;
  std::string selected_proto;
  bool new_connection_requested;

  enum { ERR_CONNECT_FAIL = -100 };

  Client(uint32_t id, Worker *worker, size_t req_todo);
  ~Client();
  int make_socket(addrinfo *addr);
  int connect();
  void disconnect();
  void fail();
  void timeout();
  void restart_timeout();
  int submit_request();
  void process_request_failure();
  void process_timedout_streams();
  void process_abandoned_streams();
  void report_tls_info();
  void report_app_info();
  void terminate_session();
  // Asks client to create new connection, instead of just fail.
  void try_new_connection();

  int do_read();
  int do_write();

  // low-level I/O callback functions called by do_read/do_write
  int connected();
  int read_clear();
  int write_clear();
  int tls_handshake();
  int read_tls();
  int write_tls();

  int on_read(const uint8_t *data, size_t len);
  int on_write();

  int connection_made();

  void on_request(int32_t stream_id);
  void on_header(int32_t stream_id, const uint8_t *name, size_t namelen,
                 const uint8_t *value, size_t valuelen);
  void on_status_code(int32_t stream_id, uint16_t status);
  // |success| == true means that the request/response was exchanged
  // |successfully, but it does not mean response carried successful
  // |HTTP status code.
  void on_stream_close(int32_t stream_id, bool success, bool final = false);
  // Returns RequestStat for |stream_id|.  This function must be
  // called after on_request(stream_id), and before
  // on_stream_close(stream_id, ...).  Otherwise, this will return
  // nullptr.
  RequestStat *get_req_stat(int32_t stream_id);

  void record_request_time(RequestStat *req_stat);
  void record_connect_start_time();
  void record_connect_time();
  void record_ttfb();
  void clear_connect_times();
  void record_client_start_time();
  void record_client_end_time();

  void signal_write();
};

} // namespace h2load

#endif // H2LOAD_H
