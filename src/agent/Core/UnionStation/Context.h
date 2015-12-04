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
#ifndef _PASSENGER_UNION_STATION_CONTEXT_H_
#define _PASSENGER_UNION_STATION_CONTEXT_H_

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
#include <StaticString.h>
#include <Utils.h>
#include <Utils/MessageIO.h>
#include <Utils/SystemTime.h>
#include <Core/UnionStation/Connection.h>
#include <Core/UnionStation/Transaction.h>

namespace Passenger {
namespace UnionStation {

using namespace std;
using namespace boost;


class Context: public boost::enable_shared_from_this<Context> {
private:
	static const unsigned int CONNECTION_POOL_MAX_SIZE = 10;

	/**** Server information ****/
	const string serverAddress;
	const string username;
	const string password;
	const string nodeName;

	/**** Working objects ****/
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

	ConnectionPtr createNewConnection() {
		TRACE_POINT();
		int fd;
		vector<string> args;
		unsigned long long timeout = 15000000;

		// Create socket.
		fd = connectToServer(serverAddress, __FILE__, __LINE__);
		FdGuard guard(fd, NULL, 0, true);

		P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, "Connection to " SHORT_PROGRAM_NAME " UstRouter");

		// Handshake: process protocol version number.
		if (!readArrayMessage(fd, args, &timeout)) {
			throw IOException("The UstRouter closed the connection before sending a version identifier");
		}
		if (args.size() != 2 || args[0] != "version") {
			throw IOException("The UstRouter didn't sent a valid version identifier");
		}
		if (args[1] != "1") {
			string message = "Unsupported UstRouter protocol version " +
				args[1] + ".";
			throw IOException(message);
		}

		// Handshake: authenticate.
		UPDATE_TRACE_POINT();
		writeScalarMessage(fd, username, &timeout);
		writeScalarMessage(fd, password, &timeout);

		UPDATE_TRACE_POINT();
		if (!readArrayMessage(fd, args, &timeout)) {
			throw IOException("The UstRouter did not send an authentication response");
		} else if (args.size() < 2 || args[0] != "status") {
			throw IOException("The authentication response that the UstRouter sent is not valid");
		} else if (args[1] == "ok") {
			// Do nothing
		} else if (args[1] == "error") {
			if (args.size() >= 3) {
				throw SecurityException("The UstRouter denied authentication: " + args[2]);
			} else {
				throw SecurityException("The UstRouter denied authentication (no server message given)");
			}
		} else {
			throw IOException("The authentication response that the UstRouter sent is not valid");
		}

		// Initialize session.
		UPDATE_TRACE_POINT();
		if (nodeName.empty()) {
			writeArrayMessage(fd, &timeout, "init", NULL);
		} else {
			writeArrayMessage(fd, &timeout, "init", nodeName.c_str(), NULL);
		}
		if (!readArrayMessage(fd, args, &timeout)) {
			throw SystemException("Cannot connect to the UstRouter", ECONNREFUSED);
		} else if (args.size() < 2 || args[0] != "status") {
			throw IOException("The UstRouter returned an invalid reply for the 'init' command");
		} else if (args[1] == "ok") {
			// Do nothing
		} else if (args[1] == "error") {
			if (args.size() >= 3) {
				throw IOException("The UstRouter denied client initialization: " + args[2]);
			} else {
				throw IOException("The UstRouter denied client initialization (no server message given)");
			}
		} else {
			throw IOException("The UstRouter returned an invalid reply for the 'init' command");
		}

		ConnectionPtr connection = boost::make_shared<Connection>(fd);
		guard.clear();
		return connection;
	}

public:
	Context() {
		initialize();
	}

