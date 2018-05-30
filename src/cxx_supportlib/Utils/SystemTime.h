/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_SYSTEM_TIME_H_
#define _PASSENGER_SYSTEM_TIME_H_

#include <boost/thread.hpp>
#include <boost/predef.h>
#include <oxt/macros.hpp>
#include <oxt/system_calls.hpp>
#include <sys/time.h>
#include <cerrno>
#include <time.h>
#include <unistd.h>
#include <Exceptions.h>

#if BOOST_OS_MACOS
	#include <mach/mach.h>
	#include <mach/mach_time.h>
	#include <cstring>
#elif BOOST_OS_AIX
	#include <sys/systemcfg.h>
#elif defined(_POSIX_MONOTONIC_CLOCK) || defined(CLOCK_MONOTONIC)
	#define SYSTEM_TIME_HAVE_MONOTONIC_CLOCK
	#ifdef CLOCK_MONOTONIC_COARSE
		#define SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_COARSE
	#endif
	#ifdef CLOCK_MONOTONIC_FAST
		#define SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_FAST
	#endif
#endif

namespace Passenger {

using namespace std;
using namespace oxt;

namespace SystemTimeData {
	extern bool initialized;
	extern bool hasForcedValue;
	extern time_t forcedValue;
	extern bool hasForcedUsecValue;
	extern unsigned long long forcedUsecValue;

	#if BOOST_OS_MACOS
		extern mach_timebase_info_data_t timeInfo;
	#elif defined(SYSTEM_TIME_HAVE_MONOTONIC_CLOCK)
		#ifdef SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_COARSE
			extern unsigned long long monotonicCoarseResolutionNs;
		#endif
		#ifdef SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_FAST
			extern unsigned long long monotonicFastResolutionNs;
		#endif
		extern unsigned long long monotonicResolutionNs;
	#endif
}

typedef unsigned long long MonotonicTimeUsec;

/**
 * This class allows one to obtain the system time, similar to time() and
 * gettimeofday(). Unlike time(), it is possible to force a certain time
 * to be returned, which is useful for testing code that depends on the
 * system time.
 *
 * get() provides seconds resolution while getUsec() provides microseconds
 * resolution. Both clocks can be independently forced to a certain value
 * through force() and forceUsec().
 *
 * In addition, getMonotonicUsec() returns the monotonic clock in
 * microseconds. This can also be forced to a certain value using forceUsec().
 *
 * Before using any SystemTime functions, you should call
 * SystemTime::initialize(). If you don't do that, then initialize() will be
 * called for you, but since initialize() isn't thread-safe you should
 * call it at the beginning of your program.
 */
class SystemTime {
public:
	enum Granularity {
		GRAN_1SEC   = 1000000000,   // 1 second granularity
		GRAN_10MSEC = 10000000,     // 10 milliseconds granularity
		GRAN_1MSEC  = 1000000,      // 1 millisecond granularity
		GRAN_1USEC  = 1000          // 1 microsecond granularty
	};

private:
	static void initializeIfNeeded() {
		if (OXT_UNLIKELY(!SystemTimeData::initialized)) {
			initialize();
		}
	}

