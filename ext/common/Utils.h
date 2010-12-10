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
#include <unistd.h>
#include "StaticString.h"
#include "Exceptions.h"

namespace Passenger {

using namespace std;
using namespace boost;

static const uid_t USER_NOT_GIVEN = (uid_t) -1;
static const gid_t GROUP_NOT_GIVEN = (gid_t) -1;

typedef struct CachedFileStat CachedFileStat;
class ResourceLocator;

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
 * Check whether the specified file exists.
 *
 * @param filename The filename to check.
 * @param cstat A CachedFileStat object, if you want to use cached statting.
 * @param throttleRate A throttle rate for cstat. Only applicable if cstat is not NULL.
 * @return Whether the file exists.
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @throws TimeRetrievalException
 * @throws boost::thread_interrupted
 * @ingroup Support
 */
bool fileExists(const StaticString &filename, CachedFileStat *cstat = 0,
                unsigned int throttleRate = 0);

/**
 * Check whether 'filename' exists and what kind of file it is.
 *
 * @param filename The filename to check.
 * @param mstat A CachedFileStat object, if you want to use cached statting.
 * @param throttleRate A throttle rate for cstat. Only applicable if cstat is not NULL.
 * @return The file type.
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @throws TimeRetrievalException
 * @throws boost::thread_interrupted
 * @ingroup Support
 */
FileType getFileType(const StaticString &filename, CachedFileStat *cstat = 0,
                     unsigned int throttleRate = 0);

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
 * @ingroup Support
 */
void createFile(const string &filename, const StaticString &contents,
                mode_t permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
                uid_t owner = USER_NOT_GIVEN, gid_t group = GROUP_NOT_GIVEN,
                bool overwrite = true);

/**
 * Returns a canonical version of the specified path. All symbolic links
 * and relative path elements are resolved.
 *
 * @throws FileSystemException Something went wrong.
 * @ingroup Support
 */
string canonicalizePath(const string &path);

/**
 * If <em>path</em> refers to a symlink, then this function resolves the
 * symlink for 1 level. That is, if the symlink points to another symlink,
 * then the other symlink will not be resolved. The resolved path is returned.
 *
 * If the symlink doesn't point to an absolute path, then this function will
 * prepend <em>path</em>'s directory to the result.
 *
 * If <em>path</em> doesn't refer to a symlink then this method will return
 * <em>path</em>.
 *
 * @throws FileSystemException Something went wrong.
 * @ingroup Support
 */
string resolveSymlink(const string &path);

/**
 * Given a path, extracts its directory name.
 *
 * @ingroup Support
 */
string extractDirName(const StaticString &path);

/**
 * Given a path, extracts its base name.
 *
 * @ingroup Support
 */
string extractBaseName(const StaticString &path);

/**
 * Escape the given raw string into an XML value.
 *
 * @throws std::bad_alloc Something went wrong.
 * @ingroup Support
 */
string escapeForXml(const string &input);

/**
 * Returns the username of the user that the current process is running as.
 * If the user has no associated username, then "UID xxxx" is returned,
 * where xxxx is the current UID.
 */
string getProcessUsername();

/**
 * Converts a mode string into a mode_t value.
 *
 * At this time only the symbolic mode strings are supported, e.g. something like looks
 * this: "u=rwx,g=w,o=rx". The grammar is as follows:
 * @code
 *   mode   ::= (clause ("," clause)*)?
 *   clause ::= who "=" permission*
 *   who    ::= "u" | "g" | "o"
 *   permission ::= "r" | "w" | "x" | "s"
 * @endcode
 *
 * Notes:
 * - The mode value starts with 0. So if you specify "u=rwx", then the group and world
 *   permissions will be empty (set to 0).
 * - The "s" permission is only allowed for who == "u" or who == "g".
 * - The return value does not depend on the umask.
 *
 * @throws InvalidModeStringException The mode string cannot be parsed.
 */
mode_t parseModeString(const StaticString &mode);

/**
 * Return the path name for the directory in which the system stores general
 * temporary files. This is usually "/tmp", but might be something else depending
 * on some environment variables.
 *
 * @ensure result != NULL
 * @ingroup Support
 */
const char *getSystemTempDir();

/* Create a temporary directory for storing Phusion Passenger instance-specific
 * temp files, such as temporarily buffered uploads, sockets for backend
 * processes, etc.
 * The directory that will be created is the one returned by
 * <tt>getPassengerTempDir(false, parentDir)</tt>. This call stores the path to
 * this temp directory in an internal variable, so that subsequent calls to
 * getPassengerTempDir() will return the same path.
 *
 * The created temp directory will have several subdirectories:
 * - webserver_private - for storing the web server's buffered uploads.
 * - info - for storing files that allow external tools to query information
 *          about a running Phusion Passenger instance.
 * - backends - for storing Unix sockets created by backend processes.
 * - master - for storing files such as the Passenger HelperServer socket.
 *
 * If a (sub)directory already exists, then it will not result in an error.
 *
 * The <em>userSwitching</em> and <em>lowestUser</em> arguments passed to
 * this method are used for determining the optimal permissions for the
 * (sub)directories. The permissions will be set as tightly as possible based
 * on the values. The <em>workerUid</em> and <em>workerGid</em> arguments
 * will be used for determining the owner of certain subdirectories.
 *
 * @note You should only call this method inside the web server's master
 *       process. In case of Apache, this is the Apache control process,
 *       the one that tends to run as root. This is because this function
 *       will set directory permissions and owners/groups, which may require
 *       root privileges.
 *
 * @param parentDir The directory under which the Phusion Passenger-specific
 *                  temp directory should be created. This argument may be the
 *                  empty string, in which case getSystemTempDir() will be used
 *                  as the parent directory.
 * @param userSwitching Whether user switching is turned on.
 * @param lowestUser The user that the spawn manager and the pool server will
 *                   run as, if user switching is turned off.
 * @param workerUid The UID that the web server's worker processes are running
 *                  as. On Apache, this is the UID that's associated with the
 *                  'User' directive.
 * @param workerGid The GID that the web server's worker processes are running
 *                  as. On Apache, this is the GID that's associated with the
 *                  'Group' directive.
 * @throws IOException Something went wrong.
 * @throws SystemException Something went wrong.
 * @throws FileSystemException Something went wrong.
 */
/* void createPassengerTempDir(const string &parentDir, bool userSwitching,
                            const string &lowestUser,
                            uid_t workerUid, gid_t workerGid); */

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
 * @throws FileSystemException Something went wrong.
 */
void removeDirTree(const string &path);

/**
 * Check whether the specified directory is a valid Ruby on Rails
 * application root directory.
 *
 * @param cstat A CachedFileStat object, if you want to use cached statting.
 * @param throttleRate A throttle rate for cstat. Only applicable if cstat is not NULL.
 * @throws FileSystemException Unable to check because of a system error.
 * @throws TimeRetrievalException
 * @throws boost::thread_interrupted
 * @ingroup Support
 */
bool verifyRailsDir(const string &dir, CachedFileStat *cstat = 0,
                    unsigned int throttleRate = 0);

/**
 * Check whether the specified directory is a valid Rack application
 * root directory.
 *
 * @param cstat A CachedFileStat object, if you want to use cached statting.
 * @param throttleRate A throttle rate for cstat. Only applicable if cstat is not NULL.
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @throws TimeRetrievalException
 * @throws boost::thread_interrupted
 * @ingroup Support
 */
bool verifyRackDir(const string &dir, CachedFileStat *cstat = 0,
                   unsigned int throttleRate = 0);

/**
 * Check whether the specified directory is a valid WSGI application
 * root directory.
 *
 * @param cstat A CachedFileStat object, if you want to use cached statting.
 * @param throttleRate A throttle rate for cstat. Only applicable if cstat is not NULL.
 * @throws FileSystemException Unable to check because of a filesystem error.
 * @throws TimeRetrievalException
 * @throws boost::thread_interrupted
 * @ingroup Support
 */
bool verifyWSGIDir(const string &dir, CachedFileStat *cstat = 0,
                   unsigned int throttleRate = 0);

void prestartWebApps(const ResourceLocator &locator, const string &serializedprestartURLs);

/**
 * Returns the system's host name.
 *
 * @throws SystemException The host name cannot be retrieved.
 */
string getHostName();

/**
 * Convert a signal number to its associated name.
 */
string getSignalName(int sig);

/**
 * Resets the current process's signal handler disposition and signal mask
 * to default values. One should call this every time one forks a child process;
 * non-default signal masks/handler dispositions can cause all kinds of weird quirks,
 * like waitpid() malfunctioning on OS X.
 *
 * This function is async-signal safe.
 */
void resetSignalHandlersAndMask();

/**
 * Close all file descriptors that are higher than <em>lastToKeepOpen</em>.
 * This function is async-signal safe. But make sure there are no other
 * threads running that might open file descriptors!
 */
void closeAllFileDescriptors(int lastToKeepOpen);


/**
 * Represents a buffered upload file.
 *
 * @ingroup Support
 */
class BufferedUpload {
public:
	/** The file handle. */
	FILE *handle;
	
