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
#ifndef SHRPX_HTTP2_UPSTREAM_H
#define SHRPX_HTTP2_UPSTREAM_H

#include "shrpx.h"

#include <memory>

#include <ev.h>

#include <nghttp2/nghttp2.h>

#include "shrpx_upstream.h"
#include "shrpx_downstream_queue.h"
#include "memchunk.h"
#include "buffer.h"

using namespace nghttp2;

namespace shrpx {

class ClientHandler;
class HttpsUpstream;

class Http2Upstream : public Upstream {
public:
  Http2Upstream(ClientHandler *handler);
  virtual ~Http2Upstream();
  virtual int on_read();
  virtual int on_write();
  virtual int on_timeout(Downstream *downstream);
  virtual int on_downstream_abort_request(Downstream *downstream,
                                          unsigned int status_code);
  virtual ClientHandler *get_client_handler() const;

  virtual int downstream_read(DownstreamConnection *dconn);
  virtual int downstream_write(DownstreamConnection *dconn);
  virtual int downstream_eof(DownstreamConnection *dconn);
  virtual int downstream_error(DownstreamConnection *dconn, int events);

  void add_pending_downstream(std::unique_ptr<Downstream> downstream);
  void remove_downstream(Downstream *downstream);

  int rst_stream(Downstream *downstream, uint32_t error_code);
  int terminate_session(uint32_t error_code);
  int error_reply(Downstream *downstream, unsigned int status_code);

  virtual void pause_read(IOCtrlReason reason);
  virtual int resume_read(IOCtrlReason reason, Downstream *downstream,
                          size_t consumed);

  virtual int on_downstream_header_complete(Downstream *downstream);
  virtual int on_downstream_body(Downstream *downstream, const uint8_t *data,
                                 size_t len, bool flush);
  virtual int on_downstream_body_complete(Downstream *downstream);

  virtual void on_handler_delete();
  virtual int on_downstream_reset(bool no_retry);
  virtual int send_reply(Downstream *downstream, const uint8_t *body,
                         size_t bodylen);
  virtual int initiate_push(Downstream *downstream, const char *uri,
                            size_t len);
  virtual int response_riovec(struct iovec *iov, int iovcnt) const;
  virtual void response_drain(size_t n);
  virtual bool response_empty() const;

  virtual Downstream *on_downstream_push_promise(Downstream *downstream,
                                                 int32_t promised_stream_id);
  virtual int
  on_downstream_push_promise_complete(Downstream *downstream,
                                      Downstream *promised_downstream);
  virtual bool push_enabled() const;
  virtual void cancel_premature_downstream(Downstream *promised_downstream);

  bool get_flow_control() const;
  // Perform HTTP/2 upgrade from |upstream|. On success, this object
  // takes ownership of the |upstream|. This function returns 0 if it
  // succeeds, or -1.
  int upgrade_upstream(HttpsUpstream *upstream);
  void start_settings_timer();
  void stop_settings_timer();
  int consume(int32_t stream_id, size_t len);
  void log_response_headers(Downstream *downstream,
                            const std::vector<nghttp2_nv> &nva) const;
  void start_downstream(Downstream *downstream);
  void initiate_downstream(Downstream *downstream);

  void submit_goaway();
  void check_shutdown();

  int prepare_push_promise(Downstream *downstream);
  int submit_push_promise(const std::string &scheme,
                          const std::string &authority, const std::string &path,
                          Downstream *downstream);

  int on_request_headers(Downstream *downstream, const nghttp2_frame *frame);

  using WriteBuffer = Buffer<32_k>;

  WriteBuffer *get_response_buf();

  void set_pending_data_downstream(Downstream *downstream, size_t n,
                                   size_t padlen);

  // Changes stream priority of |downstream|, which is assumed to be a
  // pushed stream.
  int adjust_pushed_stream_priority(Downstream *downstream);

private:
  WriteBuffer wb_;
  std::unique_ptr<HttpsUpstream> pre_upstream_;
  DownstreamQueue downstream_queue_;
  ev_timer settings_timer_;
  ev_timer shutdown_timer_;
  ev_prepare prep_;
  // A response buffer used to belong to Downstream object.  This is
  // moved here when response is partially written to wb_ in
  // send_data_callback, but before writing them all, Downstream
  // object was destroyed.  On destruction of Downstream,
  // pending_data_downstream_ becomes nullptr.
  DefaultMemchunks pending_response_buf_;
  // Downstream object whose DATA frame payload is partillay written
  // to wb_ in send_data_callback.  This field exists to keep track of
  // its lifetime.  When it is destroyed, its response buffer is
  // transferred to pending_response_buf_, and this field becomes
  // nullptr.
  Downstream *pending_data_downstream_;
  ClientHandler *handler_;
  nghttp2_session *session_;
  const uint8_t *data_pending_;
  // The length of lending data to be written into wb_.  If
  // data_pending_ is not nullptr, data_pending_ points to the data to
  // write.  Otherwise, pending_data_downstream_->get_response_buf()
  // if pending_data_downstream_ is not nullptr, or
  // pending_response_buf_ holds data to write.
  size_t data_pendinglen_;
  size_t padding_pendinglen_;
  bool flow_control_;
  bool shutdown_handled_;
};

nghttp2_session_callbacks *create_http2_upstream_callbacks();

} // namespace shrpx

#endif // SHRPX_HTTP2_UPSTREAM_H
