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
#ifndef SHRPX_DOWNSTREAM_H
#define SHRPX_DOWNSTREAM_H

#include "shrpx.h"

#include <cinttypes>
#include <vector>
#include <string>
#include <memory>
#include <chrono>

#include <ev.h>

#include <nghttp2/nghttp2.h>

#include "shrpx_io_control.h"
#include "http2.h"
#include "memchunk.h"

using namespace nghttp2;

namespace shrpx {

class Upstream;
class DownstreamConnection;
struct BlockedLink;

class FieldStore {
public:
  FieldStore(size_t headers_initial_capacity)
      : content_length(-1), buffer_size_(0), header_key_prev_(false),
        trailer_key_prev_(false) {
    http2::init_hdidx(hdidx_);
    headers_.reserve(headers_initial_capacity);
  }

  const Headers &headers() const { return headers_; }
  const Headers &trailers() const { return trailers_; }

  Headers &headers() { return headers_; }

  const void add_extra_buffer_size(size_t n) { buffer_size_ += n; }
  size_t buffer_size() const { return buffer_size_; }

  size_t num_fields() const { return headers_.size() + trailers_.size(); }

  // Returns pointer to the header field with the name |name|.  If
  // multiple header have |name| as name, return last occurrence from
  // the beginning.  If no such header is found, returns nullptr.
  // This function must be called after headers are indexed
  const Headers::value_type *header(int16_t token) const;
  Headers::value_type *header(int16_t token);
  // Returns pointer to the header field with the name |name|.  If no
  // such header is found, returns nullptr.
  const Headers::value_type *header(const StringRef &name) const;

  void add_header(std::string name, std::string value);
  void add_header(std::string name, std::string value, int16_t token);
  void add_header(const uint8_t *name, size_t namelen, const uint8_t *value,
                  size_t valuelen, bool no_index, int16_t token);

  void append_last_header_key(const char *data, size_t len);
  void append_last_header_value(const char *data, size_t len);

  bool header_key_prev() const { return header_key_prev_; }

  // Lower the header field names and indexes header fields.  If there
  // is any invalid headers (e.g., multiple Content-Length having
  // different values), returns -1.
  int index_headers();

  // Empties headers.
  void clear_headers();

  void add_trailer(const uint8_t *name, size_t namelen, const uint8_t *value,
                   size_t valuelen, bool no_index, int16_t token);
  void add_trailer(std::string name, std::string value);

  void append_last_trailer_key(const char *data, size_t len);
  void append_last_trailer_value(const char *data, size_t len);

  bool trailer_key_prev() const { return trailer_key_prev_; }

  // content-length, -1 if it is unknown.
  int64_t content_length;

private:
  Headers headers_;
  // trailer fields.  For HTTP/1.1, trailer fields are only included
  // with chunked encoding.  For HTTP/2, there is no such limit.
  Headers trailers_;
  http2::HeaderIndex hdidx_;
  // Sum of the length of name and value in headers_ and trailers_.
  // This could also be increased by add_extra_buffer_size() to take
  // into account for request URI in case of HTTP/1.x request.
  size_t buffer_size_;
  bool header_key_prev_;
  bool trailer_key_prev_;
};

struct Request {
  Request()
      : fs(16), recv_body_length(0), unconsumed_body_length(0), method(-1),
        http_major(1), http_minor(1), upgrade_request(false),
        http2_upgrade_seen(false), connection_close(false),
        http2_expect_body(false), no_authority(false) {}

  void consume(size_t len) {
    assert(unconsumed_body_length >= len);
    unconsumed_body_length -= len;
  }

  FieldStore fs;
  // Request scheme.  For HTTP/2, this is :scheme header field value.
  // For HTTP/1.1, this is deduced from URI or connection.
  std::string scheme;
  // Request authority.  This is HTTP/2 :authority header field value
  // or host header field value.  We may deduce it from absolute-form
  // HTTP/1 request.  We also store authority-form HTTP/1 request.
  // This could be empty if request comes from HTTP/1.0 without Host
  // header field and origin-form.
  std::string authority;
  // Request path, including query component.  For HTTP/1.1, this is
  // request-target.  For HTTP/2, this is :path header field value.
  // For CONNECT request, this is empty.
  std::string path;
  // the length of request body received so far
  int64_t recv_body_length;
  // The number of bytes not consumed by the application yet.
  size_t unconsumed_body_length;
  int method;
  // HTTP major and minor version
  int http_major, http_minor;
  // Returns true if the request is HTTP upgrade (HTTP Upgrade or
  // CONNECT method).  Upgrade to HTTP/2 is excluded.  For HTTP/2
  // Upgrade, check get_http2_upgrade_request().
  bool upgrade_request;
  // true if h2c is seen in Upgrade header field.
  bool http2_upgrade_seen;
  bool connection_close;
  // true if this is HTTP/2, and request body is expected.  Note that
  // we don't take into account HTTP method here.
  bool http2_expect_body;
  // true if request does not have any information about authority.
  // This happens when: For HTTP/2 request, :authority is missing.
  // For HTTP/1 request, origin or asterisk form is used.
  bool no_authority;
};

struct Response {
  Response()
      : fs(32), recv_body_length(0), unconsumed_body_length(0), http_status(0),
        http_major(1), http_minor(1), connection_close(false) {}

