/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
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
#ifndef _PASSENGER_UNION_STATION_H_
#define _PASSENGER_UNION_STATION_H_

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <map>
#include <stdexcept>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <cassert>

#include <RandomGenerator.h>
#include <FileDescriptor.h>
#include <StaticString.h>
#include <Logging.h>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/MessageIO.h>
#include <Utils/StrIntUtils.h>
#include <Utils/MD5.h>
#include <Utils/SystemTime.h>


namespace Passenger {
namespace UnionStation {

using namespace std;
using namespace boost;
using namespace oxt;


// All access to the file descriptor must be synchronized through the lock.
struct Connection {
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
	
	bool disconnect(string &errorResponse) {
		if (!connected()) {
			return false;
		}
		
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
		disconnect();
		
		if (response.size() == 2 && response[0] == "error") {
			errorResponse = response[1];
			return true;
		} else {
			return false;
		}
	}
	
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


/** A special lock type for Connection that also keeps a smart
 * pointer to the data structure so that the mutex is not destroyed
 * prematurely.
 */
struct ConnectionLock {
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


/**
 * A scope guard which closes the given Connection on destruction unless cleared.
 */
class ConnectionGuard {
private:
	ConnectionPtr connection;
	bool cleared;

public:
	ConnectionGuard(const ConnectionPtr &_connection)
		: connection(_connection),
		  cleared(false)
		{ }
	
	~ConnectionGuard() {
		if (!cleared) {
			connection->disconnect();
		}
	}

	void clear() {
		cleared = true;
	}
};


enum ExceptionHandlingMode {
	PRINT,
	THROW,
	IGNORE
};


class LoggerFactory;
typedef boost::shared_ptr<LoggerFactory> LoggerFactoryPtr;

inline void _checkinConnection(const LoggerFactoryPtr &loggerFactory, const ConnectionPtr &connection);


class Logger: public boost::noncopyable {
private:
	static const int INT64_STR_BUFSIZE = 22; // Long enough for a 64-bit number.
	static const unsigned long long IO_TIMEOUT = 5000000; // In microseconds.
	
	const LoggerFactoryPtr loggerFactory;
	const ConnectionPtr connection;
	const string txnId;
	const string groupName;
	const string category;
	const string unionStationKey;
	const ExceptionHandlingMode exceptionHandlingMode;
	bool shouldFlushToDiskAfterClose;
	
	/**
	 * Buffer must be at least txnId.size() + 1 + INT64_STR_BUFSIZE + 1 bytes.
	 */
	char *insertTxnIdAndTimestamp(char *buffer, const char *end) {
		assert(end - buffer >= int(txnId.size() + 1 + INT64_STR_BUFSIZE + 1));
		int size;
		
		// "txn-id-here"
		buffer = appendData(buffer, end, txnId);
		
		// "txn-id-here "
		buffer = appendData(buffer, end, " ", 1);
		
		// "txn-id-here 123456"
		assert(end - buffer >= INT64_STR_BUFSIZE);
		size = snprintf(buffer, INT64_STR_BUFSIZE, "%llu", SystemTime::getUsec());
		if (size >= INT64_STR_BUFSIZE) {
			// The buffer is too small.
			throw IOException("Cannot format a new transaction log message timestamp.");
		}
		buffer += size;
		
		// "txn-id-here 123456 "
		buffer = appendData(buffer, end, " ", 1);
		
		return buffer;
	}
	
	template<typename ExceptionType>
	void handleException(const ExceptionType &e) {
		switch (exceptionHandlingMode) {
		case THROW:
			throw e;
		case PRINT: {
				const tracable_exception *te =
					dynamic_cast<const tracable_exception *>(&e);
				if (te != NULL) {
					P_WARN(te->what() << "\n" << te->backtrace());
				} else {
					P_WARN(e.what());
				}
				break;
			}
		default:
			break;
		}
	}
	
public:
	Logger()
		: exceptionHandlingMode(PRINT)
		{ }
	
	Logger(const LoggerFactoryPtr &_loggerFactory,
		const ConnectionPtr &_connection,
		const string &_txnId,
		const string &_groupName,
		const string &_category,
		const string &_unionStationKey,
		ExceptionHandlingMode _exceptionHandlingMode = PRINT)
		: loggerFactory(_loggerFactory),
		  connection(_connection),
		  txnId(_txnId),
		  groupName(_groupName),
		  category(_category),
		  unionStationKey(_unionStationKey),
		  exceptionHandlingMode(_exceptionHandlingMode),
		  shouldFlushToDiskAfterClose(false)
		{ }
	
