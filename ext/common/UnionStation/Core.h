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
#ifndef _PASSENGER_UNION_STATION_CORE_H_
#define _PASSENGER_UNION_STATION_CORE_H_

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread.hpp>
#include <oxt/backtrace.hpp>

#include <errno.h>

#include <string>
#include <vector>
#include <stdexcept>

#include <Logging.h>
#include <Exceptions.h>
#include <RandomGenerator.h>
#include <StaticString.h>
#include <UnionStation/Connection.h>
#include <UnionStation/Transaction.h>
#include <Utils.h>
#include <Utils/MessageIO.h>
#include <Utils/SystemTime.h>

namespace Passenger {
namespace UnionStation {

using namespace std;
using namespace boost;


class Core: public boost::enable_shared_from_this<Core> {
private:
	static const unsigned int CONNECTION_POOL_MAX_SIZE = 10;
	static const unsigned int TXN_ID_MAX_SIZE =
		2 * sizeof(unsigned int) +    // max hex timestamp size
		11 +                          // space for a random identifier
		1;                            // null terminator

	/**** Server information ****/
	const string serverAddress;
	const string username;
	const string password;
	const string nodeName;

	/**** Working objects ****/
	RandomGenerator randomGenerator;
	TransactionPtr nullTransaction;

	/********************** Connection handling fields **********************
	 * These fields are synchronized through the mutex. The contents
	 * of the conntection objects are not synchronized through this mutex,
	 * but through the Connection object's own mutex.
	 ************************************************************************/
	mutable boost::mutex syncher;
	vector<ConnectionPtr> connectionPool;
	/** How long to wait before reconnecting. */
	unsigned long long reconnectTimeout;
	/** Earliest time at which we should attempt a reconnect. Earlier attempts
	 * will fail. Calculated from reconnectTimeout.
	 */
	unsigned long long nextReconnectTime;

	static string determineNodeName(const string &givenNodeName) {
		if (givenNodeName.empty()) {
			return getHostName();
		} else {
			return givenNodeName;
		}
	}

	static bool isNetworkError(int code) {
		return code == EPIPE || code == ECONNREFUSED || code == ECONNRESET
			|| code == EHOSTUNREACH || code == ENETDOWN || code == ENETUNREACH
			|| code == ETIMEDOUT;
	}

	template<typename T>
	static bool instanceof(const std::exception &e) {
		return dynamic_cast<const T *>(&e) != NULL;
	}

	void initialize() {
		nullTransaction   = boost::make_shared<Transaction>();
		reconnectTimeout  = 1000000;
		nextReconnectTime = 0;
	}

	/** Creates a transaction ID string. `txnId` MUST be at least TXN_ID_MAX_SIZE bytes. */
	void createTxnId(char *txnId, char **txnIdEnd, unsigned long long timestamp) {
		unsigned int timestampSize;
		char *end;

		// "[timestamp]"
		// Our timestamp is like a Unix timestamp but with minutes
		// resolution instead of seconds. 32 bits will last us for
		// about 8000 years.
		timestampSize = integerToHexatri<unsigned int>(
			timestamp / 1000000 / 60,
			txnId);
		end = txnId + timestampSize;

		// "[timestamp]-"
		*end = '-';
		end++;

		// "[timestamp]-[random id]"
		randomGenerator.generateAsciiString(end, 11);
		end += 11;
		*end = '\0';
		*txnIdEnd = end;
	}

	ConnectionPtr createNewConnection() {
		TRACE_POINT();
		int fd;
		vector<string> args;
		unsigned long long timeout = 15000000;

		// Create socket.
		fd = connectToServer(serverAddress);
		FdGuard guard(fd, true);
		
		// Handshake: process protocol version number.
		if (!readArrayMessage(fd, args, &timeout)) {
			throw IOException("The logging agent closed the connection before sending a version identifier.");
		}
		if (args.size() != 2 || args[0] != "version") {
			throw IOException("The logging agent server didn't sent a valid version identifier.");
		}
		if (args[1] != "1") {
			string message = "Unsupported logging agent protocol version " +
				args[1] + ".";
			throw IOException(message);
		}

		// Handshake: authenticate.
		UPDATE_TRACE_POINT();
		writeScalarMessage(fd, username, &timeout);
		writeScalarMessage(fd, password, &timeout);

		UPDATE_TRACE_POINT();
		if (!readArrayMessage(fd, args, &timeout)) {
			throw IOException("The logging agent did not send an authentication response.");
		} else if (args.size() != 1) {
			throw IOException("The authentication response that the logging agent sent is not valid.");
		} else if (args[0] != "ok") {
			throw SecurityException("The logging agent server denied authentication: " + args[0]);
		}

		// Initialize session.
		UPDATE_TRACE_POINT();
		writeArrayMessage(fd, &timeout, "init", nodeName.c_str(), NULL);
		if (!readArrayMessage(fd, args, &timeout)) {
			throw SystemException("Cannot connect to logging server", ECONNREFUSED);
		} else if (args.size() != 1) {
			throw IOException("Logging server returned an invalid reply for the 'init' command");
		} else if (args[0] == "server shutting down") {
			throw SystemException("Cannot connect to server", ECONNREFUSED);
		} else if (args[0] != "ok") {
			throw IOException("Logging server returned an invalid reply for the 'init' command");
		}

		ConnectionPtr connection = boost::make_shared<Connection>(fd);
		guard.clear();
		return connection;
	}

public:
	Core() {
		initialize();
	}

