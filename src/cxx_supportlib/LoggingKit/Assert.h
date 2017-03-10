/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_LOGGING_KIT_ASSERT_H_
#define _PASSENGER_LOGGING_KIT_ASSERT_H_

#include <oxt/backtrace.hpp>
#include <cstddef>
#include <cstdlib>

#include <LoggingKit/Logging.h>
#include <Utils/FastStringStream.h>

namespace Passenger {
namespace LoggingKit {


struct AssertionFailureInfo {
	const char *filename;
	const char *function; // May be NULL.
	const char *expression;
	unsigned int line;

	AssertionFailureInfo()
		: filename(NULL),
		  function(NULL),
		  expression(NULL),
		  line(0)
		{ }
};

// If assert() or similar fails, we attempt to store its information here.
extern AssertionFailureInfo lastAssertionFailure;


/*
 * The P_BUG family of macros allow you to print a [BUG] error message
 * and abort with a stack trace.
 *
 * P_BUG(expr)
 *     Prints the given expression and aborts.
 *
 * P_BUG_WITH_FORMATTER_CODE(varname, code)
 *     Same effect as P_BUG, but it allows more fine-grained control over the behavior.
 *     This macro declares the internal string stream object with a name defined by `varname`,
 *     and evaluates the `code`. The given code is supposed to use `<<` calls to append
 *     add text into the stream.
 *     When the code is done evaluating, this macro prints the string stream and aborts.
 *
 * P_BUG_UTP(expr)
 * P_BUG_UTP_WITH_FORMATTER_CODE(varname, code)
 *     Like P_BUG/P_BUG_WITH_FORMATTER_CODE, but instead of allocating a `TRACE_POINT()`
 *     (which may conflict if the calling function already has one defined), it calls
 *     `UPDATE_TRACE_POINT()` instead.
 */

/** Print a [BUG] error message and abort with a stack trace. */
#define P_BUG_WITH_FORMATTER_CODE(varname, code) \
	do { \
		TRACE_POINT(); \
		const char *_exprStr; \
		Passenger::FastStringStream<> varname; \
		code \
		_exprStr = Passenger::LoggingKit::_strdupFastStringStream(varname); \
		Passenger::LoggingKit::lastAssertionFailure.filename = __FILE__; \
		Passenger::LoggingKit::lastAssertionFailure.line = __LINE__; \
		Passenger::LoggingKit::lastAssertionFailure.function = __PRETTY_FUNCTION__; \
		Passenger::LoggingKit::lastAssertionFailure.expression = _exprStr; \
		P_CRITICAL("[BUG] " << _exprStr); \
		abort(); \
	} while (false)

#define P_BUG_UTP_WITH_FORMATTER_CODE(varname, code) \
	do { \
		UPDATE_TRACE_POINT(); \
		const char *_exprStr; \
		Passenger::FastStringStream<> varname; \
		code \
		_exprStr = Passenger::LoggingKit::_strdupFastStringStream(varname); \
		Passenger::LoggingKit::lastAssertionFailure.filename = __FILE__; \
		Passenger::LoggingKit::lastAssertionFailure.line = __LINE__; \
		Passenger::LoggingKit::lastAssertionFailure.function = __PRETTY_FUNCTION__; \
		Passenger::LoggingKit::lastAssertionFailure.expression = _exprStr; \
		P_CRITICAL("[BUG] " << _exprStr); \
		abort(); \
	} while (false)

#define P_BUG(expr) P_BUG_WITH_FORMATTER_CODE( _sstream , _sstream << expr; )
#define P_BUG_UTP(expr) P_BUG_UTP_WITH_FORMATTER_CODE( _sstream , _sstream << expr; )


/**
 * Asserts whether the actual value equals the expected value.
 * If not, it prints a message that prints how the two values differ
 * and aborts.
 */
#define P_ASSERT_EQ(value, expected) \
	do { \
		if (OXT_UNLIKELY(value != expected)) { \
			P_BUG("Expected " << #value << " to be " << expected << ", got " << value); \
		} \
	} while (false)


} // namespace LoggingKit
} // namespace Passenger

#endif /* _PASSENGER_LOGGING_KIT_ASSERT_H_ */