	~Logger() {
		TRACE_POINT();
		if (connection == NULL) {
			return;
		}
		ConnectionLock l(connection);
		if (!connection->connected()) {
			return;
		}
		
		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(),
			timestamp);
		
		UPDATE_TRACE_POINT();
		ConnectionGuard guard(connection);
		try {
			unsigned long long timeout = IO_TIMEOUT;
			writeArrayMessage(connection->fd, &timeout,
				"closeTransaction",
				txnId.c_str(),
				timestamp,
				NULL);
			
			if (shouldFlushToDiskAfterClose) {
				UPDATE_TRACE_POINT();
				timeout = IO_TIMEOUT;
				writeArrayMessage(connection->fd, &timeout,
					"flush", NULL);
				readArrayMessage(connection->fd, &timeout);
			}

			_checkinConnection(loggerFactory, connection);
			guard.clear();
		} catch (const SystemException &e) {
			string errorResponse;
			
			UPDATE_TRACE_POINT();
			guard.clear();
			if (connection->disconnect(errorResponse)) {
				handleException(IOException(
					"Logging agent disconnected with error: " +
					errorResponse));
			} else {
				handleException(e);
			}
		}
	}
	
	void message(const StaticString &text) {
		TRACE_POINT();
		if (connection == NULL) {
			P_TRACE(3, "[Union Station log to null] " << text);
			return;
		}
		ConnectionLock l(connection);
		if (!connection->connected()) {
			P_TRACE(3, "[Union Station log to null] " << text);
			return;
		}
		
		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(), timestamp);
		
		UPDATE_TRACE_POINT();
		ConnectionGuard guard(connection);
		try {
			unsigned long long timeout = IO_TIMEOUT;
			P_TRACE(3, "[Union Station log] " << txnId << " " << timestamp << " " << text);
			writeArrayMessage(connection->fd, &timeout,
				"log",
				txnId.c_str(),
				timestamp,
				NULL);
			writeScalarMessage(connection->fd, text, &timeout);
			guard.clear();
		} catch (const std::exception &e) {
			string errorResponse;
			
			UPDATE_TRACE_POINT();
			guard.clear();
			if (connection->disconnect(errorResponse)) {
				handleException(IOException(
					"Logging agent disconnected with error: " +
					errorResponse));
			} else {
				handleException(e);
			}
		}
	}
	
	void abort(const StaticString &text) {
		message("ABORT");
	}
	
	void flushToDiskAfterClose(bool value) {
		shouldFlushToDiskAfterClose = value;
	}
	
	bool isNull() const {
		return connection == NULL;
	}
	
	const string &getTxnId() const {
		return txnId;
	}
	
	const string &getGroupName() const {
		return groupName;
	}
	
	const string &getCategory() const {
		return category;
	}
	
	const string &getUnionStationKey() const {
		return unionStationKey;
	}
};

typedef boost::shared_ptr<Logger> LoggerPtr;


class ScopeLog: public noncopyable {
private:
	Logger * const log;
	enum {
		NAME,
		GRANULAR
	} type;
	union {
		const char *name;
		struct {
			const char *endMessage;
			const char *abortMessage;	
		} granular;
	} data;
	bool ok;
	
	static string timevalToString(struct timeval &tv) {
		unsigned long long i = (unsigned long long) tv.tv_sec * 1000000 + tv.tv_usec;
		return usecToString(i);
	}
	
	static string usecToString(unsigned long long usec) {
		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(usec, timestamp);
		return timestamp;
	}
	
public:
	ScopeLog()
		: log(NULL)
		{ }

	ScopeLog(const LoggerPtr &_log, const char *name)
		: log(_log.get())
	{
		type = NAME;
		data.name = name;
		ok = false;

		char message[150];
		char *pos = message;
		const char *end = message + sizeof(message);
		struct rusage usage;

		pos = appendData(pos, end, "BEGIN: ");
		pos = appendData(pos, end, name);
		pos = appendData(pos, end, " (");
		pos = appendData(pos, end, usecToString(SystemTime::getUsec()));
		pos = appendData(pos, end, ",");
		if (getrusage(RUSAGE_SELF, &usage) == -1) {
			int e = errno;
			throw SystemException("getrusage() failed", e);
		}
		pos = appendData(pos, end, timevalToString(usage.ru_utime));
		pos = appendData(pos, end, ",");
		pos = appendData(pos, end, timevalToString(usage.ru_stime));
		pos = appendData(pos, end, ") ");

		if (log != NULL) {
			log->message(StaticString(message, pos - message));
		}
	}
	
