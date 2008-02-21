#ifndef _PASSENGER_UTILS_H_
#define _PASSENGER_UTILS_H_

#include <boost/shared_ptr.hpp>
#include <string>
#include <vector>
#include <ostream>
#include <sstream>

#include <sys/types.h>
#include <unistd.h>
#include <cstring>

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
 */
template<typename T> shared_ptr<T>
ptr(T *pointer) {
	return shared_ptr<T>(pointer);
}

/**
 * Convert anything to a string.
 *
 * @param something The thing to convert.
 */
template<typename T> string
toString(T something) {
	stringstream s;
	s << something;
	return s.str();
}

/**
 * Converts the given string to an integer.
 */
int atoi(const string &s);

/**
 * Split the given string using the given separator.
 *
 * @param str The string to split.
 * @param sep The separator to use.
 * @param output The vector to write the output to.
 */
void split(const string &str, char sep, vector<string> &output);

/**
 * Check whether the specified file exists.
 *
 * @param filename The filename to check.
 * @return Whether the file exists.
 */
bool fileExists(const char *filename);

/**
 * Find the location of the Passenger spawn server script.
 * This is done by scanning $PATH. For security reasons, only
 * absolute paths are scanned.
 *
 * @return An absolute path to the spawn server script, or
 *         an empty string on error.
 */
string findSpawnServer();


#ifdef PASSENGER_DEBUG
	#define P_DEBUG(expr) \
		do { \
			if (Passenger::_debugStream != 0) { \
				*Passenger::_debugStream << \
					"[" << getpid() << ":" << __FILE__ << ":" << __LINE__ << "] " << \
					expr << std::endl; \
			} \
		} while (false)
	#define P_WARN P_DEBUG
	#define P_ERROR P_DEBUG
	#define P_TRACE P_DEBUG
#else
	#define P_DEBUG(expr) do { /* nothing */ } while (false)
	#define P_WARN P_DEBUG
	#define P_ERROR P_DEBUG
	#define P_TRACE P_DEBUG
#endif

void initDebugging(const char *logFile = NULL);

// Internal; do not use directly.
extern ostream *_debugStream;

} // namespace Passenger

#endif /* _PASSENGER_UTILS_H_ */

