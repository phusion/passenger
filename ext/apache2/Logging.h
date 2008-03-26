#ifndef _PASSENGER_LOGGING_H_
#define _PASSENGER_LOGGING_H_

#include <sys/types.h>
#include <unistd.h>
#include <ostream>

namespace Passenger {

using namespace std;

extern int _debugLevel;
extern ostream *_logStream;
extern ostream *_debugStream;

void setDebugFile(const char *logFile = NULL);

/**
 * Write the given expression to the log stream.
 */
#define P_LOG(expr) \
	do { \
		if (Passenger::_logStream != 0) { \
			*Passenger::_logStream << \
				"[" << getpid() << ":" << __FILE__ << ":" << __LINE__ << "] " << \
				expr << std::endl; \
		} \
	} while (false)

/**
 * Write the given expression, which represents a warning,
 * to the log stream.
 */
#define P_WARN(expr) P_LOG(expr)

/**
 * Write the given expression, which represents an error,
 * to the log stream.
 */
#define P_ERROR(expr) P_LOG(expr)

/**
 * Write the given expression, which represents a debugging message,
 * to the log stream.
 */
#define P_DEBUG(expr) P_TRACE(1, expr)

#ifdef PASSENGER_DEBUG
	#define P_TRACE(level, expr) \
		do { \
			if (Passenger::_debugLevel >= level) { \
				if (Passenger::_debugStream != 0) { \
					*Passenger::_debugStream << \
						"[" << getpid() << ":" << __FILE__ << ":" << __LINE__ << "] " << \
						expr << std::endl; \
				} \
			} \
		} while (false)
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
#endif

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_H_ */