	ScopeLog(const LoggerPtr &_log,
		const char *beginMessage,
		const char *endMessage,
		const char *abortMessage = NULL)
		: log(_log.get())
	{
		if (_log != NULL) {
			type = GRANULAR;
			data.granular.endMessage = endMessage;
			data.granular.abortMessage = abortMessage;
			ok = abortMessage == NULL;
			_log->message(beginMessage);
		}
	}
	
	~ScopeLog() {
		if (log == NULL) {
			return;
		}
		if (type == NAME) {
			char message[150];
			char *pos = message;
			const char *end = message + sizeof(message);
			struct rusage usage;
			
			if (ok) {
				pos = appendData(pos, end, "END: ");
			} else {
				pos = appendData(pos, end, "FAIL: ");
			}
			pos = appendData(pos, end, data.name);
			pos = appendData(pos, end, " (");
			pos = appendData(pos, end, usecToString(SystemTime::getUsec()));
			pos = appendData(pos, end, ",");
			if (getrusage(RUSAGE_SELF, &usage) == -1) {
				int e = errno;
				throw SystemException("getrusage() failed", e);
			}
			pos = appendData(pos, end, timevalToString(usage.ru_utime));
			pos = appendData(pos, end, ",");
			pos = appendData(pos, end, timevalToString(usage.ru_stime));
			pos = appendData(pos, end, ")");

			log->message(StaticString(message, pos - message));
		} else {
			if (ok) {
				log->message(data.granular.endMessage);
			} else {
				log->message(data.granular.abortMessage);
			}
		}
	}
	
	void success() {
		ok = true;
	}
};


class LoggerFactory: public boost::enable_shared_from_this<LoggerFactory> {
private:
	static const unsigned int CONNECTION_POOL_MAX_SIZE = 10;

	const string serverAddress;
	const string username;
	const string password;
	const string nodeName;
	RandomGenerator randomGenerator;

	LoggerPtr nullLogger;
	
	/** Lock protecting the fields that follow, but not the
	 * contents of the connection object.
	 */
	mutable boost::mutex syncher;
	vector<ConnectionPtr> connectionPool;
	unsigned int maxConnectTries;
	unsigned long long reconnectTimeout;
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
	
	ConnectionPtr createNewConnection() {
		TRACE_POINT();
		int fd;
		vector<string> args;
		unsigned long long timeout = 15000000;
		
		fd = connectToServer(serverAddress);
		FdGuard guard(fd, true);
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
		
		guard.clear();
		return boost::make_shared<Connection>(fd);
	}
	
public:
	LoggerFactory() {
		nullLogger = boost::make_shared<Logger>();
	}
	
	LoggerFactory(const string &_serverAddress, const string &_username,
	              const string &_password, const string &_nodeName = string())
		: serverAddress(_serverAddress),
		  username(_username),
		  password(_password),
		  nodeName(determineNodeName(_nodeName))
	{
		nullLogger = boost::make_shared<Logger>();
		if (!_serverAddress.empty() && isLocalSocketAddress(_serverAddress)) {
			maxConnectTries = 10;
		} else {
			maxConnectTries = 1;
		}
		reconnectTimeout  = 1000000;
		nextReconnectTime = 0;
	}

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
		boost::lock_guard<boost::mutex> l(syncher);
		if (connectionPool.size() < CONNECTION_POOL_MAX_SIZE) {
			connectionPool.push_back(connection);
		} else {
			connection->disconnect();
		}
	}

	LoggerPtr createNullLogger() const {
		return nullLogger;
	}
	
