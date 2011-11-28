/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_LOGGING_H_
#define _PASSENGER_LOGGING_H_

#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <map>
#include <stdexcept>
#include <ostream>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <cerrno>

#include <RandomGenerator.h>
#include <FileDescriptor.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/ScopeGuard.h>
#include <Utils/MessageIO.h>
#include <Utils/StrIntUtils.h>
#include <Utils/MD5.h>
#include <Utils/SystemTime.h>


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/********** Debug logging facilities **********/

extern int _logLevel;
extern ostream *_logStream;

int getLogLevel();
void setLogLevel(int value);
void setDebugFile(const char *logFile = NULL);

/**
 * Write the given expression to the given stream.
 *
 * @param expr The expression to write.
 * @param stream A pointer to an object that accepts the '<<' operator.
 */
#define P_LOG_TO(level, expr, stream) \
	do { \
		if (stream != 0 && Passenger::_logLevel >= level) { \
			time_t the_time;			\
			struct tm the_tm;			\
			char datetime_buf[60];			\
			struct timeval tv;			\
			std::stringstream sstream;              \
								\
			the_time = time(NULL);			\
			localtime_r(&the_time, &the_tm);	\
			strftime(datetime_buf, sizeof(datetime_buf), "%F %H:%M:%S", &the_tm); \
			gettimeofday(&tv, NULL); \
			sstream << \
				"[ pid=" << ((unsigned long) getpid()) <<  \
				" thr=" << pthread_self() << \
				" file=" << __FILE__ << ":" << (unsigned long) __LINE__ << \
				" time=" << datetime_buf << "." << (unsigned long) (tv.tv_usec / 1000) << \
				" ]: " << \
				expr << std::endl;	\
			*stream << sstream.str();		\
			stream->flush();			\
		} \
	} while (false)

/**
 * Write the given expression to the log stream.
 */
#define P_LOG(level, expr) P_LOG_TO(level, expr, Passenger::_logStream)

/**
 * Write the given expression, which represents a warning,
 * to the log stream.
 */
#define P_WARN(expr) P_LOG(0, expr)

/**
 * Write the given expression, which represents an error,
 * to the log stream.
 */
#define P_ERROR(expr) P_LOG(-1, expr)

/**
 * Write the given expression, which represents a debugging message,
 * to the log stream.
 */
#define P_DEBUG(expr) P_TRACE(1, expr)

#ifdef PASSENGER_DEBUG
	#define P_TRACE(level, expr) P_LOG_TO(level, expr, Passenger::_logStream)
	
	#define P_ASSERT(expr, result_if_failed, message) \
		do { \
			if (!(expr)) { \
				P_ERROR("Assertion failed: " << message); \
				return result_if_failed; \
			} \
		} while (false)
	#define P_ASSERT_WITH_VOID_RETURN(expr, message) \
		do { \
			if (!(expr)) { \
				P_ERROR("Assertion failed: " << message); \
				return; \
			} \
		} while (false)
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
	
	#define P_ASSERT(expr, result_if_failed, message) do { /* nothing */ } while (false)
	#define P_ASSERT_WITH_VOID_RETURN(expr, message) do { /* nothing */ } while (false)
#endif


/********** Analytics logging facilities *********/

// All access to the file descriptor must be synchronized through the lock.
struct AnalyticsLoggerConnection {
	mutable boost::mutex lock;
	FileDescriptor fd;
	
	AnalyticsLoggerConnection(FileDescriptor _fd)
		: fd(_fd)
		{ }
	
	bool connected() const {
		return fd != -1;
	}
	
