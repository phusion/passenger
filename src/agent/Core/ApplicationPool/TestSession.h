/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#include <utility>
#include <cassert>
#include <cstring>
#include <unistd.h>
#include <LoggingKit/Logging.h>
#include <IOTools/IOUtils.h>
#include <IOTools/BufferedIO.h>
#include <StrIntTools/StrIntUtils.h>
#include <Core/ApplicationPool/AbstractSession.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <FileDescriptor.h>
#include <Utils.h>
#include <Utils/ScopeGuard.h>
#include <FileTools/FileManip.h>
#include <oxt/system_calls.hpp>

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
	mutable bool forcingNonInstantConnect;

public:
	TestSession()
		: refcount(1),
		  pid(123),
		  gupid("gupid-123"),
		  protocol("session"),
		  stickySessionId(0),
		  closed(false),
		  success(false),
		  wantKeepAlive(false),
		  forcingNonInstantConnect(false)
		{ }

	virtual void ref() const override {
		boost::lock_guard<boost::mutex> l(syncher);
		assert(refcount > 0);
		refcount++;
	}

	virtual void unref() const override {
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

	virtual pid_t getPid() const override {
		boost::lock_guard<boost::mutex> l(syncher);
		return pid;
	}

	void setPid(pid_t p) {
		boost::lock_guard<boost::mutex> l(syncher);
		pid = p;
	}

	virtual StaticString getGupid() const override {
		boost::lock_guard<boost::mutex> l(syncher);
		return gupid;
	}

	void setGupid(const string &v) {
		boost::lock_guard<boost::mutex> l(syncher);
		gupid = v;
	}

	virtual StaticString getProtocol() const override {
		boost::lock_guard<boost::mutex> l(syncher);
		return protocol;
	}

	void setProtocol(const string &v) {
		boost::lock_guard<boost::mutex> l(syncher);
		protocol = v;
	}

	virtual unsigned int getStickySessionId() const override {
		boost::lock_guard<boost::mutex> l(syncher);
		return stickySessionId;
	}

	void setStickySessionId(unsigned int v) {
		boost::lock_guard<boost::mutex> l(syncher);
		stickySessionId = v;
	}

	virtual const ApiKey &getApiKey() const override {
		return apiKey;
	}

	virtual int fd() const override {
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

	virtual bool isClosed() const override {
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

	void forceNonInstantConnect() {
		boost::lock_guard<boost::mutex> l(syncher);
		forcingNonInstantConnect = true;
	}

	virtual bool initiate() override {
		boost::lock_guard<boost::mutex> l(syncher);

		// Create a unique temporary directory for the socket
		string tempDirPathTemplate = StaticString(getSystemTempDir()) + "/passenger.session.XXXXXX";
		DynamicBuffer tempDirPath(tempDirPathTemplate.size() + 1);
		memcpy(tempDirPath.data, tempDirPathTemplate.c_str(), tempDirPathTemplate.size() + 1);
		if (mkdtemp(tempDirPath.data) == NULL) {
			int e = errno;
			throw SystemException("Cannot create temporary directory", e);
		}
		ScopeGuard g([&]() {
			try {
				removeDirTree(tempDirPath.data);
			} catch (const std::exception &e) {
				P_ERROR("Error deleting temporary directory " << tempDirPath.data << ": " << e.what());
			}
		});

		// Create server socket
		string socketPath = StaticString(tempDirPath.data) + "/socket";
		FileDescriptor serverFd(createUnixServer(socketPath.c_str(), 0, true, __FILE__, __LINE__),
								__FILE__, __LINE__);

		// Create client socket (non-blocking)
		NUnix_State clientState;
		setupNonBlockingUnixSocket(clientState, socketPath, __FILE__, __LINE__);
		bool immediatelyConnected = connectToUnixServer(clientState);
		connection.first = std::move(clientState.fd);

		// Accept connection (blocking)
		FileDescriptor serverSideFd(oxt::syscalls::accept(serverFd, NULL, NULL),
			__FILE__, __LINE__);
		if (serverSideFd == -1) {
			int e = errno;
			throw SystemException("Cannot accept connection", e);
		}

		// Store the server-side fd.
		connection.second = std::move(serverSideFd);
		peerBufferedIO = BufferedIO(connection.second);

		if (forcingNonInstantConnect) {
			return false;
		} else {
			return immediatelyConnected;
		}
	}

	virtual void close(bool _success, bool _wantKeepAlive = false) override {
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