	template<Granularity granularityNs>
	static MonotonicTimeUsec _getMonotonicUsec() {
		if (OXT_UNLIKELY(SystemTimeData::hasForcedUsecValue)) {
			return SystemTimeData::forcedUsecValue;
		}

		#if BOOST_OS_MACOS
			initializeIfNeeded();
			if (SystemTimeData::timeInfo.numer == 0
			 && SystemTimeData::timeInfo.denom == 0)
			{
				return getUsec();
			} else {
				return mach_absolute_time()
					* SystemTimeData::timeInfo.numer
					/ SystemTimeData::timeInfo.denom
					/ 1000;
			}

		#elif BOOST_OS_SOLARIS
			return gethrtime() / 1000ull;

		#elif BOOST_OS_AIX
			timebasestruct_t t;
			read_wall_time(&t, TIMEBASE_SZ);
			time_base_to_time(&t, TIMEBASE_SZ);
			return t.tb_high * 1000000ull + t.tb_low / 1000ull;

		#elif defined(SYSTEM_TIME_HAVE_MONOTONIC_CLOCK)
			clockid_t clockId = (clockid_t) -1;
			struct timespec ts;
			int ret;

			initializeIfNeeded();

			// We choose a different monotonic clock
			// based on the resolution we need. In general,
			// coarser resolutions are faster, for example
			// because (on Linux) they are implemented
			// through VDSOs instead of system calls.
			//
			// Benchmarks and properties as of 10 March 2016:
			//
			// FreeBSD 10.2 (200000 iterations):
			// CLOCK_MONOTONIC           1m 9s      11 nanosec resolution
			// CLOCK_MONOTONIC_PRECISE   1m 9s      11 nanosec resolution
			// CLOCK_MONOTONIC_FAST      2s         11 nanosec resolution
			// gettimeofday              1m 9s
			//
			// Linux 3.13.0 (100000000 iterations):
			// CLOCK_MONOTONIC           1.5s        1 nanosec resolution
			// CLOCK_MONOTONIC_COARSE    0.45s       4 millisec resolution
			// gettimeofday              1.5s

			#ifdef SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_COARSE
				if (clockId == -1
				 && SystemTimeData::monotonicCoarseResolutionNs != 0
				 && SystemTimeData::monotonicCoarseResolutionNs <= granularityNs)
				{
					clockId = CLOCK_MONOTONIC_COARSE;
				}
			#endif
			#ifdef SYSTEM_TIME_HAVE_CLOCK_MONOTONIC_FAST
				if (clockId == -1
				 && SystemTimeData::monotonicFastResolutionNs != 0
				 && SystemTimeData::monotonicFastResolutionNs <= granularityNs)
				{
					clockId = CLOCK_MONOTONIC_FAST;
				}
			#endif
			if (clockId == -1
			 && SystemTimeData::monotonicResolutionNs != 0
			 && SystemTimeData::monotonicResolutionNs <= granularityNs)
			{
				clockId = CLOCK_MONOTONIC;
			}

			if (clockId == (clockid_t) -1) {
				return getUsec();
			} else {
				do {
					ret = clock_gettime(clockId, &ts);
				} while (ret == -1 && errno == EINTR);
				if (ret == -1) {
					int e = errno;
					throw TimeRetrievalException(
						"Unable to retrieve the system time",
						e);
				}
				return ts.tv_sec * 1000000ull + ts.tv_nsec / 1000ull;
			}

		#else
			return getUsec();
		#endif
	}

public:
	static void initialize() {
		SystemTimeData::initialized = true;
		#if BOOST_OS_MACOS
			if (mach_timebase_info(&SystemTimeData::timeInfo) != KERN_SUCCESS) {
				memset(&SystemTimeData::timeInfo, 0, sizeof(SystemTimeData::timeInfo));
			}
		#elif defined(SYSTEM_TIME_HAVE_MONOTONIC_CLOCK)
			struct timespec ts;

			#ifdef CLOCK_MONOTONIC_COARSE
				if (clock_getres(CLOCK_MONOTONIC_COARSE, &ts) == 0) {
					SystemTimeData::monotonicCoarseResolutionNs =
						ts.tv_sec * 1000000000ull +
						ts.tv_nsec;
				}
			#endif
			#ifdef CLOCK_MONOTONIC_FAST
				if (clock_getres(CLOCK_MONOTONIC_FAST, &ts) == 0) {
					SystemTimeData::monotonicFastResolutionNs =
						ts.tv_sec * 1000000000ull +
						ts.tv_nsec;
				}
			#endif
			if (clock_getres(CLOCK_MONOTONIC, &ts) == 0) {
				SystemTimeData::monotonicResolutionNs =
					ts.tv_sec * 1000000000ull +
					ts.tv_nsec;
			}
		#endif
	}

