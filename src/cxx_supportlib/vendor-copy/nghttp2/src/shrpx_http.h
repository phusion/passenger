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
#ifndef SHRPX_HTTP_H
#define SHRPX_HTTP_H

#include "shrpx.h"

#include <string>

#include <nghttp2/nghttp2.h>

namespace shrpx {

namespace http {

std::string create_error_html(unsigned int status_code);

std::string create_via_header_value(int major, int minor);

// Returns generated RFC 7239 Forwarded header field value.  The
// |params| is bitwise-OR of zero or more of shrpx_forwarded_param
// defined in shrpx_config.h.
std::string create_forwarded(int params, const std::string &node_by,
                             const std::string &node_for,
                             const std::string &host, const std::string &proto);

// Adds ANSI color codes to HTTP headers |hdrs|.
std::string colorizeHeaders(const char *hdrs);

ssize_t select_padding_callback(nghttp2_session *session,
                                const nghttp2_frame *frame, size_t max_payload,
                                void *user_data);

} // namespace http

} // namespace shrpx

#endif // SHRPX_HTTP_H
