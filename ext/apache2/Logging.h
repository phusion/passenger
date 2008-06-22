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
#ifndef _PASSENGER_LOGGING_H_
#define _PASSENGER_LOGGING_H_

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <ostream>
#include <ctime>

namespace Passenger {

using namespace std;

extern unsigned int _logLevel;
extern ostream *_logStream;
extern ostream *_debugStream;

unsigned int getLogLevel();
void setLogLevel(unsigned int value);
void setDebugFile(const char *logFile = NULL);

/**
 * Write the given expression to the log stream.
 */
#define P_LOG(expr) \
	do { \
		if (Passenger::_logStream != 0) { \
			time_t the_time = time(NULL); \
			struct tm *the_tm = localtime(&the_time); \
			char datetime_buf[60]; \
			struct timeval tv; \
			strftime(datetime_buf, sizeof(datetime_buf), "%F %H:%M:%S", the_tm); \
			gettimeofday(&tv, NULL); \
			*Passenger::_logStream << \
				"[ pid=" << getpid() << " file=" << __FILE__ << ":" << __LINE__ << \
				" time=" << datetime_buf << "." << (tv.tv_usec / 1000) << " ]:" << \
				"\n  " << expr << std::endl; \
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
			if (Passenger::_logLevel >= level) { \
				if (Passenger::_debugStream != 0) { \
					time_t the_time = time(NULL); \
					struct tm *the_tm = localtime(&the_time); \
					char datetime_buf[60]; \
					struct timeval tv; \
					strftime(datetime_buf, sizeof(datetime_buf), "%F %H:%M:%S", the_tm); \
					gettimeofday(&tv, NULL); \
					*Passenger::_debugStream << \
						"[ pid=" << getpid() << " file=" << __FILE__ << ":" << __LINE__ << \
						" time=" << datetime_buf << "." << (tv.tv_usec / 1000) << " ]:" << \
						"\n  " << expr << std::endl; \
				} \
			} \
		} while (false)
	
	#define P_ASSERT(expr, result_if_failed, message) \
		do { \
			if (!(expr)) { \
				P_ERROR("Assertion failed: " << message); \
				return result_if_failed; \
			} \
		} while (false)
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
	
	#define P_ASSERT(expr, result_if_failed, message) do { /* nothing */ } while (false)
#endif

} // namespace Passenger

#endif /* _PASSENGER_LOGGING_H_ */

