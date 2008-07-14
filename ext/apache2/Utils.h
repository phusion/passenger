/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_UTILS_H_
#define _PASSENGER_UTILS_H_

#include <boost/shared_ptr.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include "Exceptions.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * Convenience shortcut for creating a <tt>shared_ptr</tt>.
 * Instead of:
 * @code
 *    shared_ptr<Foo> foo;
 *    ...
 *    foo = shared_ptr<Foo>(new Foo());
 * @endcode
 * one can write:
 * @code
 *    shared_ptr<Foo> foo;
 *    ...
 *    foo = ptr(new Foo());
 * @endcode
 *
 * @param pointer The item to put in the shared_ptr object.
 * @ingroup Support
 */
template<typename T> shared_ptr<T>
ptr(T *pointer) {
	return shared_ptr<T>(pointer);
}

/**
 * Used internally by toString(). Do not use directly.
 *
 * @internal
 */
template<typename T>
struct AnythingToString {
	string operator()(T something) {
		stringstream s;
		s << something;
		return s.str();
	}
};

/**
 * Used internally by toString(). Do not use directly.
 *
 * @internal
 */
template<>
struct AnythingToString< vector<string> > {
	string operator()(const vector<string> &v) {
		string result("[");
		vector<string>::const_iterator it;
		unsigned int i;
		for (it = v.begin(), i = 0; it != v.end(); it++, i++) {
			result.append("'");
			result.append(*it);
			if (i == v.size() - 1) {
				result.append("'");
			} else {
				result.append("', ");
			}
		}
		result.append("]");
		return result;
	}
};

/**
 * Convert anything to a string.
 *
 * @param something The thing to convert.
 * @ingroup Support
 */
template<typename T> string
toString(T something) {
	return AnythingToString<T>()(something);
}

/**
 * Converts the given string to an integer.
 * @ingroup Support
 */
int atoi(const string &s);

/**
 * Split the given string using the given separator.
 *
 * @param str The string to split.
 * @param sep The separator to use.
 * @param output The vector to write the output to.
 * @ingroup Support
 */
void split(const string &str, char sep, vector<string> &output);

/**
 * Check whether the specified file exists.
 *
 * @param filename The filename to check.
 * @return Whether the file exists.
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @ingroup Support
 */
bool fileExists(const char *filename);

/**
 * Find the location of the Passenger spawn server script.
 * If passengerRoot is given, t T
 *
 * @param passengerRoot The Passenger root folder. If NULL is given, then
 *      the spawn server is found by scanning $PATH. For security reasons,
 *      only absolute paths are scanned.
 * @return An absolute path to the spawn server script, or
 *         an empty string on error.
 * @throws FileSystemException Unable to access parts of the filesystem.
 * @ingroup Support
 */
string findSpawnServer(const char *passengerRoot = NULL);

/**
 * Find the location of the Passenger ApplicationPool server
 * executable.
 *
 * @param passengerRoot The Passenger root folder.
 * @return An absolute path to the executable.
 * @throws FileSystemException Unable to access parts of the filesystem.
 * @pre passengerRoot != NULL
 * @ingroup Support
 */
string findApplicationPoolServer(const char *passengerRoot);

/**
 * Returns a canonical version of the specified path. All symbolic links
 * and relative path elements are resolved.
 * Returns an empty string if something went wrong.
 *
 * @ingroup Support
 */
string canonicalizePath(const string &path);

/**
 * Check whether the specified directory is a valid Ruby on Rails
 * 'public' directory.
 *
 * @throws FileSystemException Unable to check because of a system error.
 * @ingroup Support
 */
bool verifyRailsDir(const string &dir);

/**
 * Check whether the specified directory is a valid Rack 'public'
 * directory.
 *
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @ingroup Support
 */
bool verifyRackDir(const string &dir);

/**
 * Check whether the specified directory is a valid WSGI 'public'
 * directory.
 *
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @ingroup Support
 */
bool verifyWSGIDir(const string &dir);

/**
 * Represents a temporary file. The associated file is automatically
 * deleted upon object destruction.
 *
 * @ingroup Support
 */
class TempFile {
public:
	/** The filename. If this temp file is anonymous, then the filename is an empty string. */
	string filename;
	/** The file handle. */
	FILE *handle;
	
	/**
	 * Create an empty, temporary file, and open it for reading and writing.
	 *
	 * @param anonymous Set to true if this temp file should be unlinked
	 *        immediately. Anonymous temp files are useful if one just wants
	 *        a big not-in-memory buffer to work with.
	 * @throws SystemException Something went wrong.
	 */
	TempFile(bool anonymous = true) {
		const char *temp_dir;
		char templ[PATH_MAX];
		int fd;
		
		temp_dir = getenv("TMP");
		if (temp_dir == NULL || *temp_dir == '\0') {
			temp_dir = "/tmp";
		}
		
		snprintf(templ, sizeof(templ), "%s/passenger.XXXXXX", temp_dir);
		templ[sizeof(templ) - 1] = '\0';
		fd = mkstemp(templ);
		if (fd == -1) {
			throw SystemException("Cannot create a temporary file", errno);
		}
		if (anonymous) {
			fchmod(fd, 0000);
			unlink(templ);
		} else {
			filename.assign(templ);
		}
		handle = fdopen(fd, "w+");
	}
	
	~TempFile() {
		fclose(handle);
		if (!filename.empty()) {
			unlink(filename.c_str());
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_UTILS_H_ */

