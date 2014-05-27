/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
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
 * Represents a connection to the logging server.
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

	/**
	 * Disconnect from the server. If the server sent an error response
	 * right before closing the connection, try to read it and return it
	 * through `errorResponse`. Returns whether an error response was read.
	 *
	 * Reading the error response might result in an exception, e.g.
	 * because of networking and protocol exceptions. In such an event,
	 * the connection is still guaranteed to be disconnected.
	 */
	bool disconnect(string &errorResponse) {
		if (!connected()) {
			return false;
		}

		ConnectionGuard guard(this);

		/* The server might send an "error" array message
		 * just before disconnecting. Try to read it.
		 */
		TRACE_POINT();
		vector<string> response;
		try {
			unsigned long long timeout = 20000000;
			while (true) {
				response = readArrayMessage(fd, &timeout);
			}
		} catch (const TimeoutException &) {
			/* This means that the last message isn't an array
			 * message or that the server didn't send it quickly
			 * enough. In any case, discard whatever previous
			 * array messages we were able to read because they're
			 * guaranteed not to be the error message we're expecting.
			 */
			response.clear();
		} catch (const SystemException &e) {
			/* We treat ECONNRESET the same as EOFException.
			 * Other errors are treated as TimeoutException.
			 */
			if (e.code() != ECONNRESET) {
				response.clear();
			}
		} catch (const EOFException &) {
			/* Do nothing. We've successfully read the last array message. */
		}

		UPDATE_TRACE_POINT();
		if (response.size() == 2 && response[0] == "error") {
			errorResponse = response[1];
			return true;
		} else {
			return false;
		}
	}

	/** Disconnect from the server, ignoring any error responses the
	 * server might have sent.
	 */
	void disconnect() {
		if (fd != -1) {
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			safelyClose(fd);
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
