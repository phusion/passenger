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
#ifndef _PASSENGER_SYSTEM_TOOLS_USER_DATABASE_H_
#define _PASSENGER_SYSTEM_TOOLS_USER_DATABASE_H_

/*
 * Utility functions for looking up OS user and group accounts.
 * Wraps the getpwnam/getpwuid/getgrnam/getgrgid family of functions.
 * We're wrapping them because that family of functions's associated error
 * handling code are hard to get right. The utility functions in this file
 * throw exceptions with appropriate error messages.
 * Another problem is that the raw OS functions are not necessarily
 * thread-safe. The _r variants of those functions (e.g. getpwnam_r) are
 * thread-safe, but their API makes calling code riddled with boilerplate
 * that is easy to get wrong.
 *
 * In short, the utility functions in this file are easier to use, are
 * thread-safe, and are less error-prone compared to the raw OS functions.
 */

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include <string>

#include <boost/core/noncopyable.hpp>

#include <StaticString.h>
#include <StrIntTools/StrIntUtils.h>


namespace Passenger {

using namespace std;


struct OsUserOrGroup: private boost::noncopyable {
	DynamicBuffer buffer;

	OsUserOrGroup();
	virtual ~OsUserOrGroup();
};

struct OsUser: public OsUserOrGroup {
	struct passwd pwd;
};

struct OsGroup: public OsUserOrGroup {
	struct group grp;
};


/**
 * Looks up an OS user account by name, similar to getpwnam(). Puts
 * the result in `result`.
 *
 * @return True if lookup was successful, false if no user exists with the given name.
 * @throws SystemException An error occurred while looking up the user, and it's not
 *                         because the user does not exist.
 */
bool lookupSystemUserByName(const StaticString &name, OsUser &result);

/**
 * Looks up an OS user account by UID, similar to getpwuid(). Puts
 * the result in `result`.
 *
 * @return True if lookup was successful, false if no user exists with the given UID.
 * @throws SystemException An error occurred while looking up the user, and it's not
 *                         because the user does not exist.
 */
bool lookupSystemUserByUid(uid_t uid, OsUser &result);

/**
 * Looks up an OS group account by name, similar to getgrnam(). Puts
 * the result in `result`.
 *
 * @return True if lookup was successful, false if no group exists with the given name.
 * @throws SystemException An error occurred while looking up the group, and it's not
 *                         because the group does not exist.
 */
bool lookupSystemGroupByName(const StaticString &name, OsGroup &result);

/**
 * Looks up an OS group account by GID, similar to getgrgid(). Puts
 * the result in `result`.
 *
 * @return True if lookup was successful, false if no group exists with the given GID.
 * @throws SystemException An error occurred while looking up the group, and it's not
 *                         because the group does not exist.
 */
bool lookupSystemGroupByGid(gid_t gid, OsGroup &result);

/**
 * Returns the username of the OS user account with the given UID. If no such
 * account exists or if that account has no name, then returns a string that
 * is printf-style formatted according to `fallbackFormat`.
 *
 * `fallbackFormat` may contain at most one directive, which must be %d.
 * If it contains more than one directive then bad things may happen.
 */
string lookupSystemUsernameByUid(uid_t uid,
	const StaticString &fallbackFormat = P_STATIC_STRING("UID %d"));

/**
 * Returns the group name of the OS group account with the given GID. If no such
 * account exists or if that account has no name, then returns a string that
 * is printf-style formatted according to `fallbackFormat`.
 *
 * `fallbackFormat` may contain at most one directive, which must be %d.
 * If it contains more than one directive then bad things may happen.
 */
string lookupSystemGroupnameByGid(gid_t gid,
	const StaticString &fallbackFormat = P_STATIC_STRING("GID %d"));

/**
 * Returns the home directory of the current user. This queries $HOME,
 * or if that's not available, the OS user database.
 *
 * @throws SystemException
 * @throws RuntimeException
 */
string getHomeDir();


} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TOOLS_USER_DATABASE_H_ */
