/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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
#include "Exceptions.h"

namespace Passenger {

using namespace oxt;

namespace SystemTimeData {
	extern bool hasForcedValue;
	extern time_t forcedValue;
}

/**
 * This class allows one to obtain the system time, similar to time(). Unlike
 * time(), it is possible to force a certain time to be returned, which is
 * useful for testing code that depends on the system time.
 */
class SystemTime {
public:
	/**
	 * Returns the time since the Epoch, measured in seconds. Or, if a time
	 * was forced, then the forced time is returned instead.
	 *
	 * @throws SystemException Something went wrong while retrieving the time.
	 * @throws boost::thread_interrupted
	 */
	static time_t get() {
		if (SystemTimeData::hasForcedValue) {
			return SystemTimeData::forcedValue;
		} else {
			time_t ret = syscalls::time(NULL);
			if (ret == -1) {
				throw SystemException("Unable to retrieve the system time",
					errno);
			}
			return ret;
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
	 * Release the previously forced value, so that get()
	 * returns the system time once again.
	 */
	static void release() {
		SystemTimeData::hasForcedValue = false;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TIME_H_ */
