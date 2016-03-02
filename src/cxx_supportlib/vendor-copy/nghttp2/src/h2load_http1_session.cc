/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2015 British Broadcasting Corporation
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
#include "h2load_http1_session.h"

#include <cassert>
#include <cerrno>

#include "h2load.h"
#include "util.h"
#include "template.h"

#include <iostream>
#include <fstream>

#include "http-parser/http_parser.h"

using namespace nghttp2;

namespace h2load {

Http1Session::Http1Session(Client *client)
    : stream_req_counter_(1), stream_resp_counter_(1), client_(client), htp_(),
      complete_(false) {
  http_parser_init(&htp_, HTTP_RESPONSE);
  htp_.data = this;
}

Http1Session::~Http1Session() {}

namespace {
// HTTP response message begin
int htp_msg_begincb(http_parser *htp) {
  auto session = static_cast<Http1Session *>(htp->data);

  if (session->stream_resp_counter_ >= session->stream_req_counter_) {
    return -1;
  }

  return 0;
}
} // namespace

namespace {
// HTTP response status code
int htp_statuscb(http_parser *htp, const char *at, size_t length) {
  auto session = static_cast<Http1Session *>(htp->data);
  auto client = session->get_client();
  client->on_status_code(session->stream_resp_counter_, htp->status_code);

  return 0;
}
} // namespace

namespace {
// HTTP response message complete
int htp_msg_completecb(http_parser *htp) {
  auto session = static_cast<Http1Session *>(htp->data);
  auto client = session->get_client();

  auto final = http_should_keep_alive(htp) == 0;
  auto req_stat = client->get_req_stat(session->stream_resp_counter_);

  assert(req_stat);

  client->on_stream_close(session->stream_resp_counter_, true, final);

  session->stream_resp_counter_ += 2;

  if (final) {
    http_parser_pause(htp, 1);
    // Connection is going down.  If we have still request to do,
    // create new connection and keep on doing the job.
    if (client->req_started < client->req_todo) {
      client->try_new_connection();
    }

    return 0;
  }

  return 0;
}
} // namespace

namespace {
int htp_hdr_keycb(http_parser *htp, const char *data, size_t len) {
  auto session = static_cast<Http1Session *>(htp->data);
  auto client = session->get_client();

  client->worker->stats.bytes_head += len;
  client->worker->stats.bytes_head_decomp += len;
  return 0;
}
} // namespace

namespace {
int htp_hdr_valcb(http_parser *htp, const char *data, size_t len) {
  auto session = static_cast<Http1Session *>(htp->data);
  auto client = session->get_client();

  client->worker->stats.bytes_head += len;
  client->worker->stats.bytes_head_decomp += len;
  return 0;
}
} // namespace

namespace {
int htp_body_cb(http_parser *htp, const char *data, size_t len) {
  auto session = static_cast<Http1Session *>(htp->data);
  auto client = session->get_client();

  client->record_ttfb();
  client->worker->stats.bytes_body += len;

  return 0;
}
} // namespace

namespace {
http_parser_settings htp_hooks = {
    htp_msg_begincb,   // http_cb      on_message_begin;
    nullptr,           // http_data_cb on_url;
    htp_statuscb,      // http_data_cb on_status;
    htp_hdr_keycb,     // http_data_cb on_header_field;
    htp_hdr_valcb,     // http_data_cb on_header_value;
    nullptr,           // http_cb      on_headers_complete;
    htp_body_cb,       // http_data_cb on_body;
    htp_msg_completecb // http_cb      on_message_complete;
};
} // namespace

void Http1Session::on_connect() { client_->signal_write(); }

int Http1Session::submit_request() {
  auto config = client_->worker->config;
  const auto &req = config->h1reqs[client_->reqidx];
  client_->reqidx++;

  if (client_->reqidx == config->h1reqs.size()) {
    client_->reqidx = 0;
  }

  client_->on_request(stream_req_counter_);

  auto req_stat = client_->get_req_stat(stream_req_counter_);

  client_->record_request_time(req_stat);
  client_->wb.write(req.c_str(), req.size());

  // increment for next request
  stream_req_counter_ += 2;

  return 0;
}

int Http1Session::on_read(const uint8_t *data, size_t len) {
  auto nread = http_parser_execute(&htp_, &htp_hooks,
                                   reinterpret_cast<const char *>(data), len);

  if (client_->worker->config->verbose) {
    std::cout.write(reinterpret_cast<const char *>(data), nread);
  }

  auto htperr = HTTP_PARSER_ERRNO(&htp_);

  if (htperr == HPE_PAUSED) {
    // pause is done only when connection: close is requested
    return -1;
  }

  if (htperr != HPE_OK) {
    std::cerr << "[ERROR] HTTP parse error: "
              << "(" << http_errno_name(htperr) << ") "
              << http_errno_description(htperr) << std::endl;
    return -1;
  }

  return 0;
}

int Http1Session::on_write() {
  if (complete_) {
    return -1;
  }
  return 0;
}

void Http1Session::terminate() { complete_ = true; }

Client *Http1Session::get_client() { return client_; }

} // namespace h2load
