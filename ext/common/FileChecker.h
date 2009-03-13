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
	FileChecker(const string &filename)
		: cstat(filename)
	{
		lastMtime = 0;
		lastCtime = 0;
		changed();
	}
	
	/**
	 * Checks whether the file's timestamp has changed or has been created
	 * or removed since the last call to changed().
	 *
	 * @param throttleRate When set to a non-zero value, throttling will be
	 *                     enabled. stat() will be called at most once per
	 *                     throttleRate seconds.
	 * @throws SystemException Something went wrong while retrieving the
	 *         system time. stat() errors will <em>not</em> result in SystemException
	 *         being thrown.
	 * @throws boost::thread_interrupted
	 */
	bool changed(unsigned int throttleRate = 0) {
		int ret;
		time_t ctime, mtime;
		bool result;
		
		ret = cstat.refresh(throttleRate);
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
