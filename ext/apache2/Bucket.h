/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_BUCKET_H_
#define _PASSENGER_BUCKET_H_

/**
 * apr_bucket_pipe closes a pipe's file descriptor when it has reached
 * end-of-stream, but not when an error has occurred. This behavior is
 * undesirable because it can easily cause file descriptor leaks.
 *
 * passenger_bucket is like apr_bucket_pipe, but it also holds a reference to
 * a Session. When a read error has occured or when end-of-stream has been
 * reached, the Session will be dereferenced, so that the underlying file
 * descriptor is closed.
 *
 * passenger_bucket also ignores the APR_NONBLOCK_READ flag because that's
 * known to cause strange I/O problems.
 */

#include <apr_buckets.h>
#include "Application.h"

apr_bucket *passenger_bucket_create(Passenger::Application::SessionPtr session,
                                    apr_file_t *pipe,
                                    apr_bucket_alloc_t *list);

#endif /* _PASSENGER_BUCKET_H_ */

