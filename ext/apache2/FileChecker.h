/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
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
#include <string>

#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#include <oxt/system_calls.hpp>

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
	string filename;
	time_t lastMtime;
	time_t lastCtime;
	unsigned int throttleRate;
	time_t lastCheckTime;
	
	bool checkChanged() {
		struct stat buf;
		int ret;
		bool result;
		
		do {
			ret = stat(filename.c_str(), &buf);
		} while (ret == -1 && errno == EINTR);
		
		if (ret == -1) {
			buf.st_mtime = 0;
			buf.st_ctime = 0;
		}
		result = lastMtime != buf.st_mtime || lastCtime != buf.st_ctime;
		lastMtime = buf.st_mtime;
		lastCtime = buf.st_ctime;
		return result;
	}
	
	bool expired(time_t begin, unsigned int interval, time_t &currentTime) const {
		currentTime = syscalls::time(NULL);
		return (unsigned int) (currentTime - begin) >= interval;
	}
	
public:
	/**
	 * Create a FileChecker object.
	 *
	 * @param filename The filename to check for.
	 * @param throttleRate When set to a non-zero value, throttling will be
	 *                     enabled. stat() will be called at most once per
	 *                     throttleRate seconds.
	 */
	FileChecker(const string &filename, unsigned int throttleRate = 0) {
		this->filename = filename;
		lastMtime = 0;
		lastCtime = 0;
		this->throttleRate = throttleRate;
		lastCheckTime = 0;
		checkChanged();
	}
	
	/**
	 * Checks whether the file's timestamp has changed or has been created
	 * or removed since the last call to changed().
	 */
	bool changed() {
		if (throttleRate > 0) {
			time_t currentTime;
			if (expired(lastCheckTime, throttleRate, currentTime)) {
				lastCheckTime = currentTime;
				return checkChanged();
			} else {
				return false;
			}
		} else {
			return checkChanged();
		}
	}
};

} // namespace Passenger
