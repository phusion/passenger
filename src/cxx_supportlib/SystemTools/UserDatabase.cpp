/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cerrno>

#include <oxt/backtrace.hpp>

#include <Exceptions.h>
#include <SystemTools/UserDatabase.h>

namespace Passenger {

using namespace std;


OsUserOrGroup::OsUserOrGroup()
	// _SC_GETPW_R_SIZE_MAX is not a maximum:
	// http://tomlee.co/2012/10/problems-with-large-linux-unix-groups-and-getgrgid_r-getgrnam_r/
	: buffer(std::max<long>(1024 * 128, sysconf(_SC_GETPW_R_SIZE_MAX)))
{
	// Do nothing
}

OsUserOrGroup::~OsUserOrGroup() {
	// Do nothing
}


bool
lookupSystemUserByName(const StaticString &name, OsUser &result) {
	TRACE_POINT();

	// Null terminate name
	DynamicBuffer ntName(name.size() + 1);
	memcpy(ntName.data, name.data(), name.size());
	ntName.data[name.size()] = '\0';

	int code;
	struct passwd *output = NULL;

	do {
		code = getpwnam_r(ntName.data, &result.pwd, result.buffer.data,
			result.buffer.size, &output);
	} while (code == EINTR || code == EAGAIN);
	if (code == 0) {
		return output != NULL;
	} else {
		throw SystemException("Error looking up OS user account " + name, code);
	}
}

bool
lookupSystemUserByUid(uid_t uid, OsUser &result) {
	TRACE_POINT();
	int code;
	struct passwd *output = NULL;

	do {
		code = getpwuid_r(uid, &result.pwd, result.buffer.data,
			result.buffer.size, &output);
	} while (code == EINTR || code == EAGAIN);
	if (code == 0) {
		return output != NULL;
	} else {
		throw SystemException("Error looking up OS user account " + toString(uid), code);
	}
}

bool
lookupSystemGroupByName(const StaticString &name, OsGroup &result) {
	TRACE_POINT();

	// Null terminate name
	DynamicBuffer ntName(name.size() + 1);
	memcpy(ntName.data, name.data(), name.size());
	ntName.data[name.size()] = '\0';

	int code;
	struct group *output = NULL;

	do {
		code = getgrnam_r(ntName.data, &result.grp, result.buffer.data,
			result.buffer.size, &output);
	} while (code == EINTR || code == EAGAIN);
	if (code == 0) {
		return output != NULL;
	} else {
		throw SystemException("Error looking up OS group account " + name, code);
	}
}

bool
lookupSystemGroupByGid(gid_t gid, OsGroup &result) {
	TRACE_POINT();
	int code;
	struct group *output = NULL;

	do {
		code = getgrgid_r(gid, &result.grp, result.buffer.data,
			result.buffer.size, &output);
	} while (code == EINTR || code == EAGAIN);
	if (code == 0) {
		return output != NULL;
	} else {
		throw SystemException("Error looking up OS group account " + toString(gid), code);
	}
}

string
lookupSystemUsernameByUid(uid_t uid, const StaticString &fallbackFormat) {
	OsUser user;
	bool result;

	try {
		result = lookupSystemUserByUid(uid, user);
	} catch (const SystemException &) {
		result = false;
	}

	if (result && user.pwd.pw_name != NULL && user.pwd.pw_name[0] != '\0') {
		return user.pwd.pw_name;
	} else {
		// Null terminate fallback format string
		DynamicBuffer fallbackFormatNt(fallbackFormat.size() + 1);
		memcpy(fallbackFormatNt.data, fallbackFormat.data(), fallbackFormat.size());
		fallbackFormatNt.data[fallbackFormat.size()] = '\0';

		char buf[512];
		snprintf(buf, sizeof(buf), fallbackFormatNt.data, (int) uid);
		buf[sizeof(buf) - 1] = '\0';
		return buf;
	}
}

string
lookupSystemGroupnameByGid(gid_t gid, const StaticString &fallbackFormat) {
	OsGroup group;
	bool result;

	try {
		result = lookupSystemGroupByGid(gid, group);
	} catch (const SystemException &) {
		result = false;
	}

	if (result && group.grp.gr_name != NULL && group.grp.gr_name[0] != '\0') {
		return group.grp.gr_name;
	} else {
		// Null terminate fallback format string
		DynamicBuffer fallbackFormatNt(fallbackFormat.size() + 1);
		memcpy(fallbackFormatNt.data, fallbackFormat.data(), fallbackFormat.size());
		fallbackFormatNt.data[fallbackFormat.size()] = '\0';

		char buf[512];
		snprintf(buf, sizeof(buf), fallbackFormatNt.data, (int) gid);
		buf[sizeof(buf) - 1] = '\0';
		return buf;
	}
}

string
getHomeDir() {
	TRACE_POINT();
	const char *env = getenv("HOME");
	if (env != NULL && *env != '\0') {
		return env;
	}

	OsUser user;
	bool result;
	uid_t uid = getuid();
	try {
		result = lookupSystemUserByUid(uid, user);
	} catch (const SystemException &e) {
		throw SystemException("Cannot determine the home directory for user "
			+ lookupSystemUsernameByUid(uid) + ": error looking up OS user account",
			e.code());
	}
	if (result) {
		if (user.pwd.pw_dir == NULL || user.pwd.pw_dir[0] == '\0') {
			throw RuntimeException("Cannot determine the home directory for user "
				+ lookupSystemUsernameByUid(uid) + ": OS user account has no home directory defined");
		} else {
			return user.pwd.pw_dir;
		}
	} else {
		throw RuntimeException("Cannot determine the home directory for user "
			+ lookupSystemUsernameByUid(uid) + ": OS user account does not exist");
	}
}


} // namespace Passenger
