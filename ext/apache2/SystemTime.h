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

#include <time.h>

#ifdef __cplusplus
	extern "C" {
#endif

/**
 * This file provides a function for obtaining the system time, similar to
 * time(). Unlike time(), it is possible to force a certain time to be
 * returned, which is useful for testing code that depends on the system time.
 */

/**
 * Returns the time since the Epoch, measured in seconds. Or, if a time was
 * forced, then the forced time is returned instead.
 *
 * On error, <tt>(time_t) -1</tt> is returned, and <tt>errno</tt> is set
 * appropriately.
 */
time_t passenger_system_time_get();

/**
 * Force passenger_system_time_get() to return the given value.
 */
void passenger_system_time_force_value(time_t value);

/**
 * Release the previously forced value, so that passenger_system_time_get()
 * returns the system time once again.
 */
void passenger_system_time_release_forced_value();

#ifdef __cplusplus
	}
#endif

#ifdef __cplusplus
	#include <boost/thread.hpp>
	#include <oxt/system_calls.hpp>
	#include "Exceptions.h"
	
	namespace PhusionPassenger {
	
	using namespace boost;
	
	class SystemTime {
	public:
		/**
		 * A C++ wrapper around passenger_system_time_get(). It
		 * throws a SystemException if an error occurred, and respects
		 * boost::this_thread::syscalls_interruptable().
		 *
		 * @throws SystemException
		 * @throws boost::thread_interrupted
		 */
		static time_t get() {
			int e;
			time_t ret;
			
			do {
				ret = passenger_system_time_get();
				e = errno;
			} while (ret == (time_t) -1 && e == EINTR
				&& !this_thread::syscalls_interruptable());
			if (ret == (time_t) -1 && e == EINTR && this_thread::syscalls_interruptable()) {
				throw boost::thread_interrupted();
			}
			errno = e;
			
			if (ret == (time_t) -1) {
				throw SystemException("Unable to retrieve the system time", e);
			}
			
			return ret;
		}
	};
	
	} // namespace PhusionPassenger
#endif

#endif /* _PASSENGER_SYSTEM_TIME_H_ */
