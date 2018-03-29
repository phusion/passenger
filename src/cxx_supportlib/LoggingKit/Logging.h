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
#ifndef _PASSENGER_LOGGING_KIT_LOGGING_H_
#define _PASSENGER_LOGGING_KIT_LOGGING_H_

#include <oxt/macros.hpp>
#include <LoggingKit/Forward.h>
#include <StaticString.h>
#include <Utils/FastStringStream.h>
#include <DataStructures/HashedStaticString.h>

namespace Passenger {
namespace LoggingKit {


/*
 * The P_LOG family of macros write the given expression to the log
 * output stream if the log level is sufficiently high.
 */

#define P_LOG(context, level, file, line, expr) \
	do { \
		const Passenger::LoggingKit::ConfigRealization *_configRlz; \
		if (Passenger::LoggingKit::_passesLogLevel((context), (level), &_configRlz)) { \
			Passenger::FastStringStream<> _ostream; \
			Passenger::LoggingKit::_prepareLogEntry(_ostream, (level), (file), (line)); \
			_ostream << expr << "\n"; \
			Passenger::LoggingKit::_writeLogEntry(_configRlz, _ostream.data(), _ostream.size()); \
		} \
	} while (false)

#define P_LOG_UNLIKELY(context, level, file, line, expr) \
	do { \
		const Passenger::LoggingKit::ConfigRealization *_configRlz; \
		if (OXT_UNLIKELY(Passenger::LoggingKit::_passesLogLevel((context), (level), &_configRlz))) { \
			Passenger::FastStringStream<> _ostream; \
			Passenger::LoggingKit::_prepareLogEntry(_ostream, (level), (file), (line)); \
			_ostream << expr << "\n"; \
			Passenger::LoggingKit::_writeLogEntry(_configRlz, _ostream.data(), _ostream.size()); \
		} \
	} while (false)


/*
 * P_CRITICAL, P_ERROR, P_WARN, P_NOTICE, P_INFO and P_DEBUG write
 * the given expression to the log output stream if the log level is
 * sufficiently high.
 *
 * The _WITH_POS variant of these macros allow you to specify which file
 * and line it should report as the origin of the log message.
 */

#define P_CRITICAL(expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::CRIT, __FILE__, __LINE__, expr)
#define P_CRITICAL_WITH_POS(expr, file, line) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::CRIT, file, line, expr)

#define P_ERROR(expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::ERROR, __FILE__, __LINE__, expr)
#define P_ERROR_WITH_POS(file, line, expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::ERROR, file, line, expr)

#define P_WARN(expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::WARN, __FILE__, __LINE__, expr)
#define P_WARN_WITH_POS(file, line, expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::WARN, file, line, expr)

#define P_NOTICE(expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::NOTICE, __FILE__, __LINE__, expr)
#define P_NOTICE_WITH_POS(file, line, expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::NOTICE, file, line, expr)

#define P_INFO(expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::INFO, __FILE__, __LINE__, expr)
#define P_INFO_WITH_POS(file, line, expr) P_LOG(Passenger::LoggingKit::context, \
	Passenger::LoggingKit::INFO, file, line, expr)

#define P_DEBUG(expr) P_TRACE(1, expr)
#define P_DEBUG_WITH_POS(file, line, expr) P_TRACE_WITH_POS(1, file, line, expr)


/*
 * The P_TRACE family of macros are like P_DEBUG, but allow you to set the debugging
 * level.
 *
 * Level = 1: debug
 * Level = 2: debug2
 * Level = 3: debug3
 */

#ifdef PASSENGER_DEBUG
	#define P_TRACE(level, expr) P_LOG_UNLIKELY(Passenger::LoggingKit::context, \
		Passenger::LoggingKit::Level(int(Passenger::LoggingKit::INFO) + level), \
		__FILE__, __LINE__, expr)
	#define P_TRACE_WITH_POS(level, file, line, expr) P_LOG_UNLIKELY(Passenger::LoggingKit::context, \
		Passenger::LoggingKit::Level(int(Passenger::LoggingKit::INFO) + level), \
		file, line, expr)
