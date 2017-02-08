/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL_ABSTRACT_SESSION_H_
#define _PASSENGER_APPLICATION_POOL_ABSTRACT_SESSION_H_

#include <sys/types.h>
#include <boost/atomic.hpp>
#include <boost/intrusive_ptr.hpp>
#include <StaticString.h>
#include <Shared/ApplicationPoolApiKey.h>

namespace Passenger {
namespace ApplicationPool2 {


/**
 * An abstract base class for Session so that unit tests can work with
 * a mocked version of it.
 */
class AbstractSession {
public:
	virtual ~AbstractSession() {}

	virtual void ref() const = 0;
	virtual void unref() const = 0;

	virtual pid_t getPid() const = 0;
	virtual StaticString getGupid() const = 0;
	virtual StaticString getProtocol() const = 0;
	virtual unsigned int getStickySessionId() const = 0;
	virtual const ApiKey &getApiKey() const = 0;
	virtual int fd() const = 0;
	virtual bool isClosed() const = 0;

	virtual void initiate(bool blocking = true) = 0;

	virtual void requestOOBW() { /* Do nothing */ }

	/**
	 * This Session object becomes fully unsable after closing.
	 */
	virtual void close(bool success, bool wantKeepAlive = false) = 0;
};


inline void
intrusive_ptr_add_ref(const AbstractSession *session) {
	session->ref();
}

inline void
intrusive_ptr_release(const AbstractSession *session) {
	session->unref();
}


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_ABSTRACT_SESSION_H_ */
