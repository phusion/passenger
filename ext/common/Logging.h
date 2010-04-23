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
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <ostream>
#include <sstream>
#include <cstdio>
#include <ctime>

#include "RandomGenerator.h"
#include "FileDescriptor.h"
#include "MessageClient.h"
#include "StaticString.h"
#include "Exceptions.h"
#include "Utils/StrIntUtils.h"
#include "Utils/MD5.h"
#include "Utils/SystemTime.h"


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/********** Debug logging facilities **********/

extern unsigned int _logLevel;
extern ostream *_logStream;
extern ostream *_debugStream;

unsigned int getLogLevel();
void setLogLevel(unsigned int value);
void setDebugFile(const char *logFile = NULL);

/**
 * Write the given expression to the given stream.
 *
 * @param expr The expression to write.
 * @param stream A pointer to an object that accepts the '<<' operator.
 */
#define P_LOG_TO(expr, stream) \
	do { \
		if (stream != 0) { \
			time_t the_time;			\
			struct tm *the_tm;			\
			char datetime_buf[60];			\
			struct timeval tv;			\
			std::stringstream sstream;              \
								\
			the_time = time(NULL);			\
			the_tm = localtime(&the_time);		\
			strftime(datetime_buf, sizeof(datetime_buf), "%F %H:%M:%S", the_tm); \
			gettimeofday(&tv, NULL); \
			sstream << \
				"[ pid=" << ((unsigned long) getpid()) <<  \
				" file=" << __FILE__ << ":" << (unsigned long) __LINE__ << \
				" time=" << datetime_buf << "." << (unsigned long) (tv.tv_usec / 1000) << " ]:" << \
				"\n  " << expr << std::endl;	\
			*stream << sstream.str();		\
			stream->flush();			\
		} \
	} while (false)

/**
 * Write the given expression to the log stream.
 */
#define P_LOG(expr) P_LOG_TO(expr, Passenger::_logStream)

/**
 * Write the given expression, which represents a warning,
 * to the log stream.
 */
#define P_WARN(expr) P_LOG(expr)

/**
 * Write the given expression, which represents an error,
 * to the log stream.
 */
#define P_ERROR(expr) P_LOG(expr)

/**
 * Write the given expression, which represents a debugging message,
 * to the log stream.
 */
#define P_DEBUG(expr) P_TRACE(1, expr)

#ifdef PASSENGER_DEBUG
	#define P_TRACE(level, expr) \
		do { \
			if (Passenger::_logLevel >= level) { \
				P_LOG_TO(expr, Passenger::_debugStream); \
			} \
		} while (false)
	
	#define P_ASSERT(expr, result_if_failed, message) \
		do { \
			if (!(expr)) { \
				P_ERROR("Assertion failed: " << message); \
				return result_if_failed; \
			} \
		} while (false)
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
	
	#define P_ASSERT(expr, result_if_failed, message) do { /* nothing */ } while (false)
#endif


/********** Transaction logging facilities *********/

struct AnalyticsLoggerSharedData {
	boost::mutex lock;
	MessageClient client;
};
typedef shared_ptr<AnalyticsLoggerSharedData> AnalyticsLoggerSharedDataPtr;

class AnalyticsLog {
private:
	static const int INT64_STR_BUFSIZE = 22; // Long enough for a 64-bit number.
	
	AnalyticsLoggerSharedDataPtr sharedData;
	string txnId;
	string groupName;
	string category;
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
	
public:
	AnalyticsLog() { }
	
	AnalyticsLog(const AnalyticsLoggerSharedDataPtr &sharedData, const string &txnId,
		const string &groupName, const string &category)
	{
		this->sharedData = sharedData;
		this->txnId      = txnId;
		this->groupName  = groupName;
		this->category   = category;
		shouldFlushToDiskAfterClose = false;
	}
	
