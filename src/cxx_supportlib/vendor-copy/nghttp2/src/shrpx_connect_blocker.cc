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
#include "shrpx_connect_blocker.h"

namespace shrpx {

namespace {
const ev_tstamp INITIAL_SLEEP = 2.;
} // namespace

namespace {
void connect_blocker_cb(struct ev_loop *loop, ev_timer *w, int revents) {
  if (LOG_ENABLED(INFO)) {
    LOG(INFO) << "unblock downstream connection";
  }
}
} // namespace

ConnectBlocker::ConnectBlocker(struct ev_loop *loop)
    : loop_(loop), sleep_(INITIAL_SLEEP) {
  ev_timer_init(&timer_, connect_blocker_cb, 0., 0.);
}

ConnectBlocker::~ConnectBlocker() { ev_timer_stop(loop_, &timer_); }

bool ConnectBlocker::blocked() const { return ev_is_active(&timer_); }

void ConnectBlocker::on_success() { sleep_ = INITIAL_SLEEP; }

void ConnectBlocker::on_failure() {
  if (ev_is_active(&timer_)) {
    return;
  }

  sleep_ = std::min(128., sleep_ * 2);

  LOG(WARN) << "connect failure, start sleeping " << sleep_;

  ev_timer_set(&timer_, sleep_, 0.);
  ev_timer_start(loop_, &timer_);
}

} // namespace shrpx
