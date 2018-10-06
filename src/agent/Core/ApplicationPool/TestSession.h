/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL_TEST_SESSION_H_
#define _PASSENGER_APPLICATION_POOL_TEST_SESSION_H_

#include <boost/thread.hpp>
#include <string>
#include <cassert>
#include <IOTools/IOUtils.h>
#include <IOTools/BufferedIO.h>
#include <Core/ApplicationPool/AbstractSession.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;

/**
 * The TestSession represents a session between the Core to the Application. There is a
 * connection between the two, which is represented by a SocketPair having a first (Core side)
 * and second (Application side) FD. These are also referred to as fd and peerFd.
 */
class TestSession: public AbstractSession {
private:
	mutable boost::mutex syncher;
	mutable unsigned int refcount;
	pid_t pid;
	string gupid;
	string protocol;
	ApiKey apiKey;
	SocketPair connection;
	BufferedIO peerBufferedIO;
	unsigned int stickySessionId;
	mutable bool closed;
	mutable bool success;
	mutable bool wantKeepAlive;

public:
	TestSession()
		: refcount(1),
		  pid(123),
		  gupid("gupid-123"),
		  protocol("session"),
		  stickySessionId(0),
		  closed(false),
		  success(false),
		  wantKeepAlive(false)
		{ }

	virtual void ref() const {
		boost::lock_guard<boost::mutex> l(syncher);
		assert(refcount > 0);
		refcount++;
	}

	virtual void unref() const {
		boost::lock_guard<boost::mutex> l(syncher);
		assert(refcount > 0);
		refcount--;
		if (refcount == 0) {
			if (!closed) {
				closed = true;
				success = false;
				wantKeepAlive = false;
			}
		}
	}

	virtual pid_t getPid() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return pid;
	}

	void setPid(pid_t p) {
		boost::lock_guard<boost::mutex> l(syncher);
		pid = p;
	}

	virtual StaticString getGupid() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return gupid;
	}

	void setGupid(const string &v) {
		boost::lock_guard<boost::mutex> l(syncher);
		gupid = v;
	}

	virtual StaticString getProtocol() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return protocol;
	}

	void setProtocol(const string &v) {
		boost::lock_guard<boost::mutex> l(syncher);
		protocol = v;
	}

	virtual unsigned int getStickySessionId() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return stickySessionId;
	}

	void setStickySessionId(unsigned int v) {
		boost::lock_guard<boost::mutex> l(syncher);
		stickySessionId = v;
	}

	virtual const ApiKey &getApiKey() const {
		return apiKey;
	}

	virtual int fd() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return connection.first;
	}

	virtual int peerFd() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return connection.second;
	}

	virtual BufferedIO &getPeerBufferedIO() {
		boost::lock_guard<boost::mutex> l(syncher);
		return peerBufferedIO;
	}

	virtual bool isClosed() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return closed;
	}

	bool isSuccessful() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return success;
	}

	bool wantsKeepAlive() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return wantKeepAlive;
	}

	virtual void initiate(bool blocking = true) {
		boost::lock_guard<boost::mutex> l(syncher);
		connection = createUnixSocketPair(__FILE__, __LINE__);
		peerBufferedIO = BufferedIO(connection.second);
		if (!blocking) {
			setNonBlocking(connection.first);
		}
	}

	virtual void close(bool _success, bool _wantKeepAlive = false) {
		boost::lock_guard<boost::mutex> l(syncher);
		closed = true;
		success = _success;
		wantKeepAlive = _wantKeepAlive;
	}

	void closePeerFd() {
		boost::lock_guard<boost::mutex> l(syncher);
		connection.second.close();
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_TEST_SESSION_H_ */
