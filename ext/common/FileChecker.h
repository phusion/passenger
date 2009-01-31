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
#ifndef _PASSENGER_FILE_CHECKER_H_
#define _PASSENGER_FILE_CHECKER_H_

#include <string>

#include <oxt/system_calls.hpp>

#include <sys/stat.h>
#include <errno.h>

#include "CachedFileStat.h"
#include "SystemTime.h"

namespace Passenger {

using namespace std;
using namespace oxt;

/**
 * Utility class for checking for file changes. Example:
 *
 * @code
 * FileChecker checker("foo.txt");
 * checker.changed();   // false
 * writeToFile("foo.txt");
 * checker.changed();   // true
 * checker.changed();   // false
 * @endcode
 *
 * FileChecker uses stat() to retrieve file information. FileChecker also
 * supports throttling in order to limit the number of stat() calls. This
 * can improve performance on systems where disk I/O is a problem.
 */
class FileChecker {
private:
	CachedFileStat cstat;
	time_t lastMtime;
	time_t lastCtime;
	
public:
	/**
	 * Create a FileChecker object.
	 *
	 * @param filename The filename to check for.
	 */
	FileChecker(const string &filename) {
		cached_file_stat_init(&cstat, filename.c_str());
		lastMtime = 0;
		lastCtime = 0;
		changed();
	}
	
	~FileChecker() {
		cached_file_stat_deinit(&cstat);
	}
	
	/**
	 * Checks whether the file's timestamp has changed or has been created
	 * or removed since the last call to changed().
	 *
	 * @param throttleRate When set to a non-zero value, throttling will be
	 *                     enabled. stat() will be called at most once per
	 *                     throttleRate seconds.
	 * @throws SystemException Something went wrong.
	 * @throws boost::thread_interrupted
	 */
	bool changed(unsigned int throttleRate = 0) {
		int ret;
		time_t ctime, mtime;
		bool result;
		
		do {
			ret = cached_file_stat_refresh(&cstat,
				throttleRate);
		} while (ret == -1 && errno == EINTR);
		
		if (ret == -1) {
			ctime = 0;
			mtime = 0;
		} else {
			ctime = cstat.info.st_ctime;
			mtime = cstat.info.st_mtime;
		}
		result = lastMtime != mtime || lastCtime != ctime;
		lastMtime = mtime;
		lastCtime = ctime;
		return result;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_FILE_CHECKER_H_ */
