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
#ifndef _PASSENGER_CACHED_FILE_STAT_H_
#define _PASSENGER_CACHED_FILE_STAT_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#ifdef __cplusplus

#include <errno.h>
#include <string>
#include <oxt/system_calls.hpp>

#include "SystemTime.h"

namespace Passenger {

using namespace std;
using namespace oxt;

/**
 * CachedFileStat allows one to stat() a file at a throttled rate, in order
 * to minimize stress on the filesystem. It does this by caching the old stat
 * data for a specified amount of time.
 */
class CachedFileStat {
private:
	/** The last return value of stat(). */
	int last_result;

	/** The errno set by the last stat() call. */
	int last_errno;

	/** The filename of the file to stat. */
	string filename;

	/** The last time a stat() was performed. */
	time_t last_time;
	
	/**
	 * Checks whether <em>interval</em> seconds have elapsed since <em>begin</em>
	 * The current time is returned via the <tt>currentTime</tt> argument,
	 * so that the caller doesn't have to call time() again if it needs the current
	 * time.
	 *
	 * @pre begin <= time(NULL)
	 * @return Whether <tt>interval</tt> seconds have elapsed since <tt>begin</tt>.
	 * @throws SystemException Something went wrong while retrieving the time.
	 * @throws boost::thread_interrupted
	 */
	bool expired(time_t begin, unsigned int interval, time_t &currentTime) {
		currentTime = SystemTime::get();
		return (unsigned int) (currentTime - begin) >= interval;
	}

public:
	/** The cached stat info. */
	struct stat info;
	
	/**
	 * Creates a new CachedFileStat object. The file will not be
	 * stat()ted until you call refresh().
	 *
	 * @param filename The file to stat.
	 */
	CachedFileStat(const string &filename) {
		memset(&info, 0, sizeof(struct stat));
		last_result = -1;
		last_errno = 0;
		this->filename = filename;
		last_time = 0;
	}
	
	/**
	 * Re-stat() the file, if necessary. If <tt>throttleRate</tt> seconds have
	 * passed since the last time stat() was called, then the file will be
	 * re-stat()ted.
	 *
	 * The stat information, which may either be the result of a new stat() call
	 * or just the old cached information, is be available in the <tt>info</tt>
	 * member.
	 *
	 * @return 0 if the stat() call succeeded or if no stat() was performed,
	 *         -1 if something went wrong while statting the file. In the latter
	 *         case, <tt>errno</tt> will be populated with an appropriate error code.
	 * @throws SystemException Something went wrong while retrieving the
	 *         system time. stat() errors will <em>not</em> result in SystemException
	 *         being thrown.
	 * @throws boost::thread_interrupted
	 */
	int refresh(unsigned int throttleRate) {
		time_t currentTime;
		
		if (expired(last_time, throttleRate, currentTime)) {
			last_result = syscalls::stat(filename.c_str(), &info);
			last_errno = errno;
			last_time = currentTime;
			return last_result;
		} else {
			errno = last_errno;
			return last_result;
		}
	}
};

} // namespace Passenger

#endif /* __cplusplus */


#ifdef __cplusplus
	extern "C" {
#endif

/**
 * CachedMultiFileStat allows one to stat() files at a throttled rate, in order
 * to minimize stress on the filesystem. It does this by caching the old stat
 * data for a specified amount of time.
 *
 * Unlike CachedFileStat, which can only stat() one specific file per
 * CachedFileStat object, CachedMultiFileStat can stat() any file. The
 * number of cached stat() information is limited by the given cache size.
 *
 * This class is fully thread-safe.
 */
typedef struct CachedMultiFileStat CachedMultiFileStat;

CachedMultiFileStat *cached_multi_file_stat_new(unsigned int max_size);
void cached_multi_file_stat_free(CachedMultiFileStat *mstat);
int  cached_multi_file_stat_perform(CachedMultiFileStat *mstat,
                                    const char *filename,
                                    struct stat *buf,
                                    unsigned int throttle_rate);

#ifdef __cplusplus
	}
#endif

#endif /* _PASSENGER_CACHED_FILE_STAT_H_ */

