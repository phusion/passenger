/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
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
#ifndef _PASSENGER_SYSTEM_TIME_H_
#define _PASSENGER_SYSTEM_TIME_H_

#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include "../Exceptions.h"

namespace Passenger {

using namespace oxt;

namespace SystemTimeData {
	extern bool hasForcedValue;
	extern time_t forcedValue;
	extern bool hasForcedMsecValue;
	extern unsigned long long forcedMsecValue;
	extern bool hasForcedUsecValue;
	extern unsigned long long forcedUsecValue;
}

/**
 * This class allows one to obtain the system time, similar to time() and
 * gettimeofday(). Unlike time(), it is possible to force a certain time
 * to be returned, which is useful for testing code that depends on the
 * system time.
 *
 * get() provides seconds resolution while getMsec() provides milliseconds
 * resolution. Both clocks can be independently forced to a certain value
 * through force() and forceMsec().
 */
class SystemTime {
public:
	/**
	 * Returns the time since the Epoch, measured in seconds. Or, if a time
	 * was forced with force(), then the forced time is returned instead.
	 *
	 * @throws TimeRetrievalException Something went wrong while retrieving the time.
	 * @throws boost::thread_interrupted
	 */
	static time_t get() {
		if (SystemTimeData::hasForcedValue) {
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
	 * Returns the time since the Epoch, measured in milliseconds. Or, if a
	 * time was forced with forceMsec(), then the forced time is returned instead.
	 *
	 * @param real Whether to get the real time, even if a value was forced.
	 * @throws TimeRetrievalException Something went wrong while retrieving the time.
	 * @throws boost::thread_interrupted
	 */
	static unsigned long long getMsec(bool real = false) {
		if (SystemTimeData::hasForcedMsecValue && !real) {
			return SystemTimeData::forcedMsecValue;
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
			return (unsigned long long) t.tv_sec * 1000 + t.tv_usec / 1000;
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
		if (SystemTimeData::hasForcedUsecValue) {
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
	 * Force get() to return the given value.
	 */
	static void force(time_t value) {
		SystemTimeData::hasForcedValue = true;
		SystemTimeData::forcedValue = value;
	}

	/**
	 * Force getMsec() to return the given value.
	 */
	static void forceMsec(unsigned long long value) {
		SystemTimeData::hasForcedMsecValue = true;
		SystemTimeData::forcedMsecValue = value;
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
		forceMsec(usec / 1000);
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
	 * Release the previously forced msec value, so that getMsec()
	 * returns the system time once again.
	 */
	static void releaseMsec() {
		SystemTimeData::hasForcedMsecValue = false;
	}

	/**
	 * Release the previously forced usec value, so that getUsec()
	 * returns the system time once again.
	 */
	static void releaseUsec() {
		SystemTimeData::hasForcedUsecValue = false;
	}

	/**
	 * Release all previously forced values, so that get(), getMsec()
	 * and getUsec() return the system time once again.
	 */
	static void releaseAll() {
		SystemTimeData::hasForcedValue = false;
		SystemTimeData::hasForcedMsecValue = false;
		SystemTimeData::hasForcedUsecValue = false;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TIME_H_ */
