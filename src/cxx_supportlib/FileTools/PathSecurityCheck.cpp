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

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

#include <FileTools/PathSecurityCheck.h>
#include <FileTools/PathManip.h>
#include <SystemTools/UserDatabase.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {


static bool
isSinglePathProbablySecureForRootUse(const string &path,
	vector<string> &errors, vector<string> &checkErrors)
{
	struct stat s;
	int ret;

	do {
		ret = stat(path.c_str(), &s);
	} while (ret == -1 && errno == EAGAIN);
	if (ret == -1) {
		int e = errno;
		checkErrors.push_back("Security check skipped on " + path
			+ ": stat() failed: " + strerror(e) + " (errno="
			+ toString(e) + ")");
		return true;
	}

	if (s.st_uid != 0) {
		errors.push_back(path + " is not secure: it can be modified by user "
			+ lookupSystemUsernameByUid(s.st_uid));
		return false;
	}

	if (s.st_mode & S_ISVTX) {
		return true;
	}

	if (s.st_mode & S_IWGRP) {
		errors.push_back(path + " is not secure: it can be modified by group "
			+ lookupSystemGroupnameByGid(s.st_gid));
		return false;
	}

	if (s.st_mode & S_IWOTH) {
		errors.push_back(path + " is not secure: it can be modified by anybody");
		return false;
	}

	return true;
}

bool
isPathProbablySecureForRootUse(const StaticString &path, vector<string> &errors,
	vector<string> &checkErrors)
{
	string fullPath = absolutizePath(path);
	bool result = true;

	while (!fullPath.empty() && fullPath != "/") {
		result = isSinglePathProbablySecureForRootUse(fullPath, errors, checkErrors)
			&& result;
		fullPath = extractDirName(fullPath);
	}

	return result;
}


} // namespace Passenger
