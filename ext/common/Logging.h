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
#include <ios>
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
extern int _logOutput;

int getLogLevel();
void setLogLevel(int value);
bool setDebugFile(const char *logFile = NULL);
void _prepareLogEntry(std::stringstream &sstream, const char *file, unsigned int line);
void _writeLogEntry(const std::string &str);


enum PassengerLogLevel {
	LVL_CRIT   = -2,
	LVL_ERROR  = -1,
	LVL_INFO   = 0,
	LVL_WARN   = 0,
	LVL_DEBUG  = 1,
	LVL_DEBUG2 = 2
};

/**
 * Write the given expression to the log stream.
 */
#define P_LOG(level, expr) \
	do { \
		if (Passenger::_logLevel >= (level)) { \
			std::stringstream sstream; \
			Passenger::_prepareLogEntry(sstream, __FILE__, __LINE__); \
			sstream << expr << "\n"; \
			Passenger::_writeLogEntry(sstream.str()); \
		} \
	} while (false)

/**
 * Write the given expression, which represents a warning,
 * to the log stream.
 */
#define P_WARN(expr) P_LOG(LVL_WARN, expr)

/**
 * Write the given expression, which represents a warning,
 * to the log stream.
 */
#define P_INFO(expr) P_LOG(LVL_INFO, expr)

/**
 * Write the given expression, which represents an error,
 * to the log stream.
 */
#define P_ERROR(expr) P_LOG(LVL_ERROR, expr)

/**
 * Write the given expression, which represents a critical non-recoverable error,
 * to the log stream.
 */
#define P_CRITICAL(expr) P_LOG(LVL_CRIT, expr)

/**
 * Write the given expression, which represents a debugging message,
 * to the log stream.
 */
#define P_DEBUG(expr) P_TRACE(LVL_DEBUG, expr)

#ifdef PASSENGER_DEBUG
	#define P_TRACE(level, expr) P_LOG(level, expr)
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
#endif

/**
 * Print a message that was received from an application's stdout/stderr.
 *
 * @param pid The application's PID.
 * @param channelName "stdout" or "stderr".
 * @param message The message that was received.
 */
void printAppOutput(pid_t pid, const char *channelName, const char *message, unsigned int size);

/**
 * Controls how messages that are received from applications are printed.
 *
 * If `enabled` is true then messages are printed using P_DEBUG, meaning that
 * the normal Passenger logging prefixes will be printed as well.
 *
 * If `enabled` is false (the default), then messages are printed directly
 * to the log output channel using write(), with only a very short prefix
 * that contains the PID and channel name.
 */
void setPrintAppOutputAsDebuggingMessages(bool enabled);


/** Print a [BUG] error message and abort with a stack trace. */
#define P_BUG(expr) \
	do { \
		TRACE_POINT(); \
		P_CRITICAL("[BUG] " << expr); \
		abort(); \
	} while (false)

#define P_BUG_UTP(expr) \
	do { \
		UPDATE_TRACE_POINT(); \
		P_CRITICAL("[BUG] " << expr); \
		abort(); \
	} while (false)


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
 * This macro will automatically disable interruptions in the current scope,
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
