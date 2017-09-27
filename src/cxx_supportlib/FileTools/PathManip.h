/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_FILE_TOOLS_PATH_MANIP_H_
#define _PASSENGER_FILE_TOOLS_PATH_MANIP_H_

#include <string>
#include <StaticString.h>

namespace Passenger {

using namespace std;

// All functions in this file allow non-NULL-terminated StaticStrings.


/**
 * Returns a canonical version of the specified path. All symbolic links
 * and relative path elements are resolved.
 *
 * @throws FileSystemException Something went wrong.
 */
string canonicalizePath(const string &path);

/**
 * Turns the given path into an absolute path. Unlike realpath(), this function does
 * not resolve symlinks.
 *
 * @throws SystemException
 */
string absolutizePath(const StaticString &path, const StaticString &workingDir = StaticString());

/**
 * If `path` refers to a symlink, then this function resolves the
 * symlink for 1 level. That is, if the symlink points to another symlink,
 * then the other symlink will not be resolved. The resolved path is returned.
 *
 * If the symlink doesn't point to an absolute path, then this function will
 * prepend `path`'s directory to the result.
 *
 * If `path` doesn't refer to a symlink then this method will return
 * `path<`.
 *
 * @throws FileSystemException Something went wrong.
 */
 string resolveSymlink(const StaticString &path);

/**
 * Given a path, extracts its directory name.
 */
string extractDirName(const StaticString &path);

/**
 * Given a path, extracts its directory name. This version does not use
 * any dynamically allocated storage.
 * It returns a StaticString that points either to
 * an immutable constant string, or to a substring of `path`.
 */
StaticString extractDirNameStatic(const StaticString &path);

/**
 * Given a path, extracts its base name.
 */
string extractBaseName(const StaticString &path);


} // namespace Passenger

#endif /* _PASSENGER_FILE_TOOLS_PATH_MANIP_H_ */
