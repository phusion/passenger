/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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

#include <sys/param.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>

#include <vector>
#include <cerrno>
#include <cstring>

#include <FileTools/PathManip.h>
#include <Exceptions.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {

using namespace std;


string
canonicalizePath(const string &path) {
	#ifdef __GLIBC__
		// We're using a GNU extension here. See the 'BUGS'
		// section of the realpath(3) Linux manpage for
		// rationale.
		char *tmp = realpath(path.c_str(), NULL);
		if (tmp == NULL) {
			int e = errno;
			string message;

			message = "Cannot resolve the path '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		} else {
			string result(tmp);
			free(tmp);
			return result;
		}
	#else
		char tmp[PATH_MAX];
		if (realpath(path.c_str(), tmp) == NULL) {
			int e = errno;
			string message;

			message = "Cannot resolve the path '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		} else {
			return tmp;
		}
	#endif
}

string
absolutizePath(const StaticString &path, const StaticString &workingDir) {
	vector<string> components;
	if (!startsWith(path, "/")) {
		if (workingDir.empty()) {
			char buffer[PATH_MAX + 1];
			if (getcwd(buffer, PATH_MAX) == NULL) {
				int e = errno;
				throw SystemException("Unable to query current working directory", e);
			}
			buffer[PATH_MAX] = '\0';
			split(buffer + 1, '/', components);
		} else {
			string absoluteWorkingDir = absolutizePath(workingDir);
			split(StaticString(absoluteWorkingDir.data() + 1, absoluteWorkingDir.size() - 1),
				'/', components);
		}
	}

	const char *begin = path.data();
	const char *end = path.data() + path.size();

	// Skip leading slashes.
	while (begin < end && *begin == '/') {
		begin++;
	}

	while (begin < end) {
		const char *next = (const char *) memchr(begin, '/', end - begin);
		if (next == NULL) {
			next = end;
		}

		StaticString component(begin, next - begin);
		if (component == "..") {
			if (!components.empty()) {
				components.pop_back();
			}
		} else if (component != ".") {
			components.push_back(component);
		}

		// Skip slashes until beginning of next path component.
		begin = next + 1;
		while (begin < end && *begin == '/') {
			begin++;
		}
	}

	string result;
	vector<string>::const_iterator c_it, c_end = components.end();
	for (c_it = components.begin(); c_it != c_end; c_it++) {
		result.append("/");
		result.append(*c_it);
	}
	if (result.empty()) {
		result = "/";
	}
	return result;
}

string
resolveSymlink(const StaticString &path) {
	string pathNt(path.data(), path.size());
	char buf[PATH_MAX];
	ssize_t size;

	size = readlink(pathNt.c_str(), buf, sizeof(buf) - 1);
	if (size == -1) {
		if (errno == EINVAL) {
			return pathNt;
		} else {
			int e = errno;
			string message = "Cannot resolve possible symlink '";
			message.append(path.data(), path.size());
			message.append("'");
			throw FileSystemException(message, e, pathNt);
		}
	} else {
		buf[size] = '\0';
		if (buf[0] == '\0') {
			string message = "The file '";
			message.append(path.data(), path.size());
			message.append("' is a symlink, and it refers to an empty filename. This is not allowed.");
			throw FileSystemException(message, ENOENT, pathNt);
		} else if (buf[0] == '/') {
			// Symlink points to an absolute path.
			return buf;
		} else {
			return extractDirNameStatic(path) + "/" + buf;
		}
	}
}

string
extractDirName(const StaticString &path) {
	DynamicBuffer pathCopy(path.size() + 1);
	memcpy(pathCopy.data, path.data(), path.size());
	pathCopy.data[path.size()] = '\0';
	return string(dirname(pathCopy.data));
}

StaticString
extractDirNameStatic(const StaticString &path) {
	if (path.empty()) {
		return StaticString(".", 1);
	}

	const char *data = path.data();
	const char *end = path.data() + path.size();

	// Ignore trailing '/' characters.
	while (end > data && end[-1] == '/') {
		end--;
	}
	if (end == data) {
		// Apparently the entire path consists of slashes.
		return StaticString("/", 1);
	}

	// Find last '/'.
	end--;
	while (end > data && *end != '/') {
		end--;
	}
	if (end == data) {
		if (*data == '/') {
			// '/' found, but it's the first character in the path.
			return StaticString("/", 1);
		} else {
			// No '/' found in path.
			return StaticString(".", 1);
		}
	} else {
		// '/' found and it's not the first character in path.
		// 'end' points to that '/' character.
		// Skip to first non-'/' character.
		while (end >= data && *end == '/') {
			end--;
		}
		if (end < data) {
			// The entire path consists of '/' characters.
			return StaticString("/", 1);
		} else {
			return StaticString(data, end - data + 1);
		}
	}
}

string
extractBaseName(const StaticString &path) {
	DynamicBuffer pathNt(path.size() + 1);
	memcpy(pathNt.data, path.data(), path.size());
	pathNt.data[path.size()] = '\0';
	string result = basename(pathNt.data);
	return result;
}


} // namespace Passenger
