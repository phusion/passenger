/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  See LICENSE file for license information.
 */
#ifndef _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_
#define _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_

#include <unistd.h>
#include <boost/predef.h>
#include <FileTools/FileManip.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {

using namespace std;


// adapted from https://github.com/systemd/systemd/blob/042ca/src/basic/virt.c
// separate function to keep as similar to source as possible for easier updates
inline bool
_linuxAutoDetectInContainer() {
	// https://github.com/moby/moby/issues/26102#issuecomment-253621560
	if (fileExists("/.dockerenv")) {
		return true;
	}

	if (fileExists("/proc/vz") && !fileExists("/proc/bc")) {
		return true;
	}

	const char *env = getenv("container");
	if (env != NULL) {
		return (*env != '\0');
	}

	if (fileExists("/run/systemd/container")) {
		string file = unsafeReadFile("/run/systemd/container");
		return (file.length() > 0);
	}

	if (geteuid() == 0) {
		if (fileExists("/proc/1/environ")) {
			string file = unsafeReadFile("/proc/1/environ");
			if (file.size() > 0) {
				vector<string> v;
				split(file,'\0', v);
				for(vector<string>::iterator it = v.begin(); it != v.end(); ++it) {
					if (startsWith(*it, "container=")) {
						return true;
					}
				}
			}
		}

		if (fileExists("/proc/1/sched")) {
			string file = unsafeReadFile("/proc/1/sched");
			if (file.length() > 0) {
				const char t = file[0];
				if (t == '\0') {
					return false;
				}

				if (!startsWith(file, "(1,")) {
					return true;
				}
			}
		}
	}
	return false;
}

inline bool
autoDetectInContainer() {
	#if BOOST_OS_LINUX
		return _linuxAutoDetectInContainer();
	#else
		return false;
	#endif
}


} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TOOLS_CONTAINER_HELPERS_H_ */
