#include "Backtrace.h"

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

