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