	Context(const string &_serverAddress, const string &_username,
	     const string &_password, const string &_nodeName = string())
		: serverAddress(_serverAddress),
		  username(_username),
		  password(_password),
		  nodeName(_nodeName)
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
			P_TRACE(3, "Creating new connection with UstRouter");
			ConnectionPtr connection;
			try {
				connection = createNewConnection();
			} catch (const TimeoutException &) {
				l.lock();
				P_WARN("Timeout trying to connect to the UstRouter at " << serverAddress << "; " <<
					"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return ConnectionPtr();
			} catch (const tracable_exception &e) {
				l.lock();
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				if (instanceof<IOException>(e) || instanceof<SystemException>(e)) {
					P_WARN("Cannot connect to the UstRouter at " << serverAddress <<
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

	void handleTimeout() {
		boost::lock_guard<boost::mutex> l(syncher);
		P_WARN("Timeout trying to communicate with the UstRouter at " <<
			serverAddress << "; " << "will reconnect in " <<
			reconnectTimeout / 1000000 << " second(s).");
		nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
	}

	void handleNetworkErrorOrThrow(const ConnectionPtr &connection,
		ConnectionGuard &guard, const SystemException &e)
	{
		if (e.code() == ENOENT || isNetworkError(e.code())) {
			guard.clear();
			boost::lock_guard<boost::mutex> l(syncher);
			P_WARN("The UstRouter at " << serverAddress <<
				" closed the connection (no error message given);" <<
				" will reconnect in " << reconnectTimeout / 1000000 <<
				" second(s).");
			nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
		} else {
			throw e;
		}
	}

	bool sendRequest(const ConnectionPtr &connection, StaticString argsSend[],
			unsigned int nrArgsSend)
	{
		ConnectionLock cl(connection);
		ConnectionGuard guard(connection.get());

		try {
			unsigned long long timeout = 15000000;

			writeArrayMessage(connection->fd, argsSend, nrArgsSend, &timeout);

			guard.clear();
			return true;
		} catch (const TimeoutException &) {
			handleTimeout();
			return false;
		} catch (const SystemException &e) {
			handleNetworkErrorOrThrow(connection, guard, e);
			return false;
		}
	}

	bool sendRequestGetResponse(const ConnectionPtr &connection,
		StaticString argsSend[], unsigned int nrArgsSend,
		vector<string> &argsReply, unsigned int expectedExtraReplyArgs = 0)
	{
		ConnectionLock cl(connection);
		ConnectionGuard guard(connection.get());

		try {
			unsigned long long timeout = 15000000;

			writeArrayMessage(connection->fd, argsSend, nrArgsSend, &timeout);

			if (!readArrayMessage(connection->fd, argsReply, &timeout)) {
				boost::lock_guard<boost::mutex> l(syncher);
				P_WARN("The UstRouter at " << serverAddress <<
					" closed the connection (no error message given);" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return false;
			}

			if (argsReply.size() < 2 || argsReply[0] != "status") {
				boost::lock_guard<boost::mutex> l(syncher);
				P_WARN("The UstRouter sent an invalid reply message;" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return false;
			}

			if (argsReply[1] == "error") {
				boost::lock_guard<boost::mutex> l(syncher);
				if (argsReply.size() >= 3) {
					P_WARN("The UstRouter closed the connection "
						"(error message: " << argsReply[2] <<
						"); will reconnect in " <<
						reconnectTimeout / 1000000 <<
						" second(s).");
				} else {
					P_WARN("The UstRouter closed the connection "
						"(no server message given); " <<
						"will reconnect in " <<
						reconnectTimeout / 1000000 <<
						" second(s).");
				}
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return false;
			}

			if (argsReply[1] != "ok") {
				boost::lock_guard<boost::mutex> l(syncher);
				P_WARN("The UstRouter sent an invalid reply message;" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return false;
			}

			if (argsReply.size() < 2 + expectedExtraReplyArgs) {
				boost::lock_guard<boost::mutex> l(syncher);
				P_WARN("The UstRouter sent an invalid reply message"
					" (\"ok\" status message has too few arguments);" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return false;
			}

			guard.clear();
			return true;

		} catch (const TimeoutException &) {
			handleTimeout();
			return false;
		} catch (const SystemException &e) {
			handleNetworkErrorOrThrow(connection, guard, e);
			return false;
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
		char timestampStr[2 * sizeof(unsigned long long) + 1];

		integerToHexatri<unsigned long long>(timestamp, timestampStr);
		StaticString params[] = {
			StaticString("openTransaction", sizeof("openTransaction") - 1),
			// empty txnId, implies that it should be autogenerated by
			// the UstRouter
			StaticString(),
			groupName,
			// empty nodeName, implies using the default
			// nodeName passed during initialization
			StaticString(),
			category,
			timestampStr,
			unionStationKey,
			P_STATIC_STRING("true"), // crashProtect
			P_STATIC_STRING("true"), // ack
			filters
		};
		unsigned int nparams = sizeof(params) / sizeof(StaticString);

		// Get a connection to the UstRouter.
		ConnectionPtr connection = checkoutConnection();
		if (connection == NULL) {
			P_TRACE(2, "Created NULL Union Station transaction: group=" << groupName <<
				", category=" << category);
			return createNullTransaction();
		}

		// The router will generate a txnId for us and pass it in the response.
		vector<string> argsReply;
		if (sendRequestGetResponse(connection, params, nparams, argsReply, 1)) {
			string txnId = argsReply[2];
			ConnectionGuard guard(connection.get());
			TransactionPtr transaction = boost::make_shared<Transaction>(
				shared_from_this(),
				connection,
				txnId,
				groupName,
				category,
				unionStationKey);
			guard.clear();
			P_TRACE(2, "Created new Union Station transaction: group=" << groupName <<
				", category=" << category << ", txnId=" << txnId);
			return transaction;
		} else {
			P_TRACE(2, "Created NULL Union Station transaction: group=" << groupName <<
				", category=" << category);
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
			P_STATIC_STRING("true"),  // crashProtect
			P_STATIC_STRING("false")  // ack
		};
		unsigned int nparams = sizeof(params) / sizeof(StaticString);

		// Get a connection to the UstRouter.
		ConnectionPtr connection = checkoutConnection();
		if (connection == NULL) {
			return createNullTransaction();
		}

		// We didn't ask for a response (ack), so just send here.
		if (sendRequest(connection, params, nparams)) {
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
_checkinConnection(const ContextPtr &ctx, const ConnectionPtr &connection) {
	ctx->checkinConnection(connection);
}


} // namespace UnionStation
} // namespace Passenger

#endif /* _PASSENGER_UNION_STATION_CONTEXT_H_ */
