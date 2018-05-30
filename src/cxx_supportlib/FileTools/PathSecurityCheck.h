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
#ifndef _PASSENGER_FILE_TOOLS_PATH_SECURITY_CHECK_H_
#define _PASSENGER_FILE_TOOLS_PATH_SECURITY_CHECK_H_

#include <vector>
#include <string>
#include <StaticString.h>

namespace Passenger {

using namespace std;


/**
 * Checks whether the given path is secure for use by a root process.
 * This is done by checking whether the path itself, as well as any of the
 * parent directories, can only be written to by root. Returns whether the
 * path is deemed secure.
 *
 * If a non-root user can write to any of the directories in the path then that
 * user can cause the root proces to read an arbitrary file. That file can even
 * be one that is not owned by said user, through the use of symlinks.
 *
 * Checking is done according to normal Unix permissions. ACLs and systems like
 * SELinux are not taken into consideration. Also, if this function fails to
 * check a part of the path (e.g. because stat() failed) then this function
 * simply skips that part. Therefore this function does not perform a full check
 * and its result (which *can* be a false positive or a false negative) should be
 * taken with a grain of salt.
 *
 * Error messages that can be used to inform the user which parts of the path
 * are insecure, are outputted into `errors`. This vector becomes non-empty
 * only if result is false.
 *
 * Any errors that occur w.r.t. checking itself (e.g. stat() errors) are
 * outputted into `checkErrors`. This vector may become non-empty no matter
 * the result.
 */
bool isPathProbablySecureForRootUse(const StaticString &path,
	vector<string> &errors, vector<string> &checkErrors);


} // namespace Passenger

#endif /* _PASSENGER_FILE_TOOLS_PATH_SECURITY_CHECK_H_ */