	~AnalyticsLog() {
		if (sharedData != NULL) {
			lock_guard<boost::mutex> l(sharedData->lock);
			if (sharedData->client.connected()) {
				try {
					char timestamp[2 * sizeof(unsigned long long) + 1];
					integerToHex<unsigned long long>(SystemTime::getUsec(),
						timestamp);
					sharedData->client.write("closeTransaction",
						txnId.c_str(), timestamp, NULL);
				} catch (const SystemException &e) {
					if (e.code() == EPIPE || e.code() == ECONNRESET) {
						TRACE_POINT();
						// Maybe the server sent us an error message and closed
						// the connection. Let's check.
						vector<string> args;
						if (sharedData->client.read(args)) {
							if (args[0] == "error") {
								throw IOException("The logging server responded with an error: " + args[1]);
							} else {
								throw IOException("The logging server sent an unexpected error reply.");
							}
						} else {
							throw IOException("The logging server unexpectedly closed the connection.");
						}
					} else {
						throw;
					}
				}
				
				if (shouldFlushToDiskAfterClose) {
					vector<string> args;
					sharedData->client.write("flush", NULL);
					sharedData->client.read(args);
				}
			}
		}
	}
	
	void message(const StaticString &text) {
		if (sharedData != NULL) {
			lock_guard<boost::mutex> l(sharedData->lock);
			if (sharedData->client.connected()) {
				char timestamp[2 * sizeof(unsigned long long) + 1];
				integerToHex<unsigned long long>(SystemTime::getUsec(), timestamp);
				sharedData->client.write("log", txnId.c_str(),
					timestamp, NULL);
				sharedData->client.writeScalar(text);
			}
		}
	}
	
	void abort(const StaticString &text) {
		if (sharedData != NULL) {
			lock_guard<boost::mutex> l(sharedData->lock);
			if (sharedData->client.connected()) {
				message("ABORT");
			}
		}
	}
	
	void flushToDiskAfterClose(bool value) {
		shouldFlushToDiskAfterClose = value;
	}
	
	bool isNull() const {
		return sharedData == NULL;
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
};

typedef shared_ptr<AnalyticsLog> AnalyticsLogPtr;

class AnalyticsScopeLog {
private:
	AnalyticsLog *log;
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
	
	static string timevalToMsecString(struct timeval &tv) {
		unsigned long long i = (unsigned long long) tv.tv_sec * 1000000 + tv.tv_usec;
		return toString<unsigned long long>(i);
	}
	
public:
	AnalyticsScopeLog(const AnalyticsLogPtr &log, const char *name) {
		this->log = log.get();
		type = NAME;
		data.name = name;
		ok = false;
		if (log != NULL && !log->isNull()) {
			string message;
			struct rusage usage;
			
			message.reserve(150);
			message.append("BEGIN: ");
			message.append(name);
			message.append(" (time = ");
			message.append(toString(SystemTime::getUsec()));
			message.append(", utime = ");
			if (getrusage(RUSAGE_SELF, &usage) == -1) {
				int e = errno;
				throw SystemException("getrusage() failed", e);
			}
			message.append(timevalToMsecString(usage.ru_utime));
			message.append(", stime = ");
			message.append(timevalToMsecString(usage.ru_stime));
			message.append(")");
			log->message(message);
		}
	}
	
