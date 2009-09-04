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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "AccountsDatabase.h"
#include "MessageServer.h"
#include "Utils.h"

namespace Passenger {

AccountsDatabasePtr
AccountsDatabase::createDefault() {
	AccountsDatabasePtr database(new AccountsDatabase());
	string infoDir;
	struct stat buf;
	int ret;
	
	infoDir = getPassengerTempDir() + "/info";
	do {
		ret = stat(infoDir.c_str(), &buf);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		int e = errno;
		throw FileSystemException("Cannot stat " + infoDir, e, infoDir);
	}
	
	char passengerStatusPasswordBuf[MessageServer::MAX_PASSWORD_SIZE];
	generateSecureToken(passengerStatusPasswordBuf, sizeof(passengerStatusPasswordBuf));
	string passengerStatusPassword(passengerStatusPasswordBuf, sizeof(passengerStatusPasswordBuf));
	database->add("_passenger-status", passengerStatusPassword, false,
		Account::INSPECT_BASIC_INFO | Account::INSPECT_BACKTRACES);
	createFile(infoDir + "/passenger-status-password.txt",
		passengerStatusPassword, S_IRUSR, buf.st_uid, buf.st_gid);
	
	return database;
}

} // namespace Passenger
