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
#include "Timer.h"
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

class RequestLogger {
public:
	class RequestLog {
	private:
		friend class RequestLogger;
		
		Timer timer;
		int fd;
		string filename;
		string id;
		bool owner;
		
		RequestLog(const string &filename, int fd, const string &id, bool owner) {
			this->filename = filename;
			this->fd = fd;
			this->id = id;
			this->owner = owner;
		}
		
		void atomicWrite(const char *data, unsigned int size) {
			int ret;
			
			ret = write(fd, data, size);
			if (ret == -1) {
				int e = errno;
				throw SystemException("Cannot write to the request log", e);
			} else if ((unsigned int) ret != size) {
				throw IOException("Cannot atomically write to the request log");
			}
		}

	public:
		~RequestLog() {
			if (fd != -1 && owner) {
				this_thread::disable_syscall_interruption dsi;
				abort();
			}
		}
		
		string getFilename() const {
			return filename;
		}
		
		string getId() const {
			return id;
		}
		
		void log(const StaticString &message) {
			char data[id.size() + message.size() + 60];
			int len;
			
			len = snprintf(data, sizeof(data), "Request %s at t+%llu: %s\n",
				id.c_str(), timer.elapsed(), message.c_str());
			if ((unsigned int) len >= sizeof(data)) {
				throw IOException("Cannot format a request log message.");
			}
			atomicWrite(data, len);
		}
		
		void commit() {
			char data[id.size() + 40];
			int len;
			
			len = snprintf(data, sizeof(data), "Request %s finished at t+%llu\n",
				id.c_str(), timer.elapsed());
			if ((unsigned int) len >= sizeof(data)) {
				throw IOException("Cannot format a request log commit message.");
			}
			atomicWrite(data, len);
			syscalls::close(fd);
			fd = -1;
		}
		
		void abort() {
			char data[id.size() + 40];
			int len;
			
			len = snprintf(data, sizeof(data), "Request %s aborted at t+%llu\n",
				id.c_str(), timer.elapsed());
			if ((unsigned int) len >= sizeof(data)) {
				throw IOException("Cannot format a request log abort message.");
			}
			atomicWrite(data, len);
			syscalls::close(fd);
			fd = -1;
		}
	};
	
	typedef shared_ptr<RequestLog> RequestLogPtr;
	
private:
	string filename;
	RandomGenerator randomGenerator;
	
	static int openLogFile(const string &filename) {
		return syscalls::open(filename.c_str(), O_CREAT | O_WRONLY | O_APPEND,
			S_IRUSR | S_IWUSR |
			S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH);
	}
	
public:
	RequestLogger(const string &filename) {
		this->filename = filename;
	}
	
	RequestLogPtr newRequest() {
		TRACE_POINT();
		int fd;
		
		fd = openLogFile(filename);
		try {
			struct timeval timestamp;
			string id;
			char message[40 + 40], buf[20];
			int ret, len;
			
			do {
				ret = gettimeofday(&timestamp, NULL);
			} while (ret == -1 && errno == EINTR);
			
			id = toHex(randomGenerator.generateBytes(buf, 20));
			len = snprintf(message, sizeof(message), "New transaction %s at t=%llu\n",
				id.c_str(),
				(unsigned long long) (timestamp.tv_sec + timestamp.tv_usec / 1000));
			if ((unsigned int) len >= sizeof(message)) {
				// The buffer is too small.
				throw IOException("Cannot format a new request log start message.");
			}
			
			UPDATE_TRACE_POINT();
			ret = syscalls::write(fd, message, len);
			if (ret != len) {
				if (ret == -1) {
					int e = errno;
					throw FileSystemException(
						"Cannot write to the request log file " + filename,
						e, filename);
				} else {
					throw IOException("Cannot atomically write a request log start message.");
				}
			}
			
			return RequestLogPtr(new RequestLog(filename, fd, id, true));
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			syscalls::close(fd);
			throw;
		}
	}
	
	static RequestLogPtr continueLog(const string &logFile, const string &id, bool owner = false) {
		return RequestLogPtr(new RequestLog(logFile, openLogFile(logFile), id, owner));
	}
};

typedef shared_ptr<RequestLogger::RequestLog> RequestLogPtr;

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_H_ */

