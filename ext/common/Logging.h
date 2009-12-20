/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <ostream>
#include <sstream>
#include <cstdio>
#include <ctime>

#include "RandomGenerator.h"
#include "FileDescriptor.h"
#include "SystemTime.h"
#include "MessageClient.h"
#include "StaticString.h"
#include "Exceptions.h"
#include "Utils.h"


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/********** Global logging facilities **********/

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


/********** Request-specific logging *********/

class TxnLog {
private:
	FileDescriptor handle;
	string fullId;
	string shortId;
	
	void atomicWrite(const char *data, unsigned int size) {
		int ret;
		
		ret = write(handle, data, size);
		if (ret == -1) {
			int e = errno;
			throw SystemException("Cannot write to the transaction log", e);
		} else if ((unsigned int) ret != size) {
			throw IOException("Cannot atomically write to the transaction log");
		}
	}
	
public:
	TxnLog() { }
	
	TxnLog(const FileDescriptor &handle, const string &fullId, const string &shortId) {
		this->handle  = handle;
		this->fullId  = fullId;
		this->shortId = shortId;
		message("BEGIN");
	}
	
	~TxnLog() {
		if (handle != -1) {
			message("END");
		}
	}
	
	void message(const StaticString &text) {
		if (handle != -1) {
			char timestampString[22]; // Long enough for a 64-bit number.
			size_t timestampStringLen;
			int size;
			
			size = snprintf(timestampString, sizeof(timestampString),
				"%llu", SystemTime::getMsec());
			if (size >= (int) sizeof(timestampString)) {
				// The buffer is too small.
				TRACE_POINT();
				throw IOException("Cannot format a new transaction log message timestamp.");
			}
			timestampStringLen = strlen(timestampString);
			
			// "txn-id-here 123456: log message here\n"
			char data[shortId.size() + 1 + timestampStringLen + 2 + text.size() + 1];
			char *end = data;
			
			// "txn-id-here"
			memcpy(data, shortId.c_str(), shortId.size());
			end += shortId.size();
			
			// "txn-id-here "
			*end = ' ';
			end++;
			
			// "txn-id-here 123456"
			memcpy(end, timestampString, timestampStringLen);
			end += timestampStringLen;
			
			// "txn-id-here 123456: "
			*end = ':';
			end++;
			*end = ' ';
			end++;
			
			// "txn-id-here 123456: log message here"
			memcpy(end, text.c_str(), text.size());
			end += text.size();
			
			// "txn-id-here 123456: log message here\n"
			*end = '\n';
			
			atomicWrite(data, sizeof(data));
		}
	}
	
	bool isNull() const {
		return handle == -1;
	}
	
	string getShortId() const {
		return shortId;
	}
	
	string getFullId() const {
		return fullId;
	}
};

typedef shared_ptr<TxnLog> TxnLogPtr;

class TxnLogger {
private:
	string dir;
	string socketFilename;
	string username;
	string password;
	RandomGenerator randomGenerator;
	
	boost::mutex lock;
	string currentLogFile;
	FileDescriptor currentLogHandle;
	
	FileDescriptor openLogFile(unsigned long long timestamp) {
		string logFile = determineLogFilename(dir, timestamp);
		lock_guard<boost::mutex> l(lock);
		if (logFile != currentLogFile) {
			MessageClient client;
			FileDescriptor fd;
			vector<string> args;
			
			client.connect(socketFilename, username, password);
			client.write("open log file", toString(timestamp).c_str(), NULL);
			if (!client.read(args)) {
				// TODO: retry in a short while because the watchdog may restart
				// the logging server
				throw IOException("The logging server unexpectedly closed the connection.");
			}
			if (args[0] == "error") {
				throw IOException("The logging server could not open the log file: " + args[1]);
			}
			fd = client.readFileDescriptor();
			
			currentLogFile   = logFile;
			currentLogHandle = fd;
		}
		return currentLogHandle;
	}
	
	bool parseFullId(const string &fullId, unsigned long long &timestamp, string &shortId) const {
		const char *shortIdBegin = strchr(fullId.c_str(), '-');
		if (shortIdBegin != NULL) {
			char timestampString[shortIdBegin - fullId.c_str() + 1];
			
			memcpy(timestampString, fullId.c_str(), shortIdBegin - fullId.c_str());
			timestampString[shortIdBegin - fullId.c_str()] = '\0';
			timestamp = atoll(timestampString);
			
			shortId.assign(shortIdBegin + 1);
			
			return true;
		} else {
			return false;
		}
	}
	
public:
	TxnLogger(const string &dir, const string &socketFilename, const string &username, const string &password) {
		this->dir            = dir;
		this->socketFilename = socketFilename;
		this->username       = username;
		this->password       = password;
	}
	
	static string determineLogFilename(const string &dir, unsigned long long timestamp) {
		struct tm tm;
		time_t time_value;
		string filename = dir + "/1/";
		char dateName[12];
		
		time_value = timestamp / 1000;
		localtime_r(&time_value, &tm);
		strftime(dateName, sizeof(dateName), "%G/%m/%d", &tm);
		filename.append(dateName);
		filename.append("/web_txns.txt");
		return filename;
	}
	
	TxnLogPtr newTransaction() {
		unsigned long long timestamp = SystemTime::getMsec();
		string shortId = toHex(randomGenerator.generateByteString(20));
		string fullId = toString(timestamp);
		fullId.append("-");
		fullId.append(shortId);
		return ptr(new TxnLog(openLogFile(timestamp), fullId, shortId));
	}
	
	TxnLogPtr continueTransaction(const string &fullId) {
		unsigned long long timestamp;
		string shortId;
		
		if (!parseFullId(fullId, timestamp, shortId)) {
			TRACE_POINT();
			throw ArgumentException("Invalid transaction full ID '" + fullId + "'");
		}
		return ptr(new TxnLog(openLogFile(timestamp), fullId, shortId));
	}
};

typedef shared_ptr<TxnLogger> TxnLoggerPtr;

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_H_ */

