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
#ifndef _PASSENGER_FILE_TOOLS_FILE_MANIP_H_
#define _PASSENGER_FILE_TOOLS_FILE_MANIP_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstddef>
#include <string>
#include <StaticString.h>

namespace boost {
	class mutex;
}

namespace Passenger {

using namespace std;

// All functions in this file allow non-NULL-terminated StaticStrings.


class CachedFileStat;

static const uid_t USER_NOT_GIVEN = (uid_t) -1;
static const gid_t GROUP_NOT_GIVEN = (gid_t) -1;

/** Enumeration which indicates what kind of file a file is. */
typedef enum {
	/** The file doesn't exist. */
	FT_NONEXISTANT,
	/** A regular file or a symlink to a regular file. */
	FT_REGULAR,
	/** A directory. */
	FT_DIRECTORY,
	/** Something else, e.g. a pipe or a socket. */
	FT_OTHER
} FileType;

/**
 * Given a filename, FileGuard will unlink the file in its destructor, unless
 * commit() was called. Used in file operation functions that don't want to
 * leave behind half-finished files after error conditions.
 */
struct FileGuard {
	string filename;
	bool committed;

	FileGuard(const StaticString &filename);
	~FileGuard();
	void commit();
};


/**
 * Check whether the specified file exists.
 *
 * @param filename The filename to check.
 * @param cstat A CachedFileStat object, if you want to use cached statting.
 * @param cstatMutex A mutex for locking cstat while this function uses it.
 *                   Makes this function thread-safe. May be NULL.
 * @param throttleRate A throttle rate for cstat. Only applicable if cstat is not NULL.
 * @return Whether the file exists.
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @throws TimeRetrievalException
 * @throws boost::thread_interrupted
 */
bool fileExists(const StaticString &filename, CachedFileStat *cstat = 0,
	boost::mutex *cstatMutex = NULL, unsigned int throttleRate = 0);

/**
* Check whether 'filename' exists and what kind of file it is.
*
* @param filename The filename to check. It MUST be NULL-terminated.
* @param cstat A CachedFileStat object, if you want to use cached statting.
* @param cstatMutex A mutex for locking cstat while this function uses it.
*                   Makes this function thread-safe. May be NULL.
* @param throttleRate A throttle rate for cstat. Only applicable if cstat is not NULL.
* @return The file type.
* @throws FileSystemException Unable to check because of a filesystem error.
* @throws TimeRetrievalException
* @throws boost::thread_interrupted
* @ingroup Support
*/
FileType getFileType(const StaticString &filename, CachedFileStat *cstat = 0,
		 boost::mutex *cstatMutex = NULL, unsigned int throttleRate = 0);

/**
* Create the given file with the given contents, permissions and ownership.
* This function does not leave behind junk files: if the ownership cannot be set
* or if not all data can be written then then the file will be deleted.
*
* @param filename The file to create.
* @param contents The contents to write to the file.
* @param permissions The desired file permissions.
* @param owner The desired file owner. Specify USER_NOT_GIVEN if you want to use the current
*              process's owner as the file owner.
* @param group The desired file group. Specify GROUP_NOT_GIVEN if you want to use the current
*              process's group as the file group.
* @param overwrite Whether to overwrite the file if it exists. If set to false
*                  and the file exists then nothing will happen.
* @throws FileSystemException Something went wrong.
*/
void createFile(const string &filename, const StaticString &contents,
	mode_t permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
	uid_t owner = USER_NOT_GIVEN, gid_t group = GROUP_NOT_GIVEN,
	bool overwrite = true,
	const char *callerFile = NULL, unsigned int callerLine = 0);

/**
 * Create the directory at the given path, creating intermediate directories
 * if necessary. The created directories' permissions are exactly as specified
 * by the 'mode' parameter (i.e. the umask will be ignored). You can specify
 * this directory's owner and group through the 'owner' and 'group' parameters.
 * A value of USER_NOT_GIVEN for 'owner' and/or GROUP_NOT_GIVEN 'group' means
 * that the owner/group should not be changed.
 *
 * If 'path' already exists, then nothing will happen.
 *
 * @param mode A mode string, as supported by parseModeString().
 * @throws FileSystemException Something went wrong.
 * @throws InvalidModeStringException The mode string cannot be parsed.
 */
void makeDirTree(const string &path, const StaticString &mode = "u=rwx,g=,o=",
	uid_t owner = USER_NOT_GIVEN, gid_t group = GROUP_NOT_GIVEN);

/**
 * Remove an entire directory tree recursively. If the directory doesn't exist then this
 * function does nothing.
 *
 * @throws RuntimeException Something went wrong.
 */
void removeDirTree(const string &path);


} // namespace Passenger

#endif /* _PASSENGER_FILE_TOOLS_FILE_MANIP_H_ */
