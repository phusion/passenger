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
 * Escape the given string into a single value for use in a shell (like Bash).
 *
 * @throws std::bad_alloc Something went wrong.
 */
string escapeShell(const StaticString &input);

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
 * A no-op, but usually set as a breakpoint in gdb. See CONTRIBUTING.md.
 */
void breakpoint();

} // namespace Passenger

#endif /* _PASSENGER_UTILS_H_ */