  void consume(size_t len) {
    assert(unconsumed_body_length >= len);
    unconsumed_body_length -= len;
  }

  FieldStore fs;
  // the length of response body received so far
  int64_t recv_body_length;
  // The number of bytes not consumed by the application yet.  This is
  // mainly for HTTP/2 backend.
  size_t unconsumed_body_length;
  // HTTP status code
  unsigned int http_status;
  int http_major, http_minor;
  bool connection_close;
};

class Downstream {
public:
  Downstream(Upstream *upstream, MemchunkPool *mcpool, int32_t stream_id);
  ~Downstream();
  void reset_upstream(Upstream *upstream);
  Upstream *get_upstream() const;
  void set_stream_id(int32_t stream_id);
  int32_t get_stream_id() const;
  void set_assoc_stream_id(int32_t stream_id);
  int32_t get_assoc_stream_id() const;
  void pause_read(IOCtrlReason reason);
  int resume_read(IOCtrlReason reason, size_t consumed);
  void force_resume_read();
  // Set stream ID for downstream HTTP2 connection.
  void set_downstream_stream_id(int32_t stream_id);
  int32_t get_downstream_stream_id() const;

  int attach_downstream_connection(std::unique_ptr<DownstreamConnection> dconn);
  void detach_downstream_connection();
  DownstreamConnection *get_downstream_connection();
  // Returns dconn_ and nullifies dconn_.
  std::unique_ptr<DownstreamConnection> pop_downstream_connection();

  // Returns true if output buffer is full. If underlying dconn_ is
  // NULL, this function always returns false.
  bool request_buf_full();
  // Returns true if upgrade (HTTP Upgrade or CONNECT) is succeeded.
  // This should not depend on inspect_http1_response().
  void check_upgrade_fulfilled();
  // Returns true if the upgrade is succeded as a result of the call
  // check_upgrade_fulfilled().  HTTP/2 Upgrade is excluded.
  bool get_upgraded() const;
  // Inspects HTTP/2 request.
  void inspect_http2_request();
  // Inspects HTTP/1 request.  This checks whether the request is
  // upgrade request and tranfer-encoding etc.
  void inspect_http1_request();
  // Returns true if the request is HTTP Upgrade for HTTP/2
  bool get_http2_upgrade_request() const;
  // Returns the value of HTTP2-Settings request header field.
  const std::string &get_http2_settings() const;

  // downstream request API
  const Request &request() const { return req_; }
  Request &request() { return req_; }

  // Count number of crumbled cookies
  size_t count_crumble_request_cookie();
  // Crumbles (split cookie by ";") in request_headers_ and adds them
  // in |nva|.  Headers::no_index is inherited.
  void crumble_request_cookie(std::vector<nghttp2_nv> &nva);
  // Assembles request cookies.  The opposite operation against
  // crumble_request_cookie().
  std::string assemble_request_cookie() const;

  void
  set_request_start_time(std::chrono::high_resolution_clock::time_point time);
  const std::chrono::high_resolution_clock::time_point &
  get_request_start_time() const;
  int push_request_headers();
  bool get_chunked_request() const;
  void set_chunked_request(bool f);
  int push_upload_data_chunk(const uint8_t *data, size_t datalen);
  int end_upload_data();
  // Validates that received request body length and content-length
  // matches.
  bool validate_request_recv_body_length() const;
  void set_request_downstream_host(std::string host);
  bool expect_response_body() const;
  enum {
    INITIAL,
    HEADER_COMPLETE,
    MSG_COMPLETE,
    STREAM_CLOSED,
    CONNECT_FAIL,
    IDLE,
    MSG_RESET,
    // header contains invalid header field.  We can safely send error
    // response (502) to a client.
    MSG_BAD_HEADER,
    // header fields in HTTP/1 request exceed the configuration limit.
    // This state is only transitioned from INITIAL state, and solely
    // used to signal 431 status code to the client.
    HTTP1_REQUEST_HEADER_TOO_LARGE,
  };
  void set_request_state(int state);
  int get_request_state() const;
  DefaultMemchunks *get_request_buf();
  void set_request_pending(bool f);
  bool get_request_pending() const;
  // Returns true if request is ready to be submitted to downstream.
  bool request_submission_ready() const;

