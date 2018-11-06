/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL_SOCKET_H_
#define _PASSENGER_APPLICATION_POOL_SOCKET_H_

#include <vector>
#include <oxt/macros.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/container/small_vector.hpp>
#include <climits>
#include <cassert>
#include <LoggingKit/LoggingKit.h>
#include <StaticString.h>
#include <MemoryKit/palloc.h>
#include <IOTools/IOUtils.h>
#include <Core/ApplicationPool/Common.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


struct Connection {
	int fd;
	bool wantKeepAlive: 1;
	bool fail: 1;
	bool blocking: 1;

	Connection()
		: fd(-1),
		  wantKeepAlive(false),
		  fail(false),
		  blocking(true)
		{ }

	void close() {
		if (fd != -1) {
			int fd2 = fd;
			fd = -1;
			wantKeepAlive = false;
			safelyClose(fd2);
			P_LOG_FILE_DESCRIPTOR_CLOSE(fd2);
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
	vector<Connection> idleConnections;

	OXT_FORCE_INLINE
	int connectionPoolLimit() const {
		return concurrency;
	}

	Connection connect() const {
		Connection connection;
		P_TRACE(3, "Connecting to " << address);
		connection.fd = connectToServer(address, __FILE__, __LINE__);
		connection.fail = true;
		connection.wantKeepAlive = false;
		connection.blocking = true;
		P_LOG_FILE_DESCRIPTOR_PURPOSE(connection.fd, "App " << pid << " connection");
		return connection;
	}

public:
	// Socket properties. Read-only.
	StaticString address;
	StaticString protocol;
	StaticString description;
	pid_t pid;
	/**
	 * Special values:
	 * 0 = unlimited concurrency
	 * -1 = unknown
	 */
	int concurrency;
	bool acceptHttpRequests;

	// Private. In public section as alignment optimization.
	int totalConnections;
	int totalIdleConnections;

	/** Invariant: sessions >= 0 */
	int sessions;

	Socket()
		: pid(-1),
		  concurrency(-1),
		  acceptHttpRequests(0)
		{ }

	Socket(pid_t _pid, const StaticString &_address, const StaticString &_protocol,
		const StaticString &_description, int _concurrency, bool _acceptHttpRequests)
		: address(_address),
		  protocol(_protocol),
		  description(_description),
		  pid(_pid),
		  concurrency(_concurrency),
		  acceptHttpRequests(_acceptHttpRequests),
		  totalConnections(0),
		  totalIdleConnections(0),
		  sessions(0)
		{ }

	Socket(const Socket &other)
		: idleConnections(other.idleConnections),
		  address(other.address),
		  protocol(other.protocol),
		  description(other.description),
		  pid(other.pid),
		  concurrency(other.concurrency),
		  acceptHttpRequests(other.acceptHttpRequests),
		  totalConnections(other.totalConnections),
		  totalIdleConnections(other.totalIdleConnections),
		  sessions(other.sessions)
		{ }

	Socket &operator=(const Socket &other) {
		totalConnections = other.totalConnections;
		totalIdleConnections = other.totalIdleConnections;
		idleConnections = other.idleConnections;
		address = other.address;
		protocol = other.protocol;
		description = other.description;
		pid = other.pid;
		concurrency = other.concurrency;
		acceptHttpRequests = other.acceptHttpRequests;
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
		boost::unique_lock<boost::mutex> l(connectionPoolLock);

		if (!idleConnections.empty()) {
			P_TRACE(3, "Socket " << address << ": checking out connection from connection pool (" <<
				idleConnections.size() << " -> " << (idleConnections.size() - 1) <<
				" items). Current total number of connections: " << totalConnections);
			Connection connection = idleConnections.back();
			idleConnections.pop_back();
			totalIdleConnections--;
			return connection;
		} else {
			Connection connection = connect();
			totalConnections++;
			P_TRACE(3, "Socket " << address << ": there are now " <<
				totalConnections << " total connections");
			l.unlock();
			return connection;
		}
	}

	void checkinConnection(Connection &connection) {
		boost::unique_lock<boost::mutex> l(connectionPoolLock);

		if (connection.fail || !connection.wantKeepAlive || totalIdleConnections >= connectionPoolLimit()) {
			totalConnections--;
			assert(totalConnections >= 0);
			P_TRACE(3, "Socket " << address << ": connection not checked back into "
				"connection pool. There are now " << totalConnections <<
				" connections in total");
			l.unlock();
			connection.close();
		} else {
			P_TRACE(3, "Socket " << address << ": checking in connection into connection pool (" <<
				totalIdleConnections << " -> " << (totalIdleConnections + 1) <<
				" items). Current total number of connections: " << totalConnections);
			totalIdleConnections++;
			idleConnections.push_back(connection);
		}
	}

	void closeAllConnections() {
		boost::unique_lock<boost::mutex> l(connectionPoolLock);
		assert(sessions == 0);
		assert(totalConnections == totalIdleConnections);
		vector<Connection>::iterator it, end = idleConnections.end();

		for (it = idleConnections.begin(); it != end; it++) {
			try {
				it->close();
			} catch (const SystemException &e) {
				P_ERROR("Cannot close a connection with socket " << address << ": " << e.what());
			}
		}
		idleConnections.clear();
		totalConnections = 0;
		totalIdleConnections = 0;
	}


	bool isIdle() const {
		return sessions == 0;
	}

	int busyness() const {
		/* Different sockets within a Process may have different
		 * 'concurrency' values. We want:
		 * - the socket with the smallest busyness to be be picked for routing.
		 * - to give sockets with concurrency == 0 or -1 more priority (in general)
		 *   over sockets with concurrency > 0.
		 * Therefore, in case of sockets with concurrency > 0, we describe our
		 * busyness as a percentage of 'concurrency', with the percentage value
		 * in [0..INT_MAX] instead of [0..1]. That way, the busyness value
		 * of sockets with concurrency > 0 is usually higher than that of sockets
		 * with concurrency == 0 or -1.
		 */
		if (concurrency <= 0) {
			return sessions;
		} else {
			return (int) (((long long) sessions * INT_MAX) / (double) concurrency);
		}
	}

	bool isTotallyBusy() const {
		return concurrency > 0 && sessions >= concurrency;
	}

	void recreateStrings(psg_pool_t *newPool) {
		recreateString(newPool, address);
		recreateString(newPool, protocol);
		recreateString(newPool, description);
	}
};

class SocketList: public boost::container::small_vector<Socket, 1> {
public:
	void add(pid_t pid, const StaticString &address, const StaticString &protocol,
		const StaticString &description, int concurrency, bool acceptHttpRequests)
	{
		push_back(Socket(pid, address, protocol, description, concurrency,
			acceptHttpRequests));
	}

	const Socket *findFirstSocketWithProtocol(const StaticString &protocol) const {
		const_iterator it, end = this->end();
		for (it = begin(); it != end; it++) {
			if (it->protocol == protocol) {
				return &(*it);
			}
		}
		return NULL;
	}
};

typedef boost::shared_ptr<SocketList> SocketListPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SOCKET_H_ */