#else
	#define P_TRACE(level, expr) do { /* nothing */ } while (false)
	#define P_TRACE_WITH_POS(level, file, line, expr) do { /* nothing */ } while (false)
#endif


/**
 * Log the fact that a file descriptor has been opened.
 */
#define P_LOG_FILE_DESCRIPTOR_OPEN(fd) \
	P_LOG_FILE_DESCRIPTOR_OPEN3(fd, __FILE__, __LINE__)
#define P_LOG_FILE_DESCRIPTOR_OPEN2(fd, expr) \
	P_LOG_FILE_DESCRIPTOR_OPEN4(fd, __FILE__, __LINE__, expr)
#define P_LOG_FILE_DESCRIPTOR_OPEN3(fd, file, line) \
	do { \
		const Passenger::LoggingKit::ConfigRealization *_configRlz; \
		if (Passenger::LoggingKit::_shouldLogFileDescriptors(Passenger::LoggingKit::context, \
			&_configRlz)) \
		{ \
			Passenger::FastStringStream<> _ostream; \
			Passenger::LoggingKit::_prepareLogEntry(_ostream, \
				Passenger::LoggingKit::DEBUG, file, line); \
			_ostream << "File descriptor opened: " << fd << "\n"; \
			Passenger::LoggingKit::_writeFileDescriptorLogEntry(_configRlz, \
				_ostream.data(), _ostream.size()); \
		} \
	} while (false)
#define P_LOG_FILE_DESCRIPTOR_OPEN4(fd, file, line, expr) \
	do { \
		P_LOG_FILE_DESCRIPTOR_OPEN3(fd, file, line); \
		P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, expr); \
	} while (false)

/**
 * Log the purpose of a file descriptor that was recently logged with
 * P_LOG_FILE_DESCRIPTOR_OPEN(). You should include information that
 * allows a reader to find out what a file descriptor is for.
 */
#define P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, expr) \
	do { \
		const Passenger::LoggingKit::ConfigRealization *_configRlz; \
		if (Passenger::LoggingKit::_shouldLogFileDescriptors(Passenger::LoggingKit::context, \
			&_configRlz)) \
		{ \
			Passenger::FastStringStream<> _ostream; \
			Passenger::LoggingKit::_prepareLogEntry(_ostream, \
				Passenger::LoggingKit::DEBUG, __FILE__, __LINE__); \
			_ostream << "File descriptor purpose: " << fd << ": " << expr << "\n"; \
			Passenger::LoggingKit::_writeFileDescriptorLogEntry(_configRlz, \
				_ostream.data(), _ostream.size()); \
		} \
	} while (false)

/**
 * Log the fact that a file descriptor has been closed.
 */
#define P_LOG_FILE_DESCRIPTOR_CLOSE(fd) \
	do { \
		const Passenger::LoggingKit::ConfigRealization *_configRlz; \
		if (Passenger::LoggingKit::_shouldLogFileDescriptors(Passenger::LoggingKit::context, \
			&_configRlz)) \
		{ \
			Passenger::FastStringStream<> _ostream; \
			Passenger::LoggingKit::_prepareLogEntry(_ostream, \
				Passenger::LoggingKit::DEBUG, __FILE__, __LINE__); \
			_ostream << "File descriptor closed: " << fd << "\n"; \
			Passenger::LoggingKit::_writeFileDescriptorLogEntry(_configRlz, \
				_ostream.data(), _ostream.size()); \
		} \
	} while (false)


/**
 * Logs a message that was received from an application's stdout/stderr.
 *
 * @param groupName The application's Group's name.
 * @param pid The application's PID.
 * @param channelName "stdout" or "stderr".
 * @param message The message that was received.
 * @param appLogFile an app specific file to log to.
 */
void logAppOutput(const HashedStaticString &groupName, pid_t pid, const StaticString &channelName,
	const char *message, unsigned int size, const StaticString &appLogFile);

} // namespace LoggingKit
} // namespace Passenger

#endif /* _PASSENGER_LOGGING_KIT_LOGGING_H_ */