	Core(const string &_serverAddress, const string &_username,
	     const string &_password, const string &_nodeName = string())
		: serverAddress(_serverAddress),
		  username(_username),
		  password(_password),
		  nodeName(determineNodeName(_nodeName))
	{
		initialize();
	}


	/***** Connection pool methods *****/

	ConnectionPtr checkoutConnection() {
		TRACE_POINT();
		boost::unique_lock<boost::mutex> l(syncher);
		if (!connectionPool.empty()) {
			P_TRACE(3, "Checked out existing connection");
			ConnectionPtr connection = connectionPool.back();
			connectionPool.pop_back();
			return connection;

		} else {
			if (SystemTime::getUsec() < nextReconnectTime) {
				P_TRACE(3, "Not yet time to reconnect; returning NULL connection");
				return ConnectionPtr();
			}

			l.unlock();
			P_TRACE(3, "Creating new connection with logging agent");
			ConnectionPtr connection;
			try {
				connection = createNewConnection();
			} catch (const TimeoutException &) {
				l.lock();
				P_WARN("Timeout trying to connect to the logging agent at " << serverAddress << "; " <<
					"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return ConnectionPtr();
			} catch (const tracable_exception &e) {
				l.lock();
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				if (instanceof<IOException>(e) || instanceof<SystemException>(e)) {
					P_WARN("Cannot connect to the logging agent at " << serverAddress <<
						" (" << e.what() << "); will reconnect in " <<
						reconnectTimeout / 1000000 << " second(s).");
					return ConnectionPtr();
				} else {
					throw;
				}
			}

			return connection;
		}
	}

	void checkinConnection(const ConnectionPtr &connection) {
		boost::unique_lock<boost::mutex> l(syncher);
		if (connectionPool.size() < CONNECTION_POOL_MAX_SIZE) {
			connectionPool.push_back(connection);
		} else {
			l.unlock();
			connection->disconnect();
		}
	}


	/***** Transaction methods *****/

	TransactionPtr createNullTransaction() const {
		return nullTransaction;
	}

	bool sendRequest(const ConnectionPtr &connection, StaticString args[],
		unsigned int nargs, bool expectAck)
	{
		ConnectionLock cl(connection);
		ConnectionGuard guard(connection.get());

		try {
			unsigned long long timeout = 15000000;

			writeArrayMessage(connection->fd, args, nargs, &timeout);

			if (expectAck) {
				vector<string> args;
				if (!readArrayMessage(connection->fd, args, &timeout)) {
					boost::lock_guard<boost::mutex> l(syncher);
					P_WARN("The logging agent at " << serverAddress <<
						" closed the connection (no error message given);" <<
						" will reconnect in " << reconnectTimeout / 1000000 <<
						" second(s).");
					nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
					return false;
				} else if (args.size() == 2 && args[0] == "error") {
					boost::lock_guard<boost::mutex> l(syncher);
					P_WARN("The logging agent at " << serverAddress <<
						" closed the connection (error message: " << args[1] <<
						"); will reconnect in " << reconnectTimeout / 1000000 <<
						" second(s).");
					nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
					return false;
				} else if (args.empty() || args[0] != "ok") {
					boost::lock_guard<boost::mutex> l(syncher);
					P_WARN("The logging agent at " << serverAddress <<
						" sent an unexpected reply;" <<
						" will reconnect in " << reconnectTimeout / 1000000 <<
						" second(s).");
					nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
					return false;
				}
			}

			guard.clear();
			return true;

		} catch (const TimeoutException &) {
			boost::lock_guard<boost::mutex> l(syncher);
			P_WARN("Timeout trying to communicate with the logging agent at " << serverAddress << "; " <<
				"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
			nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
			return false;

		} catch (const SystemException &e) {
			if (e.code() == ENOENT || isNetworkError(e.code())) {
				string errorResponse;
				bool gotErrorResponse;

				guard.clear();
				gotErrorResponse = connection->disconnect(errorResponse);
				boost::lock_guard<boost::mutex> l(syncher);
				if (gotErrorResponse) {
					P_WARN("The logging agent at " << serverAddress <<
						" closed the connection (error message: " << errorResponse <<
						"); will reconnect in " << reconnectTimeout / 1000000 <<
						" second(s).");
				} else {
					P_WARN("The logging agent at " << serverAddress <<
						" closed the connection (no error message given);" <<
						" will reconnect in " << reconnectTimeout / 1000000 <<
						" second(s).");
				}
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return false;
			} else {
				throw;
			}
		}
	}

	TransactionPtr newTransaction(const string &groupName,
		const string &category = "requests",
		const string &unionStationKey = "-",
		const string &filters = string())
	{
		if (isNull()) {
			return createNullTransaction();
		}

		// Prepare parameters.
		unsigned long long timestamp = SystemTime::getUsec();
		char txnId[TXN_ID_MAX_SIZE], *txnIdEnd;
		char timestampStr[2 * sizeof(unsigned long long) + 1];

		createTxnId(txnId, &txnIdEnd, timestamp);
		integerToHexatri<unsigned long long>(timestamp, timestampStr);

		StaticString params[] = {
			StaticString("openTransaction", sizeof("openTransaction") - 1),
			StaticString(txnId, txnIdEnd - txnId),
			groupName,
			// empty nodeName, implies using the default
			// nodeName passed during initialization
			StaticString(),
			category,
			timestampStr,
			unionStationKey,
			StaticString("true", 4), // crashProtect
			StaticString("true", 4), // ack
			filters
		};
		unsigned int nparams = sizeof(params) / sizeof(StaticString);

		// Get a connection to the logging server.
		ConnectionPtr connection = checkoutConnection();
		if (connection == NULL) {
			P_TRACE(2, "Created NULL Union Station transaction: group=" << groupName <<
				", category=" << category << ", txnId=" <<
				StaticString(txnId, txnIdEnd - txnId));
			return createNullTransaction();
		}

		// Send request, process reply.
		if (sendRequest(connection, params, nparams, true)) {
			ConnectionGuard guard(connection.get());
			TransactionPtr transaction = boost::make_shared<Transaction>(
				shared_from_this(),
				connection,
				string(txnId, txnIdEnd - txnId),
				groupName,
				category,
				unionStationKey);
			guard.clear();
			P_TRACE(2, "Created new Union Station transaction: group=" << groupName <<
				", category=" << category << ", txnId=" <<
				StaticString(txnId, txnIdEnd - txnId));
			return transaction;
		} else {
			P_TRACE(2, "Created NULL Union Station transaction: group=" << groupName <<
				", category=" << category << ", txnId=" <<
				StaticString(txnId, txnIdEnd - txnId));
			return createNullTransaction();
		}
	}

	TransactionPtr continueTransaction(const string &txnId,
		const string &groupName,
		const string &category = "requests",
		const string &unionStationKey = "-")
	{
		if (isNull() || txnId.empty()) {
			return createNullTransaction();
		}

		// Prepare parameters.
		char timestampStr[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(), timestampStr);

		StaticString params[] = {
			StaticString("openTransaction", sizeof("openTransaction") - 1),
			txnId,
			groupName,
			// empty nodeName, implies using the default
			// nodeName passed during initialization
			StaticString(),
			category,
			timestampStr,
			unionStationKey,
			StaticString("true", 4),  // crashProtect
			StaticString("false", 4)  // ack
		};
		unsigned int nparams = sizeof(params) / sizeof(StaticString);

		// Get a connection to the logging server.
		ConnectionPtr connection = checkoutConnection();
		if (connection == NULL) {
			return createNullTransaction();
		}

		// Send request.
		if (sendRequest(connection, params, nparams, false)) {
			ConnectionGuard guard(connection.get());
			TransactionPtr transaction = boost::make_shared<Transaction>(
				shared_from_this(),
				connection,
				txnId,
				groupName,
				category,
				unionStationKey);
			guard.clear();
			return transaction;
		} else {
			return createNullTransaction();
		}
	}


	/***** Parameter getters and setters *****/

	void setReconnectTimeout(unsigned long long usec) {
		boost::lock_guard<boost::mutex> l(syncher);
		reconnectTimeout = usec;
	}

	bool isNull() const {
		return serverAddress.empty();
	}

	const string &getAddress() const {
		return serverAddress;
	}

	const string &getUsername() const {
		return username;
	}

	const string &getPassword() const {
		return password;
	}

	/**
	 * @post !result.empty()
	 */
	const string &getNodeName() const {
		return nodeName;
	}
};


inline void
_checkinConnection(const CorePtr &core, const ConnectionPtr &connection) {
	core->checkinConnection(connection);
}


} // namespace UnionStation
} // namespace Passenger

#endif /* _PASSENGER_UNION_STATION_CORE_H_ */
