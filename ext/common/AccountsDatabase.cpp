/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include "AccountsDatabase.h"
#include "RandomGenerator.h"
#include "Exceptions.h"
#include "Constants.h"
#include "Utils.h"

namespace Passenger {

AccountsDatabasePtr
AccountsDatabase::createDefault(const ServerInstanceDir::GenerationPtr &generation,
                                bool userSwitching, const string &defaultUser,
                                const string &defaultGroup)
{
	AccountsDatabasePtr database(new AccountsDatabase());
	struct passwd *defaultUserEntry;
	struct group  *defaultGroupEntry;
	uid_t defaultUid;
	gid_t defaultGid;
	RandomGenerator random;
	string passengerStatusPassword = random.generateByteString(MESSAGE_SERVER_MAX_PASSWORD_SIZE);
	
	defaultUserEntry = getpwnam(defaultUser.c_str());
	if (defaultUserEntry == NULL) {
		throw NonExistentUserException("Default user '" + defaultUser +
			"' does not exist.");
	}
	defaultUid = defaultUserEntry->pw_uid;
	defaultGroupEntry = getgrnam(defaultGroup.c_str());
	if (defaultGroupEntry == NULL) {
		throw NonExistentGroupException("Default group '" + defaultGroup +
			"' does not exist.");
	}
	defaultGid = defaultGroupEntry->gr_gid;
	
	// An account for the 'passenger-status' command. Its password is only readable by
	// root, or (if user switching is turned off) only by the web server's user.
	database->add("_passenger-status", passengerStatusPassword, false,
		Account::INSPECT_BASIC_INFO | Account::INSPECT_SENSITIVE_INFO |
		Account::INSPECT_BACKTRACES);
	if (geteuid() == 0 && !userSwitching) {
		createFile(generation->getPath() + "/passenger-status-password.txt",
			passengerStatusPassword, S_IRUSR, defaultUid, defaultGid);
	} else {
		createFile(generation->getPath() + "/passenger-status-password.txt",
			passengerStatusPassword, S_IRUSR | S_IWUSR);
	}
	
	return database;
}

} // namespace Passenger
