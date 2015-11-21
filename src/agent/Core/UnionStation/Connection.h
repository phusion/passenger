/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_UNION_STATION_CONNECTION_H_
#define _PASSENGER_UNION_STATION_CONNECTION_H_

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <string>
#include <vector>

#include <errno.h>

#include <Logging.h>
#include <Exceptions.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>

namespace Passenger {
namespace UnionStation {

using namespace std;
using namespace boost;


struct Connection;
typedef boost::shared_ptr<Connection> ConnectionPtr;

inline void _disconnectConnection(Connection *connection);


/**
 * A scope guard which closes the given Connection on destruction unless cleared.
 * Note that this class does not hold a shared_ptr to the Connection object,
 * so make sure that the Connection outlives the guard object.
 */
class ConnectionGuard: public boost::noncopyable {
private:
	Connection * const connection;
	bool cleared;

public:
	ConnectionGuard(Connection *_connection)
		: connection(_connection),
		  cleared(false)
		{ }

	~ConnectionGuard() {
		if (!cleared) {
			_disconnectConnection(connection);
		}
	}

	void clear() {
		cleared = true;
	}
};


/**
 * Represents a connection to the UstRouter.
 * All access to the file descriptor must be synchronized through the syncher.
 * You can use the ConnectionLock to do that.
 */
struct Connection: public boost::noncopyable {
	mutable boost::mutex syncher;
	int fd;

	Connection(int _fd)
		: fd(_fd)
		{ }

	~Connection() {
		disconnect();
	}

	bool connected() const {
		return fd != -1;
	}

	void disconnect() {
		if (fd != -1) {
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			safelyClose(fd);
			P_LOG_FILE_DESCRIPTOR_CLOSE(fd);
			fd = -1;
		}
	}
};

typedef boost::shared_ptr<Connection> ConnectionPtr;


/**
 * A special lock type for Connection that also keeps a smart
 * pointer to the data structure so that the mutex is not destroyed
 * prematurely.
 */
struct ConnectionLock: public boost::noncopyable {
	ConnectionPtr connection;
	bool locked;

	ConnectionLock(const ConnectionPtr &c)
		: connection(c)
	{
		c->syncher.lock();
		locked = true;
	}

	~ConnectionLock() {
		if (locked) {
			connection->syncher.unlock();
		}
	}

	void reset(const ConnectionPtr &c, bool lockNow = true) {
		if (locked) {
			connection->syncher.unlock();
		}
		connection = c;
		if (lockNow) {
			connection->syncher.lock();
			locked = true;
		} else {
			locked = false;
		}
	}

	void lock() {
		assert(!locked);
		connection->syncher.lock();
		locked = true;
	}
};


inline void
_disconnectConnection(Connection *connection) {
	connection->disconnect();
}


} // namespace UnionStation
} // namespace Passenger

#endif /* _PASSENGER_UNION_STATION_CONNECTION_H_ */
