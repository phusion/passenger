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

class ResourceLocator;

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
 * Return the path name for the directory in which the system stores general
 * temporary files. This is usually "/tmp", but might be something else depending
 * on some environment variables.
 *
 * @ensure result != NULL
 * @ingroup Support
 */
const char *getSystemTempDir();

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
 * Run a command and capture its stdout output. This function
 * does not care whether the command fails.
 *
 * @param command The argument to pass to execvp().
 * @param status The status of the child process will be stored here, if non-NULL.
 *               When unable to waitpid() the child process because of an ECHILD
 *               or ESRCH, this will be set to -1.
 * @throws SystemException
 */
string runCommandAndCaptureOutput(const char **command, int *status = NULL);

/**
 * Run a Passenger-internal Ruby tool, e.g. passenger-config, and capture its stdout
 * output. This function does not care whether the command fails.
 *
 * @param resourceLocator
 * @param ruby The Ruby interpreter to attempt to use for running the tool.
 * @param args The command as an array of strings, e.g. ["passenger-config", "system-properties"].
 * @param status The status of the child process will be stored here, if non-NULL.
 *               When unable to waitpid() the child process because of an ECHILD
 *               or ESRCH, this will be set to -1.
 * @throws RuntimeException
 * @throws SystemException
 */
string runInternalRubyTool(const ResourceLocator &resourceLocator,
	const string &ruby, const vector<string> &args, int *status = NULL);

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

