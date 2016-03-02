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
#ifndef SHRPX_HTTP2_SESSION_H
#define SHRPX_HTTP2_SESSION_H

#include "shrpx.h"

#include <unordered_set>
#include <memory>

#include <openssl/ssl.h>

#include <ev.h>

#include <nghttp2/nghttp2.h>

#include "http-parser/http_parser.h"

#include "shrpx_connection.h"
#include "buffer.h"
#include "template.h"

using namespace nghttp2;

namespace shrpx {

class Http2DownstreamConnection;
class Worker;
class ConnectBlocker;

struct StreamData {
  StreamData *dlnext, *dlprev;
  Http2DownstreamConnection *dconn;
};

class Http2Session {
public:
  Http2Session(struct ev_loop *loop, SSL_CTX *ssl_ctx,
               ConnectBlocker *connect_blocker, Worker *worker, size_t group,
               size_t idx);
  ~Http2Session();

  int check_cert();

  // If hard is true, all pending requests are abandoned and
  // associated ClientHandlers will be deleted.
  int disconnect(bool hard = false);
  int initiate_connection();

  void add_downstream_connection(Http2DownstreamConnection *dconn);
  void remove_downstream_connection(Http2DownstreamConnection *dconn);

  void remove_stream_data(StreamData *sd);

  int submit_request(Http2DownstreamConnection *dconn, const nghttp2_nv *nva,
                     size_t nvlen, const nghttp2_data_provider *data_prd);

  int submit_rst_stream(int32_t stream_id, uint32_t error_code);

  int terminate_session(uint32_t error_code);

  nghttp2_session *get_session() const;

  bool get_flow_control() const;

  int resume_data(Http2DownstreamConnection *dconn);

  int connection_made();

  int do_read();
  int do_write();

  int on_read();
  int on_write();

  int connected();
  int read_clear();
  int write_clear();
  int tls_handshake();
  int read_tls();
  int write_tls();

  int downstream_read_proxy();
  int downstream_connect_proxy();

  int downstream_read();
  int downstream_write();

  int noop();

  void signal_write();

  struct ev_loop *get_loop() const;

  ev_io *get_wev();

  int get_state() const;
  void set_state(int state);

  void start_settings_timer();
  void stop_settings_timer();

  SSL *get_ssl() const;

  int consume(int32_t stream_id, size_t len);

  // Returns true if request can be issued on downstream connection.
  bool can_push_request() const;
  // Initiates the connection checking if downstream connection has
  // been established and connection checking is required.
  void start_checking_connection();
  // Resets connection check timer to timeout |t|.  After timeout, we
  // require connection checking.  If connection checking is already
  // enabled, this timeout is for PING ACK timeout.
  void reset_connection_check_timer(ev_tstamp t);
  void reset_connection_check_timer_if_not_checking();
  // Signals that connection is alive.  Internally
  // reset_connection_check_timer() is called.
  void connection_alive();
  // Change connection check state.
  void set_connection_check_state(int state);
  int get_connection_check_state() const;

  bool should_hard_fail() const;

  void submit_pending_requests();

  size_t get_addr_idx() const;

  size_t get_group() const;

  size_t get_index() const;

  int handle_downstream_push_promise(Downstream *downstream,
                                     int32_t promised_stream_id);
  int handle_downstream_push_promise_complete(Downstream *downstream,
                                              Downstream *promised_downstream);

  enum {
    // Disconnected
    DISCONNECTED,
    // Connecting proxy and making CONNECT request
    PROXY_CONNECTING,
    // Tunnel is established with proxy
    PROXY_CONNECTED,
    // Establishing tunnel is failed
    PROXY_FAILED,
    // Connecting to downstream and/or performing SSL/TLS handshake
    CONNECTING,
    // Connected to downstream
    CONNECTED,
    // Connection is started to fail
    CONNECT_FAILING,
  };

  enum {
    // Connection checking is not required
    CONNECTION_CHECK_NONE,
    // Connection checking is required
    CONNECTION_CHECK_REQUIRED,
    // Connection checking has been started
    CONNECTION_CHECK_STARTED
  };

  using ReadBuf = Buffer<8_k>;
  using WriteBuf = Buffer<32768>;

private:
  Connection conn_;
  ev_timer settings_timer_;
  // This timer has 2 purpose: when it first timeout, set
  // connection_check_state_ = CONNECTION_CHECK_REQUIRED.  After
  // connection check has started, this timer is started again and
  // traps PING ACK timeout.
  ev_timer connchk_timer_;
  DList<Http2DownstreamConnection> dconns_;
  DList<StreamData> streams_;
  std::function<int(Http2Session &)> read_, write_;
  std::function<int(Http2Session &)> on_read_, on_write_;
  // Used to parse the response from HTTP proxy
  std::unique_ptr<http_parser> proxy_htp_;
  Worker *worker_;
  ConnectBlocker *connect_blocker_;
  // NULL if no TLS is configured
  SSL_CTX *ssl_ctx_;
  nghttp2_session *session_;
  const uint8_t *data_pending_;
  size_t data_pendinglen_;
  // index of get_config()->downstream_addrs this object uses
  size_t addr_idx_;
  size_t group_;
  // index inside group, this is used to pin frontend to certain
  // HTTP/2 backend for better throughput.
  size_t index_;
  int state_;
  int connection_check_state_;
  bool flow_control_;
  WriteBuf wb_;
  ReadBuf rb_;
};

nghttp2_session_callbacks *create_http2_downstream_callbacks();

} // namespace shrpx

#endif // SHRPX_HTTP2_SESSION_H
