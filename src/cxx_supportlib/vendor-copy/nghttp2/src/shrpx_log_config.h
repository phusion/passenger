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
#ifndef SHRPX_LOG_CONFIG_H
#define SHRPX_LOG_CONFIG_H

#include "shrpx.h"

#include <chrono>

namespace shrpx {

struct LogConfig {
  std::chrono::system_clock::time_point time_str_updated_;
  std::string time_local_str;
  std::string time_iso8601_str;
  std::string time_http_str;
  int accesslog_fd;
  int errorlog_fd;
  // true if errorlog_fd is referring to a terminal.
  bool errorlog_tty;

  LogConfig();
  void update_tstamp(const std::chrono::system_clock::time_point &now);
};

// We need LogConfig per thread to avoid data race around opening file
// descriptor for log files.
extern LogConfig *log_config(void);

} // namespace shrpx

#endif // SHRPX_LOG_CONFIG_H
