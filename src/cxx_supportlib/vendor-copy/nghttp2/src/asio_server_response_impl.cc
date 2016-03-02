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
#include "asio_server_response_impl.h"

#include "asio_server_stream.h"
#include "asio_server_request_impl.h"
#include "asio_server_http2_handler.h"
#include "asio_common.h"

#include "http2.h"

namespace nghttp2 {
namespace asio_http2 {
namespace server {

response_impl::response_impl()
    : strm_(nullptr), generator_cb_(deferred_generator()), status_code_(200),
      state_(response_state::INITIAL), pushed_(false),
      push_promise_sent_(false) {}

unsigned int response_impl::status_code() const { return status_code_; }

void response_impl::write_head(unsigned int status_code, header_map h) {
  if (state_ != response_state::INITIAL) {
    return;
  }

  status_code_ = status_code;
  header_ = std::move(h);

  state_ = response_state::HEADER_DONE;

  if (pushed_ && !push_promise_sent_) {
    return;
  }

  start_response();
}

void response_impl::end(std::string data) {
  end(string_generator(std::move(data)));
}

void response_impl::end(generator_cb cb) {
  if (state_ == response_state::BODY_STARTED) {
    return;
  }

  generator_cb_ = std::move(cb);

  if (state_ == response_state::INITIAL) {
    write_head(status_code_);
  } else {
    // generator_cb is changed, start writing in case it is deferred.
    auto handler = strm_->handler();
    handler->resume(*strm_);
  }

  state_ = response_state::BODY_STARTED;
}

void response_impl::write_trailer(header_map h) {
  auto handler = strm_->handler();
  handler->submit_trailer(*strm_, std::move(h));
}

void response_impl::start_response() {
  auto handler = strm_->handler();

  auto &req = strm_->request().impl();

  if (!::nghttp2::http2::expect_response_body(req.method(), status_code_)) {
    state_ = response_state::BODY_STARTED;
  }

  if (handler->start_response(*strm_) != 0) {
    handler->stream_error(strm_->get_stream_id(), NGHTTP2_INTERNAL_ERROR);
    return;
  }
}

void response_impl::on_close(close_cb cb) { close_cb_ = std::move(cb); }

void response_impl::call_on_close(uint32_t error_code) {
  if (close_cb_) {
    close_cb_(error_code);
  }
}

void response_impl::cancel(uint32_t error_code) {
  auto handler = strm_->handler();
  handler->stream_error(strm_->get_stream_id(), error_code);
}

response *response_impl::push(boost::system::error_code &ec, std::string method,
                              std::string raw_path_query, header_map h) const {
  auto handler = strm_->handler();
  return handler->push_promise(ec, *strm_, std::move(method),
                               std::move(raw_path_query), std::move(h));
}

void response_impl::resume() {
  auto handler = strm_->handler();
  handler->resume(*strm_);
}

boost::asio::io_service &response_impl::io_service() {
  return strm_->handler()->io_service();
}

void response_impl::pushed(bool f) { pushed_ = f; }

void response_impl::push_promise_sent() {
  if (push_promise_sent_) {
    return;
  }
  push_promise_sent_ = true;
  if (state_ == response_state::INITIAL) {
    return;
  }
  start_response();
}

const header_map &response_impl::header() const { return header_; }

void response_impl::stream(class stream *s) { strm_ = s; }

generator_cb::result_type
response_impl::call_read(uint8_t *data, std::size_t len, uint32_t *data_flags) {
  if (generator_cb_) {
    return generator_cb_(data, len, data_flags);
  }

  *data_flags |= NGHTTP2_DATA_FLAG_EOF;

  return 0;
}

} // namespace server
} // namespace asio_http2
} // namespace nghttp2