	bool disconnect(string &errorResponse) {
		if (!connected()) {
			return false;
		}
		
		// The server might send an "error" array message
		// just before disconnecting. Try to read it.
		TRACE_POINT();
		vector<string> response;
		try {
			while (true) {
				unsigned long long timeout = 10000;
				response = readArrayMessage(fd, &timeout);
			}
		} catch (const TimeoutException &) {
			// This means that the last message isn't an array
			// message or that the server didn't send it quickly
			// enough. In any case, discard whatever previous
			// array messages we were able to read because they're
			// guaranteed not to be the error message we're expecting.
			response.clear();
		} catch (const SystemException &e) {
			// We treat ECONNRESET the same as EOFException.
			// Other errors are treated as TimeoutException.
			if (e.code() != ECONNRESET) {
				response.clear();
			}
		} catch (const EOFException &) {
			// Do nothing. We've successfully read the last array message.
		}
		
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		UPDATE_TRACE_POINT();
		fd.close();
		
		if (response.size() == 2 && response[0] == "error") {
			errorResponse = response[1];
			return true;
		} else {
			return false;
		}
	}
	
	void disconnect() {
		fd.close();
	}
};

typedef shared_ptr<AnalyticsLoggerConnection> AnalyticsLoggerConnectionPtr;


enum ExceptionHandlingMode {
	PRINT,
	THROW,
	IGNORE
};


class AnalyticsLog {
private:
	static const int INT64_STR_BUFSIZE = 22; // Long enough for a 64-bit number.
	static const unsigned long long IO_TIMEOUT = 5000000; // In microseconds.
	
	const AnalyticsLoggerConnectionPtr connection;
	const string txnId;
	const string groupName;
	const string category;
	const string unionStationKey;
	const ExceptionHandlingMode exceptionHandlingMode;
	bool shouldFlushToDiskAfterClose;
	
	/**
	 * Buffer must be at least txnId.size() + 1 + INT64_STR_BUFSIZE + 1 bytes.
	 */
	char *insertTxnIdAndTimestamp(char *buffer) {
		int size;
		
		// "txn-id-here"
		memcpy(buffer, txnId.c_str(), txnId.size());
		buffer += txnId.size();
		
		// "txn-id-here "
		*buffer = ' ';
		buffer++;
		
		// "txn-id-here 123456"
		size = snprintf(buffer, INT64_STR_BUFSIZE, "%llu", SystemTime::getUsec());
		if (size >= INT64_STR_BUFSIZE) {
			// The buffer is too small.
			throw IOException("Cannot format a new transaction log message timestamp.");
		}
		buffer += size;
		
		// "txn-id-here 123456 "
		*buffer = ' ';
		
		return buffer + 1;
	}
	
	template<typename ExceptionType>
	void handleException(const ExceptionType &e) {
		switch (exceptionHandlingMode) {
		case THROW:
			throw e;
		case PRINT:
			try {
				const tracable_exception &te =
					dynamic_cast<const tracable_exception &>(e);
				P_WARN(te.what() << "\n" << te.backtrace());
			} catch (const bad_cast &) {
				P_WARN(e.what());
			}
			break;
		default:
			break;
		}
	}
	
public:
	AnalyticsLog()
		: exceptionHandlingMode(PRINT)
		{ }
	
	AnalyticsLog(const AnalyticsLoggerConnectionPtr &_connection,
		const string &_txnId,
		const string &_groupName,
		const string &_category,
		const string &_unionStationKey,
		ExceptionHandlingMode _exceptionHandlingMode = PRINT)
		: connection(_connection),
		  txnId(_txnId),
		  groupName(_groupName),
		  category(_category),
		  unionStationKey(_unionStationKey),
		  exceptionHandlingMode(_exceptionHandlingMode),
		  shouldFlushToDiskAfterClose(false)
		{ }
	
	~AnalyticsLog() {
		TRACE_POINT();
		if (connection == NULL) {
			return;
		}
		lock_guard<boost::mutex> l(connection->lock);
		if (!connection->connected()) {
			return;
		}
		
		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(),
			timestamp);
		
