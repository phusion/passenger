/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2009  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
