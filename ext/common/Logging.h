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
#include "Utils.h"
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

class AnalyticsLog {
private:
	static const int INT64_STR_BUFSIZE = 22; // Long enough for a 64-bit number.
	
	FileDescriptor handle;
	string groupName;
	string txnId;
	bool largeMessages;
	
	class FileLock {
	private:
		int handle;
	public:
		FileLock(const int &handle) {
			this->handle = handle;
			int ret;
			do {
				ret = ::flock(handle, LOCK_EX);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				int e = errno;
				throw SystemException("Cannot lock analytics log file", e);
			}
		}
		
		~FileLock() {
			int ret;
			do {
				ret = ::flock(handle, LOCK_UN);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				int e = errno;
				throw SystemException("Cannot unlock analytics log file", e);
			};
		}
	};
	
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
	
	AnalyticsLog(const FileDescriptor &handle, const string &groupName, const string &txnId,
	             bool largeMessages)
	{
		this->handle    = handle;
		this->groupName = groupName;
		this->txnId     = txnId;
		this->largeMessages = largeMessages;
		message("ATTACH");
	}
	
	~AnalyticsLog() {
		if (handle != -1) {
			message("DETACH");
		}
	}
	
	void message(const StaticString &text) {
		if (handle != -1 && largeMessages) {
			// "txn-id-here 123456 "
			char header[txnId.size() + 1 + INT64_STR_BUFSIZE + 1];
			char *end = insertTxnIdAndTimestamp(header);
			char sizeHeader[7];
			
			snprintf(sizeHeader, sizeof(sizeHeader) - 1,
				"%4x ", (int) (end - header) + (int) text.size() + 1);
			sizeHeader[sizeof(sizeHeader) - 1] = '\0';
			
			MessageChannel channel(handle);
			FileLock lock(handle);
			channel.writeRaw(sizeHeader, strlen(sizeHeader));
			channel.writeRaw(header, end - header);
			channel.writeRaw(text);
			channel.writeRaw("\n");
		} else if (handle != -1 && !largeMessages) {
			// We want: "txn-id-here 123456 log message here\n"
			char data[txnId.size() + 1 + INT64_STR_BUFSIZE + 1 + text.size() + 1];
			char *end;
			
			// "txn-id-here 123456 "
			end = insertTxnIdAndTimestamp(data);
			
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
		if (handle != -1 && largeMessages) {
			char header[txnId.size() + 1 + INT64_STR_BUFSIZE + 1 + sizeof("ABORT: ") - 1];
			char *end;
			char sizeHeader[7];
			
			// "txn-id-here 123456 "
			end = insertTxnIdAndTimestamp(header);
			// "txn-id-here 123456 ABORT: "
			memcpy(end, "ABORT: ", sizeof("ABORT: ") - 1);
			end += sizeof("ABORT: ") - 1;
			
			snprintf(sizeHeader, sizeof(sizeHeader) - 1,
				"%4x ", (int) (end - header) + (int) text.size() + 1);
			sizeHeader[sizeof(sizeHeader) - 1] = '\0';
			
			MessageChannel channel(handle);
			FileLock lock(handle);
			channel.writeRaw(sizeHeader, strlen(sizeHeader));
			channel.writeRaw(header, end - header);
			channel.writeRaw(text);
			channel.writeRaw("\n");
		} else if (handle != -1 && !largeMessages) {
			// We want: "txn-id-here 123456 ABORT: log message here\n"
			char data[txnId.size() + 1 + INT64_STR_BUFSIZE + 1 +
				(sizeof("ABORT: ") - 1) + text.size() + 1];
			char *end;
			
			// "txn-id-here 123456 "
			end = insertTxnIdAndTimestamp(data);
			
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
	
	string getGroupName() const {
		return groupName;
	}
	
	string getTxnId() const {
		return txnId;
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
			message.append(" (utime = ");
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
				message.append(" (utime = ");
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
	struct CachedFileHandle {
		FileDescriptor fd;
		time_t lastUsed;
	};
	
	typedef map<string, CachedFileHandle> Cache;
	
	string socketFilename;
	string username;
	string password;
	string nodeName;
	RandomGenerator randomGenerator;
	
	boost::mutex lock;
	Cache fileHandleCache;
	
	FileDescriptor openLogFile(const StaticString &groupName, unsigned long long timestamp,
	                           const StaticString &nodeName, const StaticString &category = "requests")
	{
		string logFile = determineLogFilename("", groupName, nodeName, category, timestamp);
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
			client.write("open log file",
				groupName.c_str(),
				toString(timestamp).c_str(),
				nodeName.c_str(),
				category.c_str(),
				NULL);
			if (!client.read(args)) {
				// TODO: retry in a short while because the watchdog may restart
				// the logging server
				throw IOException("The logging agent unexpectedly closed the connection.");
			}
			if (args[0] == "error") {
				throw IOException("The logging agent could not open the log file: " + args[1]);
			}
			fileHandle.fd = client.readFileDescriptor();
			fileHandle.lastUsed = SystemTime::get();
			it = fileHandleCache.insert(make_pair(logFile, fileHandle)).first;
		} else {
			it->second.lastUsed = SystemTime::get();
		}
		return it->second.fd;
	}
	
	unsigned long long extractTimestamp(const string &txnId) const {
		const char *timestampBegin = strchr(txnId.c_str(), '-');
		if (timestampBegin != NULL) {
			return atoll(timestampBegin + 1);
		} else {
			return 0;
		}
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
	}
	
	static void determineGroupAndNodeDir(const string &dir, const StaticString &groupName,
		const StaticString &nodeName, string &groupDir, string &nodeDir)
	{
		string result = dir;
		appendVersionAndGroupId(result, groupName);
		groupDir = result;
		result.append(1, '/');
		appendNodeId(result, nodeName);
		nodeDir = result;
	}
	
	static void appendVersionAndGroupId(string &output, const StaticString &groupName) {
		md5_state_t state;
		md5_byte_t  digest[MD5_SIZE];
		char        checksum[MD5_HEX_SIZE];
		
		output.append("/1/", 3);
		
		md5_init(&state);
		md5_append(&state, (const md5_byte_t *) groupName.data(), groupName.size());
		md5_finish(&state, digest);
		toHex(StaticString((const char *) digest, MD5_SIZE), checksum);
		output.append(checksum, MD5_HEX_SIZE);
	}
	
	static void appendNodeId(string &output, const StaticString &nodeName) {
		md5_state_t state;
		md5_byte_t  digest[MD5_SIZE];
		char        checksum[MD5_HEX_SIZE];
		
		md5_init(&state);
		md5_append(&state, (const md5_byte_t *) nodeName.data(), nodeName.size());
		md5_finish(&state, digest);
		toHex(StaticString((const char *) digest, MD5_SIZE), checksum);
		output.append(checksum, MD5_HEX_SIZE);
	}
	
	static string determineLogFilename(const StaticString &dir,
		const StaticString &groupName, const StaticString &nodeName,
		const StaticString &category, unsigned long long timestamp)
	{
		struct tm tm;
		time_t time_value;
		char time_str[14];
		
		time_value = timestamp / 1000000;
		gmtime_r(&time_value, &tm);
		strftime(time_str, sizeof(time_str), "%Y/%m/%d/%H", &tm);
		
		string filename;
		filename.reserve(dir.size()
			+ (3 + MD5_HEX_SIZE) // version and group ID
			+ 1                  // "/"
			+ MD5_HEX_SIZE       // node ID
			+ 1                  // "/"
			+ category.size()
			+ 1                  // "/"
			+ sizeof(time_str)   // including null terminator, which we use as space for "/"
			+ sizeof("log.txt")
		);
		filename.append(dir.c_str(), dir.size());
		appendVersionAndGroupId(filename, groupName);
		filename.append(1, '/');
		appendNodeId(filename, nodeName);
		filename.append(1, '/');
		filename.append(category.c_str(), category.size());
		filename.append(1, '/');
		filename.append(time_str);
		filename.append("/log.txt");
		return filename;
	}
	
	AnalyticsLogPtr newTransaction(const string &groupName, const StaticString &category = "requests",
	                               bool largeMessages = false)
	{
		if (socketFilename.empty()) {
			return ptr(new AnalyticsLog());
		} else {
			unsigned long long timestamp = SystemTime::getUsec();
			string txnId = randomGenerator.generateHexString(4);
			txnId.append("-");
			txnId.append(toString(timestamp));
			return ptr(new AnalyticsLog(
				openLogFile(groupName, timestamp, nodeName, category),
				groupName, txnId, largeMessages));
		}
	}
	
	AnalyticsLogPtr continueTransaction(const string &groupName, const string &txnId,
	                                    const StaticString &category = "requests",
	                                    bool largeMessages = false)
	{
		if (socketFilename.empty() || groupName.empty() || txnId.empty()) {
			return ptr(new AnalyticsLog());
		} else {
			unsigned long long timestamp;
			
			timestamp = extractTimestamp(txnId);
			if (timestamp == 0) {
				TRACE_POINT();
				throw ArgumentException("Invalid transaction ID '" + txnId + "'");
			}
			return ptr(new AnalyticsLog(
				openLogFile(groupName, timestamp, nodeName, category),
				groupName, txnId, largeMessages));
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