	AnalyticsScopeLog(const AnalyticsLogPtr &log, const char *beginMessage,
	                  const char *endMessage, const char *abortMessage = NULL
	) {
		this->log = log.get();
		if (log != NULL) {
			type = GRANULAR;
			data.granular.endMessage = endMessage;
			data.granular.abortMessage = abortMessage;
			ok = abortMessage == NULL;
			log->message(beginMessage);
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
				message.append(" (time = ");
				message.append(toString(SystemTime::getUsec()));
				message.append(", utime = ");
				if (getrusage(RUSAGE_SELF, &usage) == -1) {
					int e = errno;
					throw SystemException("getrusage() failed", e);
				}
				message.append(timevalToMsecString(usage.ru_utime));
				message.append(", stime = ");
				message.append(timevalToMsecString(usage.ru_stime));
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
	string socketFilename;
	string username;
	string password;
	string nodeName;
	RandomGenerator randomGenerator;
	
	/** @invariant sharedData != NULL */
	AnalyticsLoggerSharedDataPtr sharedData;
	
	bool connected() const {
		return sharedData->client.connected();
	}
	
	void connect() {
		sharedData->client.connect(socketFilename, username, password);
		sharedData->client.write("init", nodeName.c_str(), NULL);
		sharedData->client.setAutoDisconnect(false);
	}
	
	void disconnect() {
		sharedData->client.disconnect();
		// We create a new SharedData here so that existing AnalyticsLog
		// objects still refer to the old client object and don't interfere
		// with any newly-established connections.
		sharedData.reset(new AnalyticsLoggerSharedData());
	}
	
public:
	AnalyticsLogger() { }
	
	AnalyticsLogger(const string &socketFilename, const string &username,
	                const string &password, const string &nodeName = "")
	{
		this->socketFilename = socketFilename;
		this->username       = username;
		this->password       = password;
		if (nodeName.empty()) {
			this->nodeName = getHostName();
		} else {
			this->nodeName = nodeName;
		}
		if (!socketFilename.empty()) {
			sharedData.reset(new AnalyticsLoggerSharedData());
		}
	}
	
	AnalyticsLogPtr newTransaction(const string &groupName, const string &category = "requests") {
		if (socketFilename.empty()) {
			return ptr(new AnalyticsLog());
		} else {
			unsigned long long timestamp = SystemTime::getUsec();
			char txnId[
				2 * sizeof(unsigned int) +    // max hex timestamp size
				11 +                          // space for a random identifier
				1                             // null terminator
			];
			char *end;
			unsigned int timestampSize;
			
			// "[timestamp]"
			// Our timestamp is like a Unix timestamp but with minutes
			// resolution instead of seconds. 32 bits will last us for
			// about 8000 years.
			timestampSize = integerToHex<unsigned int>(timestamp / 1000000 / 60,
				txnId);
			end = txnId + timestampSize;
			
			// "[timestamp]-"
			*end = '-';
			end++;
			
			// "[timestamp]-[random id]"
			randomGenerator.generateAsciiString(end, 11);
			end += 11;
			*end = '\0';
			
			lock_guard<boost::mutex> l(sharedData->lock);
			
			if (!connected()) {
				TRACE_POINT();
				connect();
			}
			try {
				char timestampStr[2 * sizeof(unsigned long long) + 1];
				integerToHex<unsigned long long>(timestamp, timestampStr);
				sharedData->client.write("openTransaction",
					txnId,
					groupName.c_str(),
					category.c_str(),
					timestampStr,
					NULL);
			} catch (const SystemException &e) {
				if (e.code() == EPIPE || e.code() == ECONNRESET) {
					TRACE_POINT();
					// Maybe the server sent us an error message and closed
					// the connection. Let's check.
					vector<string> args;
					if (sharedData->client.read(args)) {
						disconnect();
						if (args[0] == "error") {
							throw IOException("The logging server responded with an error: " + args[1]);
						} else {
							throw IOException("The logging server sent an unexpected reply.");
						}
					} else {
						disconnect();
						throw IOException("The logging server unexpectedly closed the connection.");
					}
				} else {
					disconnect();
					throw;
				}
			}
			return ptr(new AnalyticsLog(sharedData, string(txnId, end - txnId),
				groupName, category));
		}
	}
	
	AnalyticsLogPtr continueTransaction(const string &txnId, const string &groupName,
		const string &category = "requests")
	{
		if (socketFilename.empty() || txnId.empty()) {
			return ptr(new AnalyticsLog());
		} else {
			lock_guard<boost::mutex> l(sharedData->lock);
			
			if (!connected()) {
				TRACE_POINT();
				connect();
			}
			try {
				char timestampStr[2 * sizeof(unsigned long long) + 1];
				integerToHex<unsigned long long>(SystemTime::getUsec(), timestampStr);
				sharedData->client.write("openTransaction",
					txnId.c_str(),
					groupName.c_str(),
					category.c_str(),
					timestampStr,
					NULL);
			} catch (const SystemException &e) {
				if (e.code() == EPIPE || e.code() == ECONNRESET) {
					TRACE_POINT();
					// Maybe the server sent us an error message and closed
					// the connection. Let's check.
					vector<string> args;
					if (sharedData->client.read(args)) {
						disconnect();
						if (args[0] == "error") {
							throw IOException("The logging server responded with an error: " + args[1]);
						} else {
							throw IOException("The logging server sent an unexpected reply.");
						}
					} else {
						disconnect();
						throw IOException("The logging server unexpectedly closed the connection.");
					}
				} else {
					disconnect();
					throw;
				}
			}
			return ptr(new AnalyticsLog(sharedData, txnId, groupName, category));
		}
	}
	
	bool isNull() const {
		return socketFilename.empty();
	}
	
	string getAddress() const {
		return socketFilename;
	}
	
	string getUsername() const {
		return username;
	}
	
	string getPassword() const {
		return password;
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

