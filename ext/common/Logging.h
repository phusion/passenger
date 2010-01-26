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
#include "SystemTime.h"
#include "MessageClient.h"
#include "StaticString.h"
#include "Exceptions.h"
#include "Utils.h"


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

class TxnLog {
private:
	static const int INT64_STR_BUFSIZE = 22; // Long enough for a 64-bit number.
	
	FileDescriptor handle;
	string groupName;
	string id;
	
	/**
	 * @throws SystemException
	 * @throws IOException
	 * @throws boost::thread_interrupted
	 */
	void atomicWrite(const char *data, unsigned int size) {
		int ret;
		
		ret = syscalls::write(handle, data, size);
		if (ret == -1) {
			int e = errno;
			throw SystemException("Cannot write to the transaction log", e);
		} else if ((unsigned int) ret != size) {
			throw IOException("Cannot atomically write to the transaction log");
		}
	}
	
	/**
	 * Buffer must be at least id.size() + 1 + INT64_STR_BUFSIZE + 1 bytes.
	 */
	char *insertIdAndTimestamp(char *buffer) {
		int size;
		
		// "txn-id-here"
		memcpy(buffer, id.c_str(), id.size());
		buffer += id.size();
		
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
	TxnLog() { }
	
	TxnLog(const FileDescriptor &handle, const string &sanitizedGroupName, const string &id) {
		this->handle    = handle;
		this->groupName = sanitizedGroupName;
		this->id        = id;
		message("ATTACH");
	}
	
	~TxnLog() {
		if (handle != -1) {
			message("DETACH");
		}
	}
	
	void message(const StaticString &text) {
		if (handle != -1) {
			// We want: "txn-id-here 123456 log message here\n"
			char data[id.size() + 1 + INT64_STR_BUFSIZE + 1 + text.size() + 1];
			char *end;
			
			// "txn-id-here 123456 "
			end = insertIdAndTimestamp(data);
			
			// "txn-id-here 123456 log message here"
			memcpy(end, text.c_str(), text.size());
			end += text.size();
			
			// "txn-id-here 123456 log message here\n"
			*end = '\n';
			end++;
			
			atomicWrite(data, end - data);
		}
	}
	
	void abort(const StaticString &text) {
		if (handle != -1) {
			// We want: "txn-id-here 123456 ABORT: log message here\n"
			char data[id.size() + 1 + INT64_STR_BUFSIZE + 1 +
				(sizeof("ABORT: ") - 1) + text.size() + 1];
			char *end;
			
			// "txn-id-here 123456 "
			end = insertIdAndTimestamp(data);
			
			// "txn-id-here 123456 ABORT: "
			memcpy(end, "ABORT: ", sizeof("ABORT: ") - 1);
			end += sizeof("ABORT: ") - 1;
			
			// "txn-id-here 123456 ABORT: log message here\n"
			*end = '\n';
			end++;
			
			atomicWrite(data, end - data);
		}
	}
	
	bool isNull() const {
		return handle == -1;
	}
	
	string getId() const {
		return id;
	}
	
	/** Guaranteed to pass TxnLogger::groupNameIsSane(). */
	string getGroupName() const {
		return groupName;
	}
};

typedef shared_ptr<TxnLog> TxnLogPtr;

class TxnScopeLog {
private:
	TxnLog *log;
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
public:
	TxnScopeLog(const TxnLogPtr &log, const char *name) {
		this->log = log.get();
		type = NAME;
		data.name = name;
		ok = false;
		log->message(string("BEGIN: ") + name);
	}
	
	TxnScopeLog(const TxnLogPtr &log, const char *beginMessage,
	            const char *endMessage, const char *abortMessage = NULL
	) {
		this->log = log.get();
		type = GRANULAR;
		data.granular.endMessage = endMessage;
		data.granular.abortMessage = abortMessage;
		ok = abortMessage == NULL;
		log->message(beginMessage);
	}
	