	LoggerPtr newTransaction(const string &groupName,
		const string &category = "requests",
		const string &unionStationKey = "-",
		const string &filters = string())
	{
		if (serverAddress.empty()) {
			return createNullLogger();
		}
		
		unsigned long long timestamp = SystemTime::getUsec();
		char txnId[
			2 * sizeof(unsigned int) +    // max hex timestamp size
			11 +                          // space for a random identifier
			1                             // null terminator
		];
		char *end;
		unsigned int timestampSize;
		char timestampStr[2 * sizeof(unsigned long long) + 1];
		
		// "[timestamp]"
		// Our timestamp is like a Unix timestamp but with minutes
		// resolution instead of seconds. 32 bits will last us for
		// about 8000 years.
		timestampSize = integerToHexatri<unsigned int>(timestamp / 1000000 / 60,
			txnId);
		end = txnId + timestampSize;
		
		// "[timestamp]-"
		*end = '-';
		end++;
		
		// "[timestamp]-[random id]"
		randomGenerator.generateAsciiString(end, 11);
		end += 11;
		*end = '\0';
		
		integerToHexatri<unsigned long long>(timestamp, timestampStr);
		
		ConnectionPtr connection = checkoutConnection();
		if (connection == NULL) {
			return createNullLogger();
		}

		ConnectionLock cl(connection);
		ConnectionGuard guard(connection);

		try {
			unsigned long long timeout = 15000000;
			
			writeArrayMessage(connection->fd, &timeout,
				"openTransaction",
				txnId,
				groupName.c_str(),
				"",
				category.c_str(),
				timestampStr,
				unionStationKey.c_str(),
				"true",
				"true",
				filters.c_str(),
				NULL);
			
			vector<string> args;
			if (!readArrayMessage(connection->fd, args, &timeout)) {
				boost::lock_guard<boost::mutex> l(syncher);
				P_WARN("The logging agent at " << serverAddress <<
					" closed the connection (no error message given);" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return createNullLogger();
			} else if (args.size() == 2 && args[0] == "error") {
				boost::lock_guard<boost::mutex> l(syncher);
				P_WARN("The logging agent at " << serverAddress <<
					" closed the connection (error message: " << args[1] <<
					"); will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return createNullLogger();
			} else if (args.empty() || args[0] != "ok") {
				boost::lock_guard<boost::mutex> l(syncher);
				P_WARN("The logging agent at " << serverAddress <<
					" sent an unexpected reply;" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return createNullLogger();
			}
			
			guard.clear();
			return boost::make_shared<Logger>(shared_from_this(),
				connection,
				string(txnId, end - txnId),
				groupName, category,
				unionStationKey);
			
		} catch (const TimeoutException &) {
			boost::lock_guard<boost::mutex> l(syncher);
			P_WARN("Timeout trying to communicate with the logging agent at " << serverAddress << "; " <<
				"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
			nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
			return createNullLogger();
			
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
				return createNullLogger();
			} else {
				throw;
			}
		}
	}
	
	LoggerPtr continueTransaction(const string &txnId,
		const string &groupName,
		const string &category = "requests",
		const string &unionStationKey = "-")
	{
		if (serverAddress.empty() || txnId.empty()) {
			return createNullLogger();
		}
		
		char timestampStr[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(), timestampStr);
		
		ConnectionPtr connection = checkoutConnection();
		if (connection == NULL) {
			return createNullLogger();
		}

		ConnectionLock cl(connection);
		ConnectionGuard guard(connection);
		
		try {
			unsigned long long timeout = 15000000;
			writeArrayMessage(connection->fd, &timeout,
				"openTransaction",
				txnId.c_str(),
				groupName.c_str(),
				"",
				category.c_str(),
				timestampStr,
				unionStationKey.c_str(),
				"true",
				NULL);
			guard.clear();
			return boost::make_shared<Logger>(shared_from_this(),
				connection,
				txnId, groupName, category,
				unionStationKey);
			
		} catch (const TimeoutException &) {
			boost::lock_guard<boost::mutex> l(syncher);
			P_WARN("Timeout trying to communicate with the logging agent at " << serverAddress << "; " <<
				"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
			nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
			return createNullLogger();
			
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
				return createNullLogger();
			} else {
				throw;
			}
		}
	}
	
	void setMaxConnectTries(unsigned int value) {
		boost::lock_guard<boost::mutex> l(syncher);
		maxConnectTries = value;
	}
	
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
_checkinConnection(const LoggerFactoryPtr &loggerFactory, const ConnectionPtr &connection) {
	loggerFactory->checkinConnection(connection);
}


} // namespace UnionStation
} // namespace Passenger

#endif /* _PASSENGER_UNION_STATION_H_ */

