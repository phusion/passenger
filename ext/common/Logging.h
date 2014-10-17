/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
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
#include <csignal>


namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/********** Debug logging facilities **********/

struct AssertionFailureInfo {
	const char *filename;
	const char *function; // May be NULL.
	const char *expression;
	unsigned int line;
};

extern volatile sig_atomic_t _logLevel;
// If assert() or similar fails, we attempt to store its information here.
extern AssertionFailureInfo lastAssertionFailure;

inline OXT_FORCE_INLINE int
getLogLevel() {
	return (int) _logLevel;
}

void setLogLevel(int value);
bool setLogFile(const char *path); // Sets errno on error
string getLogFile();
void _prepareLogEntry(std::stringstream &sstream, const char *file, unsigned int line);
void _writeLogEntry(const std::string &str);
void _writeLogEntry(const char *str, unsigned int size);
const char *_strdupStringStream(const std::stringstream &stream);


enum PassengerLogLevel {
	LVL_CRIT   = 0,
	LVL_ERROR  = 1,
	LVL_WARN   = 2,
	LVL_NOTICE = 3,
	LVL_INFO   = 4,
	LVL_DEBUG  = 5,
	LVL_DEBUG2 = 6,
	LVL_DEBUG3 = 7
};

/**
 * Write the given expression to the log stream.
 */
#define P_LOG(level, file, line, expr) \
	do { \
		if (Passenger::getLogLevel() >= (level)) { \
			std::stringstream sstream; \
			Passenger::_prepareLogEntry(sstream, file, line); \
			sstream << expr << "\n"; \
			Passenger::_writeLogEntry(sstream.str()); \
		} \
	} while (false)

#define P_LOG_UNLIKELY(level, file, line, expr) \
	do { \
		if (OXT_UNLIKELY(Passenger::getLogLevel() >= (level))) { \
			std::stringstream sstream; \
			Passenger::_prepareLogEntry(sstream, file, line); \
			sstream << expr << "\n"; \
			Passenger::_writeLogEntry(sstream.str()); \
		} \
	} while (false)

/**
 * Write the given expression, which represents a warning,
 * to the log stream.
 */
#define P_WARN(expr) P_LOG(LVL_WARN, __FILE__, __LINE__, expr)
#define P_WARN_WITH_POS(file, line, expr) P_LOG(LVL_WARN, file, line, expr)

/**
 * Write the given expression, which represents a notice (important information),
 * to the log stream.
 */
#define P_NOTICE(expr) P_LOG(LVL_NOTICE, __FILE__, __LINE__, expr)
#define P_NOTICE_WITH_POS(file, line, expr) P_LOG(LVL_NOTICE, file, line, expr)

/**
 * Write the given expression, which represents a normal information message,
 * to the log stream.
 */
#define P_INFO(expr) P_LOG(LVL_INFO, __FILE__, __LINE__, expr)
#define P_INFO_WITH_POS(file, line, expr) P_LOG(LVL_INFO, file, line, expr)

/**
 * Write the given expression, which represents an error,
 * to the log stream.
 */
#define P_ERROR(expr) P_LOG(LVL_ERROR, __FILE__, __LINE__, expr)
#define P_ERROR_WITH_POS(file, line, expr) P_LOG(LVL_ERROR, file, line, expr)

/**
 * Write the given expression, which represents a critical non-recoverable error,
 * to the log stream.
 */
#define P_CRITICAL(expr) P_LOG(LVL_CRIT, __FILE__, __LINE__, expr)
#define P_CRITICAL_WITH_POS(expr, file, line) P_LOG(LVL_CRIT, file, line, expr)

/**
 * Write the given expression, which represents a debugging message,
 * to the log stream.
 */
#define P_DEBUG(expr) P_TRACE(1, expr)
#define P_DEBUG_WITH_POS(file, line, expr) P_TRACE_WITH_POS(1, file, line, expr)

#ifdef PASSENGER_DEBUG
	#define P_TRACE(level, expr) P_LOG_UNLIKELY(LVL_INFO + level, __FILE__, __LINE__, expr)
	#define P_TRACE_WITH_POS(level, file, line, expr) P_LOG_UNLIKELY(LVL_INFO + level, file, line, expr)
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
	#define P_TRACE_WITH_POS(level, file, line, expr) do { /* nothing */ } while (false)
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
		const char *_exprStr; \
		std::stringstream sstream; \
		sstream << expr; \
		_exprStr = Passenger::_strdupStringStream(sstream); \
		Passenger::lastAssertionFailure.filename = __FILE__; \
		Passenger::lastAssertionFailure.line = __LINE__; \
		Passenger::lastAssertionFailure.function = __PRETTY_FUNCTION__; \
		Passenger::lastAssertionFailure.expression = _exprStr; \
		P_CRITICAL("[BUG] " << _exprStr); \
		abort(); \
	} while (false)

#define P_BUG_UTP(expr) \
	do { \
		UPDATE_TRACE_POINT(); \
		const char *_exprStr; \
		std::stringstream sstream; \
		sstream << expr; \
		_exprStr = Passenger::_strdupStringStream(sstream); \
		Passenger::lastAssertionFailure.filename = __FILE__; \
		Passenger::lastAssertionFailure.line = __LINE__; \
		Passenger::lastAssertionFailure.function = __PRETTY_FUNCTION__; \
		Passenger::lastAssertionFailure.expression = _exprStr; \
		P_CRITICAL("[BUG] " << _exprStr); \
		abort(); \
	} while (false)

#define P_ASSERT_EQ(value, expected) \
	do { \
		if (OXT_UNLIKELY(value != expected)) { \
			P_BUG("Expected " << #value << " to be " << expected << ", got " << value); \
		} \
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
