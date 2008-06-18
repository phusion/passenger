/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2008 Phusion
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "backtrace.h"

#if !defined(NDEBUG)

namespace Passenger {

#if defined(__GNUC__) && (__GNUC__ > 2)
	// Indicate that the given expression is (un)likely to be true.
	// This allows the CPU to better perform branch prediction.
	#define LIKELY(expr) __builtin_expect((expr), 1)
	#define UNLIKELY(expr) __builtin_expect((expr), 0)
#else
	#define LIKELY(expr) expr
	#define UNLIKELY(expr) expr
#endif

static thread_specific_ptr<boost::mutex> backtraceMutex;
static thread_specific_ptr< list<TracePoint *> > currentBacktrace;
boost::mutex _threadRegistrationMutex;
list<ThreadRegistration *> _registeredThreads;

boost::mutex &
_getBacktraceMutex() {
	boost::mutex *result;
	
	result = backtraceMutex.get();
	if (UNLIKELY(result == NULL)) {
		result = new boost::mutex();
		backtraceMutex.reset(result);
	}
	return *result;
}

list<TracePoint *> *
_getCurrentBacktrace() {
	list<TracePoint *> *result;
	
	result = currentBacktrace.get();
	if (UNLIKELY(result == NULL)) {
		result = new list<TracePoint *>();
		currentBacktrace.reset(result);
	}
	return result;
}

} // namespace Passenger

#endif

