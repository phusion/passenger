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

template<typename T> shared_ptr<T>
ptr(T *pointer) {
	return shared_ptr<T>(pointer);
}

template<typename T> string
toString(T something) {
	stringstream s;
	s << something;
	return s.str();
}

void split(const string &str, char sep, vector<string> &output);


#ifdef PASSENGER_DEBUG
	#define P_DEBUG(expr) \
		do { \
			if (Passenger::_debugStream != 0) { \
				*Passenger::_debugStream << \
					"[" << getpid() << ":" << __FILE__ << ":" << __LINE__ << "] " << \
					expr << std::endl; \
			} \
		} while (false)
	#define P_TRACE P_DEBUG
#else
	#define P_DEBUG(expr) do { /* nothing */ } while (false)
	#define P_TRACE P_DEBUG
#endif

void initDebugging(const char *logFile = NULL);

// Internal; do not use directly.
extern ostream *_debugStream;

} // namespace Passenger

#endif /* _PASSENGER_UTILS_H_ */

