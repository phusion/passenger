/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
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
#include <iomanip>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <Logging.h>
#include <StaticString.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>

namespace Passenger {

int _logLevel = 0;
int _logOutput = STDERR_FILENO;
static bool printAppOutputAsDebuggingMessages = false;

int
getLogLevel() {
	return _logLevel;
}

void
setLogLevel(int value) {
	_logLevel = value;
}

bool
setDebugFile(const char *logFile) {
	int fd = open(logFile, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd != -1) {
		_logOutput = fd;
		return true;
	} else {
		return false;
	}
}

void
_prepareLogEntry(std::stringstream &sstream, const char *file, unsigned int line) {
	time_t the_time;
	struct tm the_tm;
	char datetime_buf[60];
	struct timeval tv;

	if (startsWith(file, "ext/")) {
		file += sizeof("ext/") - 1;
		if (startsWith(file, "common/")) {
			file += sizeof("common/") - 1;
			if (startsWith(file, "ApplicationPool2/")) {
				file += sizeof("Application") - 1;
			}
		}
	}

	the_time = time(NULL);
	localtime_r(&the_time, &the_tm);
	strftime(datetime_buf, sizeof(datetime_buf) - 1, "%F %H:%M:%S", &the_tm);
	gettimeofday(&tv, NULL);
	sstream <<
		"[ " << datetime_buf << "." << std::setfill('0') << std::setw(4) <<
			(unsigned long) (tv.tv_usec / 100) <<
		" " << std::dec << getpid() << "/" <<
			std::hex << pthread_self() << std::dec <<
		" " << file << ":" << line <<
		" ]: ";
}

static void
_writeLogEntry(const StaticString &str) {
	try {
		writeExact(_logOutput, str.data(), str.size());
	} catch (const SystemException &) {
		/* The most likely reason why this fails is when the user has setup
		 * Apache to log to a pipe (e.g. to a log rotation script). Upon
		 * restarting the web server, the process that reads from the pipe
		 * shuts down, so we can't write to it anymore. That's why we
		 * just ignore write errors. It doesn't make sense to abort for
		 * something like this.
		 */
	}
}

void
_writeLogEntry(const std::string &str) {
	_writeLogEntry(StaticString(str));
}

static void
realPrintAppOutput(char *buf, unsigned int bufSize,
	const char *pidStr, unsigned int pidStrLen,
	const char *channelName, unsigned int channelNameLen,
	const char *message, unsigned int messageLen)
{
	char *pos = buf;
	char *end = buf + bufSize;

	pos = appendData(pos, end, "App ");
	pos = appendData(pos, end, pidStr, pidStrLen);
	pos = appendData(pos, end, " ");
	pos = appendData(pos, end, channelName, channelNameLen);
	pos = appendData(pos, end, ": ");
	pos = appendData(pos, end, message, messageLen);
	pos = appendData(pos, end, "\n");
	_writeLogEntry(StaticString(buf, pos - buf));
}

void
printAppOutput(pid_t pid, const char *channelName, const char *message, unsigned int size) {
	if (printAppOutputAsDebuggingMessages) {
		P_DEBUG("App " << pid << " " << channelName << ": " << StaticString(message, size));
	} else {
		char pidStr[sizeof("4294967295")];
		unsigned int pidStrLen, channelNameLen, totalLen;

		try {
			pidStrLen = integerToOtherBase<pid_t, 10>(pid, pidStr, sizeof(pidStr));
		} catch (const std::length_error &) {
			pidStr[0] = '?';
			pidStr[1] = '\0';
			pidStrLen = 1;
		}

		channelNameLen = strlen(channelName);
		totalLen = (sizeof("App X Y: \n") - 2) + pidStrLen + channelNameLen + size;
		if (totalLen < 1024) {
			char buf[1024];
			realPrintAppOutput(buf, sizeof(buf),
				pidStr, pidStrLen,
				channelName, channelNameLen,
				message, size);
		} else {
			DynamicBuffer buf(totalLen);
			realPrintAppOutput(buf.data, totalLen,
				pidStr, pidStrLen,
				channelName, channelNameLen,
				message, size);
		}
	}
}

void
setPrintAppOutputAsDebuggingMessages(bool enabled) {
	printAppOutputAsDebuggingMessages = enabled;
}

} // namespace Passenger