		UPDATE_TRACE_POINT();
		ScopeGuard guard(boost::bind(&AnalyticsLoggerConnection::disconnect,
			connection.get()));
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
			guard.clear();
		} catch (const SystemException &e) {
			string errorResponse;
			
			UPDATE_TRACE_POINT();
			guard.clear();
			if (connection->disconnect(errorResponse)) {
				handleException(IOException(
					string("Logging agent disconnected with error: ") +
					e.what()));
			} else {
				handleException(e);
			}
		}
	}
	
	void message(const StaticString &text) {
		TRACE_POINT();
		if (connection == NULL) {
			return;
		}
		lock_guard<boost::mutex> l(connection->lock);
		if (!connection->connected()) {
			return;
		}
		
		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(), timestamp);
		
		UPDATE_TRACE_POINT();
		ScopeGuard guard(boost::bind(&AnalyticsLoggerConnection::disconnect,
			connection.get()));
		try {
			unsigned long long timeout = IO_TIMEOUT;
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
					string("Logging agent disconnected with error: ") +
					e.what()));
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
	
	string getTxnId() const {
		return txnId;
	}
	
	string getGroupName() const {
		return groupName;
	}
	
	string getCategory() const {
		return category;
	}
	
	string getUnionStationKey() const {
		return unionStationKey;
	}
};

typedef shared_ptr<AnalyticsLog> AnalyticsLogPtr;


class AnalyticsScopeLog: public boost::noncopyable {
private:
	AnalyticsLog * const log;
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
	AnalyticsScopeLog(const AnalyticsLogPtr &_log, const char *name)
		: log(_log.get())
	{
		type = NAME;
		data.name = name;
		ok = false;
		if (log != NULL && !log->isNull()) {
			string message;
			struct rusage usage;
			
			message.reserve(150);
			message.append("BEGIN: ");
			message.append(name);
			message.append(" (");
			message.append(usecToString(SystemTime::getUsec()));
			message.append(",");
			if (getrusage(RUSAGE_SELF, &usage) == -1) {
				int e = errno;
				throw SystemException("getrusage() failed", e);
			}
			message.append(timevalToString(usage.ru_utime));
			message.append(",");
			message.append(timevalToString(usage.ru_stime));
			message.append(") ");
			log->message(message);
		}
	}
	