	/**
	 * Create an empty upload bufer file, and open it for reading and writing.
	 *
	 * @throws SystemException Something went wrong.
	 */
	BufferedUpload(const string &dir, const char *identifier = "temp") {
		char templ[PATH_MAX];
		int fd;
		
		snprintf(templ, sizeof(templ), "%s/%s.XXXXXX", dir.c_str(), identifier);
		templ[sizeof(templ) - 1] = '\0';
		fd = mkstemp(templ);
		if (fd == -1) {
			char message[1024];
			int e = errno;
			
			snprintf(message, sizeof(message), "Cannot create a temporary file '%s'", templ);
			message[sizeof(message) - 1] = '\0';
			throw SystemException(message, e);
		}
		
		/* We use a POSIX trick here: the file's permissions are set to "u=,g=,o="
		 * and the file is deleted immediately from the filesystem, while we
		 * keep its file handle open. The result is that no other processes
		 * will be able to access this file's contents anymore, except us.
		 * We now have an anonymous disk-backed buffer.
		 */
		fchmod(fd, 0000);
		unlink(templ);
		
		handle = fdopen(fd, "w+");
	}
	
	~BufferedUpload() {
		fclose(handle);
	}
};

} // namespace Passenger

#endif /* _PASSENGER_UTILS_H_ */

