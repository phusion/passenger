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
#ifndef H2LOAD_SPDY_SESSION_H
#define H2LOAD_SPDY_SESSION_H

#include "h2load_session.h"

#include <spdylay/spdylay.h>

#include "util.h"

namespace h2load {

struct Client;

class SpdySession : public Session {
public:
  SpdySession(Client *client, uint16_t spdy_version);
  virtual ~SpdySession();
  virtual void on_connect();
  virtual int submit_request();
  virtual int on_read(const uint8_t *data, size_t len);
  virtual int on_write();
  virtual void terminate();
  void handle_window_update(int32_t stream_id, size_t recvlen);

private:
  Client *client_;
  spdylay_session *session_;
  uint16_t spdy_version_;
};

} // namespace h2load

#endif // H2LOAD_SPDY_SESSION_H