	AnalyticsScopeLog(const AnalyticsLogPtr &_log,
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
	
	~AnalyticsScopeLog() {
		if (log == NULL) {
			return;
		}
		if (type == NAME) {
			if (!log->isNull()) {
				string message;
				struct rusage usage;
				
				message.reserve(150);
				if (ok) {
					message.append("END: ");
				} else {
					message.append("FAIL: ");
				}
				message.append(data.name);
				message.append(" (");
				message.append(usecToString(SystemTime::getUsec()));
				message.append(",");
				if (getrusage(RUSAGE_SELF, &usage) == -1) {
					int e = errno;
					throw SystemException("getrusage() failed", e);
				}
				message.append(timevalToString(usage.ru_utime));
				message.append(",");
				message.append(timevalToString(usage.ru_stime));
				message.append(")");
				log->message(message);
			}
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


class AnalyticsLogger {
private:
	/** A special lock type for AnalyticsLoggerConnection that also
	 * keeps a smart pointer to the data structure so that the mutex
	 * is not destroyed prematurely.
	 */
	struct ConnectionLock {
		AnalyticsLoggerConnectionPtr connection;
		bool locked;
		
		ConnectionLock(const AnalyticsLoggerConnectionPtr &c)
			: connection(c)
		{
			c->lock.lock();
			locked = true;
		}
		
		~ConnectionLock() {
			if (locked) {
				connection->lock.unlock();
			}
		}
		
		void reset(const AnalyticsLoggerConnectionPtr &c, bool lockNow = true) {
			if (locked) {
				connection->lock.unlock();
			}
			connection = c;
			if (lockNow) {
				connection->lock.lock();
				locked = true;
			} else {
				locked = false;
			}
		}
		
		void lock() {
			assert(!locked);
			connection->lock.lock();
			locked = true;
		}
	};
	
	const string serverAddress;
	const string username;
	const string password;
	const string nodeName;
	RandomGenerator randomGenerator;
	
	/** Lock protecting the fields that follow, but not the
	 * contents of the connection object.
	 */
	mutable boost::mutex lock;
	
	unsigned int maxConnectTries;
	unsigned long long reconnectTimeout;
	unsigned long long nextReconnectTime;
	/** Normally never NULL, except when constructed with the default constructor
	 * or if serverName is empty. In those cases the AnalyticsLogger object is
	 * considered unusable.
	 */
	AnalyticsLoggerConnectionPtr connection;
	
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
	
	bool connected() const {
		return connection->connected();
	}
	
	void connect() {
		TRACE_POINT();
		FileDescriptor fd;
		vector<string> args;
		unsigned long long timeout = 15000000;
		
		fd = connectToServer(serverAddress);
		if (!readArrayMessage(fd, args, &timeout)) {
			throw IOException("The logging agent closed the connection before sending a version identifier.");
		}
		if (args.size() != 2 || args[0] != "version") {
			throw IOException("The logging agent server didn't sent a valid version identifier.");
		}
		if (args[1] != "1") {
			string message = string("Unsupported logging agent protocol version ") +
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
		
		connection = make_shared<AnalyticsLoggerConnection>(fd);
	}
	
public:
	AnalyticsLogger() { }
	
	AnalyticsLogger(const string &_serverAddress, const string &_username,
	                const string &_password, const string &_nodeName = "")
		: serverAddress(_serverAddress),
		  username(_username),
		  password(_password),
		  nodeName(determineNodeName(_nodeName))
	{
		if (!serverAddress.empty()) {
			connection = make_shared<AnalyticsLoggerConnection>(FileDescriptor());
		}
		if (isLocalSocketAddress(serverAddress)) {
			maxConnectTries = 10;
		} else {
			maxConnectTries = 1;
		}
		reconnectTimeout  = 1000000;
		nextReconnectTime = 0;
	}
	
	template<typename T>
	static bool instanceof(const std::exception &e) {
		try {
			(void) dynamic_cast<const T &>(e);
			return true;
		} catch (const bad_cast &) {
			return false;
		}
	}
	
	AnalyticsLogPtr newTransaction(const string &groupName,
		const string &category = "requests",
		const string &unionStationKey = string(),
		const string &filters = string())
	{
		if (serverAddress.empty()) {
			return make_shared<AnalyticsLog>();
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
		
		unique_lock<boost::mutex> l(lock);
		if (SystemTime::getUsec() < nextReconnectTime) {
			return make_shared<AnalyticsLog>();
		}
		ConnectionLock cl(connection);
		
		if (!connected()) {
			TRACE_POINT();
			try {
				connect();
				cl.reset(connection);
			} catch (const TimeoutException &) {
				P_WARN("Timeout trying to connect to the logging agent at " << serverAddress << "; " <<
					"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return make_shared<AnalyticsLog>();
			} catch (const tracable_exception &e) {
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				if (instanceof<IOException>(e) || instanceof<SystemException>(e)) {
					P_WARN("Cannot connect to the logging agent at " << serverAddress <<
						" (" << e.what() << "); will reconnect in " <<
						reconnectTimeout / 1000000 << " second(s).");
					return make_shared<AnalyticsLog>();
				} else {
					throw;
				}
			}
		}
		
		ScopeGuard guard(boost::bind(
			&AnalyticsLoggerConnection::disconnect,
			connection.get()));
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
				P_WARN("The logging agent at " << serverAddress <<
					" closed the connection (no error message given);" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return make_shared<AnalyticsLog>();
			} else if (args.size() == 2 && args[0] == "error") {
				P_WARN("The logging agent at " << serverAddress <<
					" closed the connection (error message: " << args[1] <<
					"); will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return make_shared<AnalyticsLog>();
			} else if (args.empty() || args[0] != "ok") {
				P_WARN("The logging agent at " << serverAddress <<
					" sent an unexpected reply;" <<
					" will reconnect in " << reconnectTimeout / 1000000 <<
					" second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return make_shared<AnalyticsLog>();
			}
			
			guard.clear();
			return make_shared<AnalyticsLog>(connection,
				string(txnId, end - txnId),
				groupName, category,
				unionStationKey);
			
		} catch (const TimeoutException &) {
			P_WARN("Timeout trying to communicate with the logging agent at " << serverAddress << "; " <<
				"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
			nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
			return make_shared<AnalyticsLog>();
			
		} catch (const SystemException &e) {
			if (e.code() == ENOENT || isNetworkError(e.code())) {
				string errorResponse;
				
				guard.clear();
				if (connection->disconnect(errorResponse)) {
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
				return make_shared<AnalyticsLog>();
			} else {
				throw;
			}
		}
	}
	
	AnalyticsLogPtr continueTransaction(const string &txnId, const string &groupName,
		const string &category = "requests", const string &unionStationKey = string())
	{
		if (serverAddress.empty() || txnId.empty()) {
			return make_shared<AnalyticsLog>();
		}
		
		char timestampStr[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(), timestampStr);
		
		unique_lock<boost::mutex> l(lock);
		if (SystemTime::getUsec() < nextReconnectTime) {
			return make_shared<AnalyticsLog>();
		}
		ConnectionLock cl(connection);
		
		if (!connected()) {
			TRACE_POINT();
			try {
				connect();
				cl.reset(connection);
			} catch (const TimeoutException &) {
				P_WARN("Timeout trying to connect to the logging agent at " << serverAddress << "; " <<
					"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				return make_shared<AnalyticsLog>();
			} catch (const tracable_exception &e) {
				nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
				if (instanceof<IOException>(e) || instanceof<SystemException>(e)) {
					P_WARN("Cannot connect to the logging agent at " << serverAddress <<
						" (" << e.what() << "); will reconnect in " <<
						reconnectTimeout / 1000000 << " second(s).");
					return make_shared<AnalyticsLog>();
				} else {
					throw;
				}
			}
		}
		
		ScopeGuard guard(boost::bind(
			&AnalyticsLoggerConnection::disconnect,
			connection.get()));
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
			return make_shared<AnalyticsLog>(connection,
				txnId, groupName, category,
				unionStationKey);
			
		} catch (const TimeoutException &) {
			P_WARN("Timeout trying to communicate with the logging agent at " << serverAddress << "; " <<
				"will reconnect in " << reconnectTimeout / 1000000 << " second(s).");
			nextReconnectTime = SystemTime::getUsec() + reconnectTimeout;
			return make_shared<AnalyticsLog>();
			
		} catch (const SystemException &e) {
			if (e.code() == ENOENT || isNetworkError(e.code())) {
				string errorResponse;
				
				guard.clear();
				if (connection->disconnect(errorResponse)) {
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
				return make_shared<AnalyticsLog>();
			} else {
				throw;
			}
		}
	}
	
	void setMaxConnectTries(unsigned int value) {
		lock_guard<boost::mutex> l(lock);
		maxConnectTries = value;
	}
	
	void setReconnectTimeout(unsigned long long usec) {
		lock_guard<boost::mutex> l(lock);
		reconnectTimeout = usec;
	}
	
	bool isNull() const {
		return serverAddress.empty();
	}
	
	string getAddress() const {
		return serverAddress;
	}
	
	string getUsername() const {
		return username;
	}
	
	string getPassword() const {
		return password;
	}
	
	FileDescriptor getConnection() const {
		lock_guard<boost::mutex> l(lock);
		lock_guard<boost::mutex> l2(connection->lock);
		return connection->fd;
	}
	
	/**
	 * @post !result.empty()
	 */
	string getNodeName() const {
		return nodeName;
	}
};

typedef shared_ptr<AnalyticsLogger> AnalyticsLoggerPtr;

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_H_ */

