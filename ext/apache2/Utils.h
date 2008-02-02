#ifndef _PASSENGER_UTILS_H_
#define _PASSENGER_UTILS_H_

#include <boost/shared_ptr.hpp>
#include <ostream>
#include <sstream>

#include <sys/types.h>
#include <unistd.h>

namespace Passenger {

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

template<typename T> boost::shared_ptr<T>
ptr(T *pointer) {
	return boost::shared_ptr<T>(pointer);
}

template<typename T> std::string
toString(T something) {
	std::stringstream s;
	s << something;
	return s.str();
}

// Internal; do not use directly.
extern std::ostream *_debugStream;

} // namespace Passenger

#endif /* _PASSENGER_UTILS_H_ */
