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
#include "shrpx_log_config.h"
#include "util.h"

using namespace nghttp2;

namespace shrpx {

LogConfig::LogConfig()
    : accesslog_fd(-1), errorlog_fd(-1), errorlog_tty(false) {}

#ifndef NOTHREADS
static pthread_key_t lckey;
static pthread_once_t lckey_once = PTHREAD_ONCE_INIT;

static void make_key(void) { pthread_key_create(&lckey, NULL); }

LogConfig *log_config(void) {
  pthread_once(&lckey_once, make_key);
  LogConfig *config = (LogConfig *)pthread_getspecific(lckey);
  if (!config) {
    config = new LogConfig();
    pthread_setspecific(lckey, config);
  }
  return config;
}
#else
static LogConfig *config = new LogConfig();
LogConfig *log_config(void) { return config; }
#endif // NOTHREADS

void LogConfig::update_tstamp(
    const std::chrono::system_clock::time_point &now) {
  auto t0 = std::chrono::system_clock::to_time_t(time_str_updated_);
  auto t1 = std::chrono::system_clock::to_time_t(now);
  if (t0 == t1) {
    return;
  }

  time_str_updated_ = now;

  time_local_str = util::format_common_log(now);
  time_iso8601_str = util::format_iso8601(now);
  time_http_str = util::format_http_date(now);
}

} // namespace shrpx
