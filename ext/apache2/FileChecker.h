#include <string>

#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#include <oxt/system_calls.hpp>

namespace Passenger {

using namespace std;
using namespace oxt;

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
	FileChecker(const string &filename, unsigned int throttleRate = 0) {
		this->filename = filename;
		lastMtime = 0;
		lastCtime = 0;
		this->throttleRate = throttleRate;
		lastCheckTime = 0;
		checkChanged();
	}
	
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
