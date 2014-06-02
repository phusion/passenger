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
#ifndef _PASSENGER_APPLICATION_POOL_SOCKET_H_
#define _PASSENGER_APPLICATION_POOL_SOCKET_H_

#include <string>
#include <vector>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <climits>
#include <cassert>
#include <Logging.h>
#include <Utils/PriorityQueue.h>
#include <Utils/IOUtils.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


class Process;

struct Connection {
	int fd;
	bool persistent: 1;
	bool fail: 1;
	
	Connection() {
		fd = -1;
		persistent = false;
		fail = false;
	}
	
	void close() {
		if (fd != -1) {
			int fd2 = fd;
			fd = -1;
			persistent = false;
			safelyClose(fd2);
		}
	}
};

/**
 * Not thread-safe except for the connection pooling methods, so only use
 * within the ApplicationPool lock.
 */
class Socket {
private:
	boost::mutex connectionPoolLock;
	int totalConnections;
	vector<Connection> idleConnections;
	
	int connectionPoolLimit() const {
		return concurrency;
	}
	
	Connection connect() const {
		Connection connection;
		P_TRACE(3, "Connecting to " << address);
		connection.fd = connectToServer(address);
		return connection;
	}
	
public:
	// Read-only.
	string name;
	string address;
	string protocol;
	int concurrency;
	
	/** The handle inside the associated Process's 'sessionSockets' priority queue.
	 * Guaranteed to be valid as long as the Process is alive.
	 */
	PriorityQueue<Socket>::Handle pqHandle;
	
	/** Invariant: sessions >= 0 */
	int sessions;
	
	Socket()
		: concurrency(0)
		{ }
	
	Socket(const string &_name, const string &_address, const string &_protocol, int _concurrency)
		: totalConnections(0),
		  name(_name),
		  address(_address),
		  protocol(_protocol),
		  concurrency(_concurrency),
		  sessions(0)
		{ }
	
	Socket(const Socket &other)
		: totalConnections(other.totalConnections),
		  idleConnections(other.idleConnections),
		  name(other.name),
		  address(other.address),
		  protocol(other.protocol),
		  concurrency(other.concurrency),
		  pqHandle(other.pqHandle),
		  sessions(other.sessions)
		{ }
	
	Socket &operator=(const Socket &other) {
		totalConnections = other.totalConnections;
		idleConnections = other.idleConnections;
		name = other.name;
		address = other.address;
		protocol = other.protocol;
		concurrency = other.concurrency;
		pqHandle = other.pqHandle;
		sessions = other.sessions;
		return *this;
	}
	
	/**
	 * Connect to this socket or reuse an existing connection.
	 *
	 * One MUST call checkinConnection() when one's done using the Connection.
	 * Failure to do so will result in a resource leak.
	 */
	Connection checkoutConnection() {
		boost::lock_guard<boost::mutex> l(connectionPoolLock);
		
		if (!idleConnections.empty()) {
			Connection connection = idleConnections.back();
			idleConnections.pop_back();
			return connection;
		} else if (totalConnections < connectionPoolLimit()) {
			Connection connection = connect();
			connection.persistent = true;
			totalConnections++;
			return connection;
		} else {
			return connect();
		}
	}
	
	void checkinConnection(Connection connection) {
		boost::unique_lock<boost::mutex> l(connectionPoolLock);
		
		if (connection.persistent) {
			if (connection.fail) {
				totalConnections--;
				l.unlock();
				connection.close();
			} else {
				idleConnections.push_back(connection);
			}
		} else {
			l.unlock();
			connection.close();
		}
	}
	
	
	bool isIdle() const {
		return sessions == 0;
	}
	
	int busyness() const {
		/* Different sockets within a Process may have different
		 * 'concurrency' values. We want:
		 * - Process.sessionSockets to sort the sockets from least used to most used.
		 * - to give sockets with concurrency == 0 more priority over sockets
		 *   with concurrency > 0.
		 * Therefore, we describe our busyness as a percentage of 'concurrency', with
		 * the percentage value in [0..INT_MAX] instead of [0..1].
		 */
		if (concurrency == 0) {
			// Allows Process.sessionSockets to give
			// idle sockets more priority.
			if (sessions == 0) {
				return 0;
			} else {
				return 1;
			}
		} else {
			return (int) (((long long) sessions * INT_MAX) / (double) concurrency);
		}
	}
	
	bool isTotallyBusy() const {
		return concurrency != 0 && sessions >= concurrency;
	}
};

class SocketList: public vector<Socket> {
public:
	void add(const string &name, const string &address, const string &protocol, int concurrency) {
		push_back(Socket(name, address, protocol, concurrency));
	}

	const Socket *findSocketWithName(const StaticString &name) const {
		const_iterator it, end = this->end();
		for (it = begin(); it != end; it++) {
			if (it->name == name) {
				return &(*it);
			}
		}
		return NULL;
	}

	bool hasSessionSockets() const {
		const_iterator it;
		for (it = begin(); it != end(); it++) {
			if (it->protocol == "session" || it->protocol == "http_session") {
				return true;
			}
		}
		return false;
	}
};

typedef boost::shared_ptr<SocketList> SocketListPtr;


} // namespace ApplicationPool2
} // namespace Pasenger

#endif /* _PASSENGER_APPLICATION_POOL2_SOCKET_H_ */