	~TxnScopeLog() {
		if (type == NAME) {
			if (ok) {
				log->message(string("END: ") + data.name);
			} else {
				log->message(string("FAIL: ") + data.name);
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

class TxnLogger {
private:
	struct CachedFileHandle {
		FileDescriptor fd;
		time_t lastUsed;
	};
	
	typedef map<string, CachedFileHandle> Cache;
	
	string dir;
	string socketFilename;
	string username;
	string password;
	RandomGenerator randomGenerator;
	
	boost::mutex lock;
	Cache fileHandleCache;
	
	FileDescriptor openLogFile(const StaticString &sanitizedGroupName, unsigned long long timestamp) {
		string logFile = determineLogFilename(dir, sanitizedGroupName, timestamp);
		Cache::iterator it;
		lock_guard<boost::mutex> l(lock);
		
		it = fileHandleCache.find(logFile);
		if (it == fileHandleCache.end()) {
			/* If there are more than 10 analytics groups then this server is probably
			 * a low-traffic server or a shared host. In such a situation opening the
			 * transaction log file won't be a significant performance bottleneck.
			 * Therefore I think the hardcoded cache limit of 10 is justified.
			 */
			while (fileHandleCache.size() >= 10) {
				Cache::iterator oldest_it = fileHandleCache.begin();
				it = oldest_it;
				it++;
				
				for (; it != fileHandleCache.end(); it++) {
					if (it->second.lastUsed < oldest_it->second.lastUsed) {
						oldest_it = it;
					}
				}
				
				fileHandleCache.erase(oldest_it);
			}
			
			MessageClient client;
			CachedFileHandle fileHandle;
			vector<string> args;
			
			client.connect(socketFilename, username, password);
			client.write("open log file", sanitizedGroupName.c_str(),
				toString(timestamp).c_str(), NULL);
			if (!client.read(args)) {
				// TODO: retry in a short while because the watchdog may restart
				// the logging server
				throw IOException("The logging server unexpectedly closed the connection.");
			}
			if (args[0] == "error") {
				throw IOException("The logging server could not open the log file: " + args[1]);
			}
			fileHandle.fd = client.readFileDescriptor();
			fileHandle.lastUsed = SystemTime::get();
			it = fileHandleCache.insert(make_pair(logFile, fileHandle)).first;
		} else {
			it->second.lastUsed = SystemTime::get();
		}
		return it->second.fd;
	}
	
	unsigned long long extractTimestamp(const string &id) const {
		const char *timestampBegin = strchr(id.c_str(), '-');
		if (timestampBegin != NULL) {
			return atoll(timestampBegin + 1);
		} else {
			return 0;
		}
	}
	
	static bool saneGroupNameCharacter(char c) {
		/* Don't allow any other characters; the Rails router
		 * has problems with generating RESTful paths where
		 * ID contains a dot. There might also be other
		 * characters that can cause problems.
		 */
		return (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| c == '_' || c == '-' || c == '%';
	}
	
public:
	TxnLogger() { }
	
	TxnLogger(const string &dir, const string &socketFilename, const string &username, const string &password) {
		this->dir            = dir;
		this->socketFilename = socketFilename;
		this->username       = username;
		this->password       = password;
	}
	
	static bool groupNameIsSane(const StaticString &groupName) {
		string::size_type i = 0;
		bool result = true;
		while (i < groupName.size() && result) {
			result = result && saneGroupNameCharacter(groupName[i]);
			i++;
		}
		return result;
	}
	
	static string sanitizeGroupName(const string &groupName) {
		// URL escaped result cannot possibly be larger than this:
		char result[groupName.size() * 3];
		char *end = result;
		string::size_type i;
		static const unsigned char hex_chars[] = {
			'0', '1', '2', '3', '4', '5', '6', '7',
			'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
		};
		
		for (i = 0; i < groupName.size(); i++) {
			unsigned char c = (unsigned char) groupName[i];
			
			if (saneGroupNameCharacter(c) && c != '%') {
				*end = c;
				end++;
			} else {
				end[0] = '%';
				end[1] = hex_chars[c / 16];
				end[2] = hex_chars[c % 16];
				end += 3;
			}
		}
		
		return string(result, end - result);
	}
	
	static string determineLogFilename(const string &dir,
		const StaticString &sanitizedGroupName, unsigned long long timestamp)
	{
		struct tm tm;
		time_t time_value;
		string filename = dir + "/1/";
		char dateName[14];
		
		if (!groupNameIsSane(sanitizedGroupName)) {
			TRACE_POINT();
			throw ArgumentException("Unsanitized analytics group name '" +
				sanitizedGroupName + "'");
		}
		
		time_value = timestamp / 1000000;
		localtime_r(&time_value, &tm);
		// Index log on the filesystem by its group name and begin time.
		strftime(dateName, sizeof(dateName), "%G/%m/%d/%H", &tm);
		filename.append(sanitizedGroupName.data(), sanitizedGroupName.size());
		filename.append(1, '/');
		filename.append(dateName);
		filename.append("/web_txns.txt");
		return filename;
	}
	
	TxnLogPtr newTransaction(const string &sanitizedGroupName) {
		if (dir.empty() || sanitizedGroupName.empty()) {
			return ptr(new TxnLog());
		} else {
			unsigned long long timestamp = SystemTime::getUsec();
			string id = toHex(randomGenerator.generateByteString(4));
			id.append("-");
			id.append(toString(timestamp));
			return ptr(new TxnLog(openLogFile(sanitizedGroupName, timestamp),
				sanitizedGroupName, id));
		}
	}
	
	TxnLogPtr continueTransaction(const string &sanitizedGroupName, const string &id) {
		if (dir.empty() || sanitizedGroupName.empty()) {
			return ptr(new TxnLog());
		} else {
			unsigned long long timestamp;
			
			timestamp = extractTimestamp(id);
			if (timestamp == 0) {
				TRACE_POINT();
				throw ArgumentException("Invalid transaction ID '" + id + "'");
			}
			return ptr(new TxnLog(openLogFile(sanitizedGroupName, timestamp),
				sanitizedGroupName, id));
		}
	}
	
	bool isNull() const {
		return dir.empty();
	}
};

typedef shared_ptr<TxnLogger> TxnLoggerPtr;

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_H_ */