  // downstream response API
  const Response &response() const { return resp_; }
  Response &response() { return resp_; }

  // Rewrites the location response header field.
  void rewrite_location_response_header(const std::string &upstream_scheme);

  bool get_chunked_response() const;
  void set_chunked_response(bool f);

  void set_response_state(int state);
  int get_response_state() const;
  DefaultMemchunks *get_response_buf();
  bool response_buf_full();
  // Validates that received response body length and content-length
  // matches.
  bool validate_response_recv_body_length() const;
  uint32_t get_response_rst_stream_error_code() const;
  void set_response_rst_stream_error_code(uint32_t error_code);
  // Inspects HTTP/1 response.  This checks tranfer-encoding etc.
  void inspect_http1_response();
  // Clears some of member variables for response.
  void reset_response();
  // True if the response is non-final (1xx status code).  Note that
  // if connection was upgraded, 101 status code is treated as final.
  bool get_non_final_response() const;
  void set_expect_final_response(bool f);
  bool get_expect_final_response() const;

  // Call this method when there is incoming data in downstream
  // connection.
  int on_read();

  // Resets upstream read timer.  If it is active, timeout value is
  // reset.  If it is not active, timer will be started.
  void reset_upstream_rtimer();
  // Resets upstream write timer. If it is active, timeout value is
  // reset.  If it is not active, timer will be started.  This
  // function also resets read timer if it has been started.
  void reset_upstream_wtimer();
  // Makes sure that upstream write timer is started.  If it has been
  // started, do nothing.  Otherwise, write timer will be started.
  void ensure_upstream_wtimer();
  // Disables upstream read timer.
  void disable_upstream_rtimer();
  // Disables upstream write timer.
  void disable_upstream_wtimer();

  // Downstream timer functions.  They works in a similar way just
  // like the upstream timer function.
  void reset_downstream_rtimer();
  void reset_downstream_wtimer();
  void ensure_downstream_wtimer();
  void disable_downstream_rtimer();
  void disable_downstream_wtimer();

  // Returns true if accesslog can be written for this downstream.
  bool accesslog_ready() const;

  // Increment retry count
  void add_retry();
  // true if retry attempt should not be done.
  bool no_more_retry() const;

  int get_dispatch_state() const;
  void set_dispatch_state(int s);

  void attach_blocked_link(BlockedLink *l);
  BlockedLink *detach_blocked_link();

  // Returns true if downstream_connection can be detached and reused.
  bool can_detach_downstream_connection() const;

  DefaultMemchunks pop_response_buf();

  enum {
    EVENT_ERROR = 0x1,
    EVENT_TIMEOUT = 0x2,
  };

  enum {
    DISPATCH_NONE,
    DISPATCH_PENDING,
    DISPATCH_BLOCKED,
    DISPATCH_ACTIVE,
    DISPATCH_FAILURE,
  };

  Downstream *dlnext, *dlprev;

  // the length of response body sent to upstream client
  int64_t response_sent_body_length;

private:
  Request req_;
  Response resp_;

  std::chrono::high_resolution_clock::time_point request_start_time_;

  // host we requested to downstream.  This is used to rewrite
  // location header field to decide the location should be rewritten
  // or not.
  std::string request_downstream_host_;

  DefaultMemchunks request_buf_;
  DefaultMemchunks response_buf_;

  ev_timer upstream_rtimer_;
  ev_timer upstream_wtimer_;

  ev_timer downstream_rtimer_;
  ev_timer downstream_wtimer_;

  Upstream *upstream_;
  std::unique_ptr<DownstreamConnection> dconn_;

  // only used by HTTP/2 or SPDY upstream
  BlockedLink *blocked_link_;
  // How many times we tried in backend connection
  size_t num_retry_;
  // The stream ID in frontend connection
  int32_t stream_id_;
  // The associated stream ID in frontend connection if this is pushed
  // stream.
  int32_t assoc_stream_id_;
  // stream ID in backend connection
  int32_t downstream_stream_id_;
  // RST_STREAM error_code from downstream HTTP2 connection
  uint32_t response_rst_stream_error_code_;
  // request state
  int request_state_;
  // response state
  int response_state_;
  // only used by HTTP/2 or SPDY upstream
  int dispatch_state_;
  // true if the connection is upgraded (HTTP Upgrade or CONNECT),
  // excluding upgrade to HTTP/2.
  bool upgraded_;
  // true if backend request uses chunked transfer-encoding
  bool chunked_request_;
  // true if response to client uses chunked transfer-encoding
  bool chunked_response_;
  // true if we have not got final response code
  bool expect_final_response_;
  // true if downstream request is pending because backend connection
  // has not been established or should be checked before use;
  // currently used only with HTTP/2 connection.
  bool request_pending_;
};

} // namespace shrpx

#endif // SHRPX_DOWNSTREAM_H
