/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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

#include <boost/shared_ptr.hpp>
#include <apr_buckets.h>
#include "Session.h"

namespace Passenger {

using namespace boost;

struct PassengerBucketState {
	/** The number of bytes that this PassengerBucket has read so far. */
	unsigned long bytesRead;
	
	/** Whether this PassengerBucket is completed, i.e. no more data
	 * can be read from the underlying file descriptor. When true,
	 * this can either mean that EOF has been reached, or that an I/O
	 * error occured. Use errorCode to check whether an error occurred.
	 */
	bool completed;
	
	/** When completed is true, errorCode contains the errno value of
	 * the last read() call.
	 *
	 * A value of 0 means that no error occured.
	 */
	int errorCode;
	
	PassengerBucketState() {
		bytesRead = 0;
		completed = false;
		errorCode = 0;
	}
};

typedef shared_ptr<PassengerBucketState> PassengerBucketStatePtr;

/**
 * We used to use an apr_bucket_pipe for forwarding the backend process's
 * response to the HTTP client. However, apr_bucket_pipe has a number of
 * issues:
 * - It closes the pipe's file descriptor when it has reached
 *   end-of-stream, but not when an error has occurred. This behavior is
 *   undesirable because it can easily cause file descriptor leaks.
 * - It does weird non-blocking-I/O related things which can cause it
 *   to read less data than can actually be read.
 *
 * PassengerBucket is like apr_bucket_pipe, but:
 * - It also holds a reference to a Session. When a read error has occured
 *   or when end-of-stream has been reached, the Session will be
 *   dereferenced, so that the underlying file descriptor is closed.
 * - It ignores the APR_NONBLOCK_READ flag because that's known to cause
 *   strange I/O problems.
 * - It can store its current state in a PassengerBucketState data structure.
 */
apr_bucket *passenger_bucket_create(SessionPtr session,
                                    PassengerBucketStatePtr state,
                                    apr_bucket_alloc_t *list);

} // namespace Passenger

#endif /* _PASSENGER_BUCKET_H_ */
