/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011 Phusion
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
#ifndef _PASSENGER_MULTI_LIBEIO_H_
#define _PASSENGER_MULTI_LIBEIO_H_

#include <boost/function.hpp>
#include <eio.h>
#include <SafeLibev.h>

namespace Passenger {

using namespace boost;


class MultiLibeio {
private:
	SafeLibevPtr libev;

public:
	typedef boost::function<void (eio_req *req)> ExecuteCallback;
	typedef boost::function<void (eio_req req)> Callback;

	static void init();
	static void shutdown();

	MultiLibeio() { }

	MultiLibeio(const SafeLibevPtr &_libev)
		: libev(_libev)
		{ }

	const SafeLibevPtr &getLibev() const {
		return libev;
	}

	eio_req *open(const char *path, int flags, mode_t mode, int pri, const Callback &callback);
	eio_req *read(int fd, void *buf, size_t length, off_t offset, int pri, const Callback &callback);
	eio_req *write(int fd, void *buf, size_t length, off_t offset, int pri, const Callback &callback);
	eio_req *custom(const ExecuteCallback &execute, int pri, const Callback &callback);
};


} // namespace Passenger

#endif /* _PASSENGER_MULTI_LIBEIO_H_ */
