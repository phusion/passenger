/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010, 2011, 2012 Phusion
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

#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/macros.hpp>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <exception>
#include <stdexcept>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <cerrno>


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
			strftime(datetime_buf, sizeof(datetime_buf) - 1, "%F %H:%M:%S", &the_tm); \
			gettimeofday(&tv, NULL); \
			sstream << \
				"[ pid=" << ((unsigned long) getpid()) <<  \
				" thr=" << pthread_self() << \
				" file=" << __FILE__ << ":" << (unsigned long) __LINE__ << \
				" time=" << datetime_buf << "." << std::setfill('0') << std::setw(4) << \
					(unsigned long) (tv.tv_usec / 100) << \
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
 * Write the given expression, which represents a warning,
 * to the log stream.
 */
#define P_INFO(expr) P_LOG(0, expr)

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
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
#endif


class NotExpectingExceptions {
private:
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	const char *filename;
	const char *function;
	unsigned int line;

public:
	NotExpectingExceptions(const char *_filename, unsigned int _line, const char *_function) {
		filename = _filename;
		line = _line;
		function = _function;
	}

	~NotExpectingExceptions() {
		if (std::uncaught_exception()) {
			P_ERROR("Unexpected exception detected at " << filename <<
				":" << line << ", function '" << function << "'!");
		}
	}
};

/**
 * Put this in code sections where you don't expect *any* exceptions to be thrown.
 * This macro will automatically disables interruptions in the current scope,
 * and will print an error message whenever the scope exits with an exception.
 *
 * When inside critical sections, you should put this macro right after the lock
 * object so that the error message is displayed before unlocking the lock;
 * otherwise other threads may run before the error message is displayed, and
 * those threads may see an inconsistant state and crash.
 */
#define NOT_EXPECTING_EXCEPTIONS() NotExpectingExceptions __nee(__FILE__, __LINE__, __PRETTY_FUNCTION__)

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_H_ */