	/**
	 * Returns the time since the Epoch, measured in seconds. Or, if a time
	 * was forced with force(), then the forced time is returned instead.
	 *
	 * @throws TimeRetrievalException Something went wrong while retrieving the time.
	 * @throws boost::thread_interrupted
	 */
	static time_t get() {
		if (OXT_UNLIKELY(SystemTimeData::hasForcedValue)) {
			return SystemTimeData::forcedValue;
		} else {
			time_t ret = syscalls::time(NULL);
			if (ret == -1) {
				int e = errno;
				throw TimeRetrievalException(
					"Unable to retrieve the system time",
					e);
			}
			return ret;
		}
	}

	/**
	 * Returns the time since the Epoch, measured in microseconds. Or, if a
	 * time was forced with forceUsec(), then the forced time is returned instead.
	 *
	 * @throws TimeRetrievalException Something went wrong while retrieving the time.
	 * @throws boost::thread_interrupted
	 */
	static unsigned long long getUsec() {
		if (OXT_UNLIKELY(SystemTimeData::hasForcedUsecValue)) {
			return SystemTimeData::forcedUsecValue;
		} else {
			struct timeval t;
			int ret;

			do {
				ret = gettimeofday(&t, NULL);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				int e = errno;
				throw TimeRetrievalException(
					"Unable to retrieve the system time",
					e);
			}
			return (unsigned long long) t.tv_sec * 1000000 + t.tv_usec;
		}
	}

	/**
	 * Returns the time since an unspecified point in the last, measured in
	 * microseconds, using the monotonic clock.
	 *
	 * The monotonic clock is not subject to clock drift, even if the user
	 * changes the wall clock time. It is ideal for measuring time between
	 * two intervals.
	 *
	 * The returned time is guaranteed to have a granularity of 1 microsecond
	 * or better. In general, querying with coarser granularities is faster.
	 * If you want a coarser granularity, use `getMonotonicUsecWithGranularity()`
	 * instead.
	 *
	 * If the monotonic clock is not available (e.g. because the operating
	 * system doesn't support it), then this function returns the regular
	 * wall clock time instead (using `getUsec()`). If the monotonic clock
	 * is available, but an error occurred querying it, then a
	 * `TimeRetrievalException` is thrown.
	 *
	 * If the time was forced with forceUsed(), then the forced time is returned
	 * instead.
	 *
	 * @throws TimeRetrievalException Something went wrong while retrieving the time.
	 */
	static MonotonicTimeUsec getMonotonicUsec() {
		return _getMonotonicUsec<GRAN_1USEC>();
	}

	template<Granularity granularity>
	static MonotonicTimeUsec getMonotonicUsecWithGranularity() {
		return _getMonotonicUsec<granularity>();
	}

	/**
	 * Force get() to return the given value.
	 */
	static void force(time_t value) {
		SystemTimeData::hasForcedValue = true;
		SystemTimeData::forcedValue = value;
	}

	/**
	 * Force getUsec() to return the given value.
	 */
	static void forceUsec(unsigned long long value) {
		SystemTimeData::hasForcedUsecValue = true;
		SystemTimeData::forcedUsecValue = value;
	}

	static void forceAll(unsigned long long usec) {
		force(usec / 1000000);
		forceUsec(usec);
	}

	/**
	 * Release the previously forced seconds value, so that get()
	 * returns the system time once again.
	 */
	static void release() {
		SystemTimeData::hasForcedValue = false;
	}

	/**
	 * Release the previously forced usec value, so that getUsec()
	 * returns the system time once again.
	 */
	static void releaseUsec() {
		SystemTimeData::hasForcedUsecValue = false;
	}

	/**
	 * Release all previously forced values, so that get() and
	 * getUsec() return the system time once again.
	 */
	static void releaseAll() {
		SystemTimeData::hasForcedValue = false;
		SystemTimeData::hasForcedUsecValue = false;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TIME_H_ */
