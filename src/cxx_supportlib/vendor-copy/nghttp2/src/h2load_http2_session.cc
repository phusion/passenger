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
#include "h2load_http2_session.h"

#include <cassert>
#include <cerrno>

#include "h2load.h"
#include "util.h"
#include "template.h"

using namespace nghttp2;

namespace h2load {

Http2Session::Http2Session(Client *client)
    : client_(client), session_(nullptr) {}

Http2Session::~Http2Session() { nghttp2_session_del(session_); }

namespace {
int on_header_callback(nghttp2_session *session, const nghttp2_frame *frame,
                       const uint8_t *name, size_t namelen,
                       const uint8_t *value, size_t valuelen, uint8_t flags,
                       void *user_data) {
  auto client = static_cast<Client *>(user_data);
  if (frame->hd.type != NGHTTP2_HEADERS ||
      frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
    return 0;
  }
  client->on_header(frame->hd.stream_id, name, namelen, value, valuelen);
  client->worker->stats.bytes_head_decomp += namelen + valuelen;
  return 0;
}
} // namespace

namespace {
int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                           void *user_data) {
  auto client = static_cast<Client *>(user_data);
  if (frame->hd.type != NGHTTP2_HEADERS ||
      frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
    return 0;
  }
  client->worker->stats.bytes_head +=
      frame->hd.length - frame->headers.padlen -
      ((frame->hd.flags & NGHTTP2_FLAG_PRIORITY) ? 5 : 0);
  if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
    client->record_ttfb();
  }
  return 0;
}
} // namespace

namespace {
int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                int32_t stream_id, const uint8_t *data,
                                size_t len, void *user_data) {
  auto client = static_cast<Client *>(user_data);
  client->record_ttfb();
  client->worker->stats.bytes_body += len;
  return 0;
}
} // namespace

namespace {
int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                             uint32_t error_code, void *user_data) {
  auto client = static_cast<Client *>(user_data);
  client->on_stream_close(stream_id, error_code == NGHTTP2_NO_ERROR);

  return 0;
}
} // namespace

namespace {
int on_frame_not_send_callback(nghttp2_session *session,
                               const nghttp2_frame *frame, int lib_error_code,
                               void *user_data) {
  if (frame->hd.type != NGHTTP2_HEADERS ||
      frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
    return 0;
  }

  auto client = static_cast<Client *>(user_data);
  // request was not sent.  Mark it as error.
  client->on_stream_close(frame->hd.stream_id, false);

  return 0;
}
} // namespace

namespace {
int before_frame_send_callback(nghttp2_session *session,
                               const nghttp2_frame *frame, void *user_data) {
  if (frame->hd.type != NGHTTP2_HEADERS ||
      frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
    return 0;
  }

  auto client = static_cast<Client *>(user_data);
  auto req_stat = client->get_req_stat(frame->hd.stream_id);
  assert(req_stat);
  client->record_request_time(req_stat);

  return 0;
}
} // namespace

namespace {
ssize_t file_read_callback(nghttp2_session *session, int32_t stream_id,
                           uint8_t *buf, size_t length, uint32_t *data_flags,
                           nghttp2_data_source *source, void *user_data) {
  auto client = static_cast<Client *>(user_data);
  auto config = client->worker->config;
  auto req_stat = client->get_req_stat(stream_id);
  assert(req_stat);
  ssize_t nread;
  while ((nread = pread(config->data_fd, buf, length, req_stat->data_offset)) ==
             -1 &&
         errno == EINTR)
    ;

  if (nread == -1) {
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }

  req_stat->data_offset += nread;

  if (nread == 0 || req_stat->data_offset == config->data_length) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
  }

  return nread;
}

} // namespace

namespace {
ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                      size_t length, int flags, void *user_data) {
  auto client = static_cast<Client *>(user_data);
  auto &wb = client->wb;

  if (wb.wleft() == 0) {
    return NGHTTP2_ERR_WOULDBLOCK;
  }

  return wb.write(data, length);
}
} // namespace

void Http2Session::on_connect() {
  int rv;

  nghttp2_session_callbacks *callbacks;

  nghttp2_session_callbacks_new(&callbacks);

  auto callbacks_deleter = defer(nghttp2_session_callbacks_del, callbacks);

  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
                                                       on_frame_recv_callback);

  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks, on_data_chunk_recv_callback);

  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks, on_stream_close_callback);

  nghttp2_session_callbacks_set_on_header_callback(callbacks,
                                                   on_header_callback);

  nghttp2_session_callbacks_set_on_frame_not_send_callback(
      callbacks, on_frame_not_send_callback);

  nghttp2_session_callbacks_set_before_frame_send_callback(
      callbacks, before_frame_send_callback);

  nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);

  nghttp2_session_client_new(&session_, callbacks, client_);

  std::array<nghttp2_settings_entry, 2> iv;
  iv[0].settings_id = NGHTTP2_SETTINGS_ENABLE_PUSH;
  iv[0].value = 0;
  iv[1].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
  iv[1].value = (1 << client_->worker->config->window_bits) - 1;

  rv = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv.data(),
                               iv.size());

  assert(rv == 0);

  auto extra_connection_window =
      (1 << client_->worker->config->connection_window_bits) - 1 -
      NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE;
  if (extra_connection_window != 0) {
    nghttp2_submit_window_update(session_, NGHTTP2_FLAG_NONE, 0,
                                 extra_connection_window);
  }

  client_->signal_write();
}

int Http2Session::submit_request() {
  if (nghttp2_session_check_request_allowed(session_) == 0) {
    return -1;
  }

  auto config = client_->worker->config;
  auto &nva = config->nva[client_->reqidx++];

  if (client_->reqidx == config->nva.size()) {
    client_->reqidx = 0;
  }

  nghttp2_data_provider prd{{0}, file_read_callback};

  auto stream_id =
      nghttp2_submit_request(session_, nullptr, nva.data(), nva.size(),
                             config->data_fd == -1 ? nullptr : &prd, nullptr);
  if (stream_id < 0) {
    return -1;
  }

  client_->on_request(stream_id);

  return 0;
}

int Http2Session::on_read(const uint8_t *data, size_t len) {
  auto rv = nghttp2_session_mem_recv(session_, data, len);
  if (rv < 0) {
    return -1;
  }

  assert(static_cast<size_t>(rv) == len);

  if (nghttp2_session_want_read(session_) == 0 &&
      nghttp2_session_want_write(session_) == 0 && client_->wb.rleft() == 0) {
    return -1;
  }

  client_->signal_write();

  return 0;
}

int Http2Session::on_write() {
  auto rv = nghttp2_session_send(session_);
  if (rv != 0) {
    return -1;
  }

  if (nghttp2_session_want_read(session_) == 0 &&
      nghttp2_session_want_write(session_) == 0 && client_->wb.rleft() == 0) {
    return -1;
  }

  return 0;
}

void Http2Session::terminate() {
  nghttp2_session_terminate_session(session_, NGHTTP2_NO_ERROR);
}

} // namespace h2load
