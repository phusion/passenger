/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion Holding B.V.
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
#ifndef _PASSENGER_UTILS_H_
#define _PASSENGER_UTILS_H_

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/thread.hpp>
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
#include <StaticString.h>
#include <Exceptions.h>
#include <Utils/LargeFiles.h>

namespace Passenger {

using namespace std;
using namespace boost;

#define foreach         BOOST_FOREACH
#define reverse_foreach BOOST_REVERSE_FOREACH

static const uid_t USER_NOT_GIVEN = (uid_t) -1;
static const gid_t GROUP_NOT_GIVEN = (gid_t) -1;

class CachedFileStat;
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
 *    boost::shared_ptr<Foo> foo;
 *    ...
 *    foo = boost::shared_ptr<Foo>(new Foo());
 * @endcode
 * one can write:
 * @code
 *    boost::shared_ptr<Foo> foo;
 *    ...
 *    foo = ptr(new Foo());
 * @endcode
 *
 * @param pointer The item to put in the boost::shared_ptr object.
 * @ingroup Support
 */
template<typename T> boost::shared_ptr<T>
ptr(T *pointer) {
	return boost::shared_ptr<T>(pointer);
}

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
 * @ingroup Support
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
 * <em>path</em> MUST be null-terminated!
 *
 * @throws FileSystemException Something went wrong.
 * @ingroup Support
 */
string resolveSymlink(const StaticString &path);

/**
 * Given a path, extracts its directory name. 'path' does not
 * have to be NULL terminated.
 *
 * @ingroup Support
 */
string extractDirName(const StaticString &path);

/**
 * Given a path, extracts its directory name. This version does not use
 * any dynamically allocated storage and does not require `path` to be
 * NULL-terminated. It returns a StaticString that points either to static
 * storage, or to a substring of `path`.
 */
StaticString extractDirNameStatic(const StaticString &path);

/**
 * Given a path, extracts its base name.
 * <em>path</em> MUST be null-terminated!
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
string escapeForXml(const StaticString &input);

/**
 * Returns the username of the user that the current process is running as.
 * If the user has no associated username, then the behavior depends on the
 * `fallback` argument. When true, "UID xxxx" is returned, where xxxx is the
 * current UID. When false, the empty string is returned.
 */
string getProcessUsername(bool fallback = true);

/**
 * Returns either the group name for the given GID, or (if the group name
 * couldn't be looked up) a string representation of the given GID.
 */
string getGroupName(gid_t gid);

/**
 * Given a `groupName` which is either the name of a group, or a string
 * containing the GID of a group, looks up the GID as a gid_t.
 *
 * Returns `(gid_t) -1` if the lookup fails.
 */
gid_t lookupGid(const string &groupName);

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
 * Turns the given path into an absolute path. Unlike realpath(), this function does
 * not resolve symlinks.
 *
 * @throws SystemException
 */
string absolutizePath(const StaticString &path, const StaticString &workingDir = StaticString());

/**
 * Return the path name for the directory in which the system stores general
 * temporary files. This is usually "/tmp", but might be something else depending
 * on some environment variables.
 *
 * @ensure result != NULL
 * @ingroup Support
 */
const char *getSystemTempDir();

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

void prestartWebApps(const ResourceLocator &locator, const string &ruby,
	const vector<string> &prestartURLs);

/**
 * Runs the given function and catches any tracable_exceptions. Upon catching such an exception,
 * its message and backtrace will be printed. If toAbort is true then it will call abort(),
 * otherwise the exception is swallowed.
 * thread_interrupted and all other exceptions are silently propagated.
 */
void runAndPrintExceptions(const boost::function<void ()> &func, bool toAbort);
void runAndPrintExceptions(const boost::function<void ()> &func);

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
 * Disables malloc() debugging facilities on OS X.
 */
void disableMallocDebugging();

/**
 * Like system(), but properly resets the signal handler mask,
 * disables malloc debugging and closes file descriptors > 2.
 * _command_ must be null-terminated.
 */
int runShellCommand(const StaticString &command);

/**
 * Run a command and capture its stdout output.
 *
 * @param command The argument to pass to execvp();
 * @throws SystemException.
 */
string runCommandAndCaptureOutput(const char **command);

/**
 * Async-signal safe way to fork().
 *
 * On Linux, the fork() glibc wrapper grabs a ptmalloc lock, so
 * if malloc causes a segfault then we can't fork.
 * http://sourceware.org/bugzilla/show_bug.cgi?id=4737
 *
 * OS X apparently does something similar, except they use a
 * spinlock so it results in 100% CPU. See _cthread_fork_prepare()
 * at http://www.opensource.apple.com/source/Libc/Libc-166/threads.subproj/cthreads.c
 * However, since POSIX in OS X is implemented on top of a Mach layer,
 * calling asyncFork() can mess up the state of the Mach layer, causing
 * some POSIX functions to mysteriously fail. See
 * https://code.google.com/p/phusion-passenger/issues/detail?id=1094
 * You should therefore not use asyncFork() unless you're in a signal
 * handler.
 */
pid_t asyncFork();

/**
 * Close all file descriptors that are higher than <em>lastToKeepOpen</em>.
 *
 * If you set `asyncSignalSafe` to true, then this function becomes fully async-signal,
 * through the use of asyncFork() instead of fork(). However, read the documentation
 * for asyncFork() to learn about its caveats.
 *
 * Also, regardless of whether `asyncSignalSafe` is true or not, this function is not
 * *thread* safe. Make sure there are no other threads running that might open file
 * descriptors, otherwise some file descriptors might not be closed even though they
 * should be.
 */
void closeAllFileDescriptors(int lastToKeepOpen, bool asyncSignalSafe = false);

/**
 * A no-op, but usually set as a breakpoint in gdb. See CONTRIBUTING.md.
 */
void breakpoint();

} // namespace Passenger

#endif /* _PASSENGER_UTILS_H_ */

