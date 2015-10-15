/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
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
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <boost/thread.hpp>
#include <boost/atomic.hpp>
#include <Logging.h>
#include <Constants.h>
#include <StaticString.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>

namespace Passenger {

volatile sig_atomic_t _logLevel = DEFAULT_LOG_LEVEL;
AssertionFailureInfo lastAssertionFailure;
static bool printAppOutputAsDebuggingMessages = false;

static boost::mutex logFileMutex;
static string logFile;
static int logFd = STDERR_FILENO;

static int fileDescriptorLog = -1;
static string fileDescriptorLogFile;

#define TRUNCATE_LOGPATHS_TO_MAXCHARS 3 // set to 0 to disable truncation


void
setLogLevel(int value) {
	_logLevel = value;
	boost::atomic_signal_fence(boost::memory_order_seq_cst);
}

string getLogFile() {
	boost::lock_guard<boost::mutex> l(logFileMutex);
	return logFile;
}

bool
setLogFile(const string &path, int *errcode) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd != -1) {
		setLogFileWithFd(path, fd);
		close(fd);
		return true;
	} else {
		if (errcode != NULL) {
			*errcode = errno;
		}
		return false;
	}
}

void
setLogFileWithFd(const string &path, int fd) {
	boost::lock_guard<boost::mutex> l(logFileMutex);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	logFile = path;
}

bool
setLogFileWithoutRedirectingStderr(const string &path, int *errcode) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd != -1) {
		int oldLogFd = logFd;
		logFd = fd;
		if (oldLogFd != STDERR_FILENO) {
			close(oldLogFd);
		}
		boost::lock_guard<boost::mutex> l(logFileMutex);
		logFile = path;
		return true;
	} else {
		if (errcode != NULL) {
			*errcode = errno;
		}
		return false;
	}
}

bool
hasFileDescriptorLogFile() {
	return fileDescriptorLog != -1;
}

string
getFileDescriptorLogFile() {
	return fileDescriptorLogFile;
}

int
getFileDescriptorLogFileFd() {
	return fileDescriptorLog;
}

bool
setFileDescriptorLogFile(const string &path, int *errcode) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd != -1) {
		if (fileDescriptorLog == -1) {
			fileDescriptorLog = fd;
		} else {
			dup2(fd, fileDescriptorLog);
			close(fd);
		}
		if (fileDescriptorLogFile != path) {
			// Do not mutate `fileDescriptorLogFile` if the path
			// hasn't changed. This allows `setFileDescriptorLogFile()`
			// to be thread-safe within the documented constraints.
			fileDescriptorLogFile = path;
		}
		return true;
	} else {
		if (errcode != NULL) {
			*errcode = errno;
		}
		return false;
	}
}

void
_prepareLogEntry(FastStringStream<> &sstream, const char *file, unsigned int line) {
	struct tm the_tm;
	char datetime_buf[32];
	int datetime_size;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &the_tm);
	datetime_size = snprintf(datetime_buf, sizeof(datetime_buf),
		"%d-%02d-%02d %02d:%02d:%02d.%04llu",
		the_tm.tm_year + 1900, the_tm.tm_mon + 1, the_tm.tm_mday,
		the_tm.tm_hour, the_tm.tm_min, the_tm.tm_sec,
		(unsigned long long) tv.tv_usec / 100);
	sstream <<
		"[ " << StaticString(datetime_buf, datetime_size) <<
		" " << std::dec << getpid() << "/" <<
			std::hex << pthread_self() << std::dec <<
		" ";

	if (startsWith(file, P_STATIC_STRING("src/"))) { // special reduncancy filter because most code resides in these paths
		file += sizeof("src/") - 1;
		if (startsWith(file, P_STATIC_STRING("cxx_supportlib/"))) {
			file += sizeof("cxx_supportlib/") - 1;
		}
	}

	if (TRUNCATE_LOGPATHS_TO_MAXCHARS > 0) {
		truncateBeforeTokens(file, P_STATIC_STRING("/\\"), TRUNCATE_LOGPATHS_TO_MAXCHARS, sstream);
	} else {
		sstream << file;
	}

	sstream << ":" << line << " ]: ";
}

static void
writeExactWithoutOXT(int fd, const char *str, unsigned int size) {
	/* We do not use writeExact() here because writeExact()
	 * uses oxt::syscalls::write(), which is an interruption point and
	 * which is slightly more expensive than a plain write().
	 * Logging may block, but in most cases not indefinitely,
	 * so we don't care if the write() here is not an interruption
	 * point. If the write does block indefinitely then it's
	 * probably a FIFO that is not opened on the other side.
	 * In that case we can blame the user.
	 */
	ssize_t ret;
	unsigned int written = 0;
	while (written < size) {
		do {
			ret = write(fd, str + written, size - written);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			/* The most likely reason why this fails is when the user has setup
			 * Apache to log to a pipe (e.g. to a log rotation script). Upon
			 * restarting the web server, the process that reads from the pipe
			 * shuts down, so we can't write to it anymore. That's why we
			 * just ignore write errors. It doesn't make sense to abort for
			 * something like this.
			 */
			break;
		} else {
			written += ret;
		}
	}
}

void
_writeLogEntry(const char *str, unsigned int size) {
	writeExactWithoutOXT(logFd, str, size);
}

void
_writeFileDescriptorLogEntry(const char *str, unsigned int size) {
	writeExactWithoutOXT(fileDescriptorLog, str, size);
}

const char *
_strdupFastStringStream(const FastStringStream<> &stream) {
	char *buf = (char *) malloc(stream.size() + 1);
	memcpy(buf, stream.data(), stream.size());
	buf[stream.size()] = '\0';
	return buf;
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
	_writeLogEntry(buf, pos - buf);
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

