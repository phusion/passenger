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
#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#include <oxt/initialize.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#ifdef __linux__
	#include <sys/syscall.h>
	#include <features.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>

#if defined(__APPLE__) || defined(__GNU_LIBRARY__)
	#define LIBC_HAS_BACKTRACE_FUNC
#endif
#ifdef LIBC_HAS_BACKTRACE_FUNC
	#include <execinfo.h>
#endif

#include <string>
#include <vector>

#include <Shared/Base.h>
#include <Constants.h>
#include <Exceptions.h>
#include <Logging.h>
#include <ResourceLocator.h>
#include <Utils.h>
#include <Utils/SystemTime.h>
#include <Utils/StrIntUtils.h>
#ifdef __linux__
	#include <ResourceLocator.h>
#endif

namespace Passenger {


using namespace std;


struct AbortHandlerState {
	pid_t pid;
	int signo;
	siginfo_t *info;
	char messagePrefix[32];
	char messageBuf[1024];
};

typedef void (*Callback)(AbortHandlerState &state, void *userData);


#define IGNORE_SYSCALL_RESULT(code) \
	do { \
		int _ret = code; \
		(void) _ret; \
	} while (false)


static bool _feedbackFdAvailable = false;
static const char digits[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};
static const char hex_chars[] = "0123456789abcdef";

static bool shouldDumpWithCrashWatch = true;
static bool beepOnAbort = false;
static bool stopOnAbort = false;

// Pre-allocate an alternative stack for use in signal handlers in case
// the normal stack isn't usable.
static char *alternativeStack;
static unsigned int alternativeStackSize;

static volatile unsigned int abortHandlerCalled = 0;
static unsigned int randomSeed = 0;
static char **origArgv = NULL;
static const char *rubyLibDir = NULL;
static const char *passengerRoot = NULL;
static const char *defaultRuby = DEFAULT_RUBY;
static const char *backtraceSanitizerCommand = NULL;
static bool backtraceSanitizerPassProgramInfo = true;
static const char *crashWatch = NULL;
static DiagnosticsDumper customDiagnosticsDumper = NULL;
static void *customDiagnosticsDumperUserData;

// We preallocate a few pipes during startup which we will close in the
// crash handler. This way we can be sure that when the crash handler
// calls pipe() it won't fail with "Too many files".
static int emergencyPipe1[2] = { -1, -1 };
static int emergencyPipe2[2] = { -1, -1 };


static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

const char *
getEnvString(const char *name, const char *defaultValue) {
	const char *value = getenv(name);
	if (value != NULL && *value != '\0') {
		return value;
	} else {
		return defaultValue;
	}
}

bool
hasEnvOption(const char *name, bool defaultValue) {
	const char *value = getEnvString(name);
	if (value != NULL) {
		return strcmp(value, "yes") == 0
			|| strcmp(value, "y") == 0
			|| strcmp(value, "1") == 0
			|| strcmp(value, "on") == 0
			|| strcmp(value, "true") == 0;
	} else {
		return defaultValue;
	}
}

// When we're in a crash handler, there's nothing we can do if we fail to
// write to stderr, so we ignore its return value and we ignore compiler
// warnings about ignoring that.
static void
write_nowarn(int fd, const void *buf, size_t n) {
	ssize_t ret = write(fd, buf, n);
	(void) ret;
}

// No idea whether strlen() is async signal safe, but let's not risk it
// and write our own version instead that's guaranteed to be safe.
static size_t
safeStrlen(const char *str) {
	size_t size = 0;
	while (*str != '\0') {
		str++;
		size++;
	}
	return size;
}

// Async-signal safe way to print to stderr.
static void
safePrintErr(const char *message) {
	write_nowarn(STDERR_FILENO, message, strlen(message));
}

// Must be async signal safe.
static char *
appendText(char *buf, const char *text) {
	size_t len = safeStrlen(text);
	strcpy(buf, text);
	return buf + len;
}

// Must be async signal safe.
static void
reverse(char *str, size_t len) {
	char *p1, *p2;
	if (*str == '\0') {
		return;
	}
	for (p1 = str, p2 = str + len - 1; p2 > p1; ++p1, --p2) {
		*p1 ^= *p2;
		*p2 ^= *p1;
		*p1 ^= *p2;
	}
}

// Must be async signal safe.
static char *
appendULL(char *buf, unsigned long long value) {
	unsigned long long remainder = value;
	unsigned int size = 0;

	do {
		buf[size] = digits[remainder % 10];
		remainder = remainder / 10;
		size++;
	} while (remainder != 0);

	reverse(buf, size);
	return buf + size;
}

// Must be async signal safe.
template<typename IntegerType>
static char *
appendIntegerAsHex(char *buf, IntegerType value) {
	IntegerType remainder = value;
	unsigned int size = 0;

	do {
		buf[size] = hex_chars[remainder % 16];
		remainder = remainder / 16;
		size++;
	} while (remainder != 0);

	reverse(buf, size);
	return buf + size;
}

// Must be async signal safe.
static char *
appendPointerAsString(char *buf, void *pointer) {
	// Use wierd union construction to avoid compiler warnings.
	if (sizeof(void *) == sizeof(unsigned int)) {
		union {
			void *pointer;
			unsigned int value;
		} u;
		u.pointer = pointer;
		return appendIntegerAsHex(appendText(buf, "0x"), u.value);
	} else if (sizeof(void *) == sizeof(unsigned long long)) {
		union {
			void *pointer;
			unsigned long long value;
		} u;
		u.pointer = pointer;
		return appendIntegerAsHex(appendText(buf, "0x"), u.value);
	} else {
		return appendText(buf, "(pointer size unsupported)");
	}
}

static char *
appendSignalName(char *buf, int signo) {
	switch (signo) {
	case SIGABRT:
		buf = appendText(buf, "SIGABRT");
		break;
	case SIGSEGV:
		buf = appendText(buf, "SIGSEGV");
		break;
	case SIGBUS:
		buf = appendText(buf, "SIGBUS");
		break;
	case SIGFPE:
		buf = appendText(buf, "SIGFPE");
		break;
	case SIGILL:
		buf = appendText(buf, "SIGILL");
		break;
	default:
		return appendULL(buf, (unsigned long long) signo);
	}
	buf = appendText(buf, "(");
	buf = appendULL(buf, (unsigned long long) signo);
	buf = appendText(buf, ")");
	return buf;
}

#define SI_CODE_HANDLER(name) \
	case name: \
		buf = appendText(buf, #name); \
		break

// Must be async signal safe.
static char *
appendSignalReason(char *buf, siginfo_t *info) {
	bool handled = true;

	switch (info->si_code) {
	SI_CODE_HANDLER(SI_USER);
	#ifdef SI_KERNEL
		SI_CODE_HANDLER(SI_KERNEL);
	#endif
	SI_CODE_HANDLER(SI_QUEUE);
	SI_CODE_HANDLER(SI_TIMER);
	#ifdef SI_ASYNCIO
		SI_CODE_HANDLER(SI_ASYNCIO);
	#endif
	#ifdef SI_MESGQ
		SI_CODE_HANDLER(SI_MESGQ);
	#endif
	#ifdef SI_SIGIO
		SI_CODE_HANDLER(SI_SIGIO);
	#endif
	#ifdef SI_TKILL
		SI_CODE_HANDLER(SI_TKILL);
	#endif
	default:
		switch (info->si_signo) {
		case SIGSEGV:
			switch (info->si_code) {
			#ifdef SEGV_MAPERR
				SI_CODE_HANDLER(SEGV_MAPERR);
			#endif
			#ifdef SEGV_ACCERR
				SI_CODE_HANDLER(SEGV_ACCERR);
			#endif
			default:
				handled = false;
				break;
			}
			break;
		case SIGBUS:
			switch (info->si_code) {
			#ifdef BUS_ADRALN
				SI_CODE_HANDLER(BUS_ADRALN);
			#endif
			#ifdef BUS_ADRERR
				SI_CODE_HANDLER(BUS_ADRERR);
			#endif
			#ifdef BUS_OBJERR
				SI_CODE_HANDLER(BUS_OBJERR);
			#endif
			default:
				handled = false;
				break;
			}
			break;
		default:
			handled = false;
			break;
		}
		if (!handled) {
			buf = appendText(buf, "#");
			buf = appendULL(buf, (unsigned long long) info->si_code);
		}
		break;
	}

	if (info->si_code <= 0) {
		buf = appendText(buf, ", signal sent by PID ");
		buf = appendULL(buf, (unsigned long long) info->si_pid);
		buf = appendText(buf, " with UID ");
		buf = appendULL(buf, (unsigned long long) info->si_uid);
	}

	buf = appendText(buf, ", si_addr=");
	buf = appendPointerAsString(buf, info->si_addr);

	return buf;
}

static int
runInSubprocessWithTimeLimit(AbortHandlerState &state, Callback callback, void *userData, int timeLimit) {
	char *end;
	pid_t child;
	int p[2], e;

	if (pipe(p) == -1) {
		e = errno;
		end = state.messageBuf;
		end = appendText(end, "Could not create subprocess: pipe() failed with errno=");
		end = appendULL(end, e);
		end = appendText(end, "\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
		return -1;
	}

	child = asyncFork();
	if (child == 0) {
		close(p[0]);
		callback(state, userData);
		_exit(0);
		return -1;

	} else if (child == -1) {
		e = errno;
		close(p[0]);
		close(p[1]);
		end = state.messageBuf;
		end = appendText(end, "Could not create subprocess: fork() failed with errno=");
		end = appendULL(end, e);
		end = appendText(end, "\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
		return -1;

	} else {
		int status;
		close(p[1]);

		// We give the child process a time limit. If it doesn't succeed in
		// exiting within the time limit, we assume that it has frozen
		// and we kill it.
		struct pollfd fd;
		fd.fd = p[0];
		fd.events = POLLIN | POLLHUP | POLLERR;
		if (poll(&fd, 1, timeLimit) <= 0) {
			kill(child, SIGKILL);
			safePrintErr("Could not run child process: it did not exit in time\n");
		}
		close(p[0]);
		if (waitpid(child, &status, 0) == child) {
			return status;
		} else {
			return -1;
		}
	}
}

static void
dumpFileDescriptorInfoWithLsof(AbortHandlerState &state, void *userData) {
	char *end;

	end = state.messageBuf;
	end = appendULL(end, state.pid);
	*end = '\0';

	closeAllFileDescriptors(2, true);

	execlp("lsof", "lsof", "-p", state.messageBuf, "-nP", (const char * const) 0);

	end = state.messageBuf;
	end = appendText(end, "ERROR: cannot execute command 'lsof': errno=");
	end = appendULL(end, errno);
	end = appendText(end, "\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
	_exit(1);
}

static void
dumpFileDescriptorInfoWithLs(AbortHandlerState &state, char *end) {
	pid_t pid;
	int status;

	pid = asyncFork();
	if (pid == 0) {
		closeAllFileDescriptors(2, true);
		// The '-v' is for natural sorting on Linux. On BSD -v means something else but it's harmless.
		execlp("ls", "ls", "-lv", state.messageBuf, (const char * const) 0);
		_exit(1);
	} else if (pid == -1) {
		safePrintErr("ERROR: Could not fork a process to dump file descriptor information!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		safePrintErr("ERROR: Could not run 'ls' to dump file descriptor information!\n");
	}
}

static void
dumpFileDescriptorInfo(AbortHandlerState &state) {
	char *messageBuf = state.messageBuf;
	char *end;
	struct stat buf;
	int status;

	end = messageBuf;
	end = appendText(end, state.messagePrefix);
	end = appendText(end, " ] Open files and file descriptors:\n");
	write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);

	status = runInSubprocessWithTimeLimit(state, dumpFileDescriptorInfoWithLsof, NULL, 4000);

	if (status != 0) {
		safePrintErr("Falling back to another mechanism for dumping file descriptors.\n");

		end = messageBuf;
		end = appendText(end, "/proc/");
		end = appendULL(end, state.pid);
		end = appendText(end, "/fd");
		*end = '\0';
		if (stat(messageBuf, &buf) == 0) {
			dumpFileDescriptorInfoWithLs(state, end + 1);
		} else {
			end = messageBuf;
			end = appendText(end, "/dev/fd");
			*end = '\0';
			if (stat(messageBuf, &buf) == 0) {
				dumpFileDescriptorInfoWithLs(state, end + 1);
			} else {
				end = messageBuf;
				end = appendText(end, "ERROR: No other file descriptor dumping mechanism on current platform detected.\n");
				write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);
			}
		}
	}
}

static void
dumpWithCrashWatch(AbortHandlerState &state) {
	char *messageBuf = state.messageBuf;
	const char *pidStr = messageBuf;
	char *end = messageBuf;
	end = appendULL(end, (unsigned long long) state.pid);
	*end = '\0';

	pid_t child = asyncFork();
	if (child == 0) {
		closeAllFileDescriptors(2, true);
		execlp(defaultRuby, defaultRuby, crashWatch, rubyLibDir,
			passengerRoot, "--dump", pidStr, (char * const) 0);
		int e = errno;
		end = messageBuf;
		end = appendText(end, "crash-watch is could not be executed! ");
		end = appendText(end, "(execlp() returned errno=");
		end = appendULL(end, e);
		end = appendText(end, ") Please check your file permissions or something.\n");
		write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);
		_exit(1);

	} else if (child == -1) {
		int e = errno;
		end = messageBuf;
		end = appendText(end, "Could not execute crash-watch: fork() failed with errno=");
		end = appendULL(end, e);
		end = appendText(end, "\n");
		write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);

	} else {
		waitpid(child, NULL, 0);
	}
}

#ifdef LIBC_HAS_BACKTRACE_FUNC
	static void
	dumpBacktrace(AbortHandlerState &state, void *userData) {
		void *backtraceStore[512];
		int frames = backtrace(backtraceStore, sizeof(backtraceStore) / sizeof(void *));
		char *end = state.messageBuf;
		end = appendText(end, "--------------------------------------\n");
		end = appendText(end, "[ pid=");
		end = appendULL(end, (unsigned long long) state.pid);
		end = appendText(end, " ] Backtrace with ");
		end = appendULL(end, (unsigned long long) frames);
		end = appendText(end, " frames:\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);

		if (backtraceSanitizerCommand != NULL) {
			int p[2];
			if (pipe(p) == -1) {
				int e = errno;
				end = state.messageBuf;
				end = appendText(end, "Could not dump diagnostics through backtrace sanitizer: pipe() failed with errno=");
				end = appendULL(end, e);
				end = appendText(end, "\n");
				end = appendText(end, "Falling back to writing to stderr directly...\n");
				write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
				backtrace_symbols_fd(backtraceStore, frames, STDERR_FILENO);
				return;
			}

			pid_t pid = asyncFork();
			if (pid == 0) {
				const char *pidStr = end = state.messageBuf;
				end = appendULL(end, (unsigned long long) state.pid);
				*end = '\0';
				end++;

				close(p[1]);
				dup2(p[0], STDIN_FILENO);
				closeAllFileDescriptors(2, true);

				char *command = end;
				end = appendText(end, "exec ");
				end = appendText(end, backtraceSanitizerCommand);
				if (backtraceSanitizerPassProgramInfo) {
					end = appendText(end, " \"");
					end = appendText(end, origArgv[0]);
					end = appendText(end, "\" ");
					end = appendText(end, pidStr);
				}
				*end = '\0';
				end++;
				execlp("/bin/sh", "/bin/sh", "-c", command, (const char * const) 0);

				end = state.messageBuf;
				end = appendText(end, "ERROR: cannot execute '");
				end = appendText(end, backtraceSanitizerCommand);
				end = appendText(end, "' for sanitizing the backtrace, trying 'cat'...\n");
				write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
				execlp("cat", "cat", (const char * const) 0);
				execlp("/bin/cat", "cat", (const char * const) 0);
				execlp("/usr/bin/cat", "cat", (const char * const) 0);
				safePrintErr("ERROR: cannot execute 'cat'\n");
				_exit(1);

			} else if (pid == -1) {
				close(p[0]);
				close(p[1]);
				int e = errno;
				end = state.messageBuf;
				end = appendText(end, "Could not dump diagnostics through backtrace sanitizer: fork() failed with errno=");
				end = appendULL(end, e);
				end = appendText(end, "\n");
				end = appendText(end, "Falling back to writing to stderr directly...\n");
				write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
				backtrace_symbols_fd(backtraceStore, frames, STDERR_FILENO);

			} else {
				int status = -1;

				close(p[0]);
				backtrace_symbols_fd(backtraceStore, frames, p[1]);
				close(p[1]);
				if (waitpid(pid, &status, 0) == -1 || status != 0) {
					end = state.messageBuf;
					end = appendText(end, "ERROR: cannot execute '");
					end = appendText(end, backtraceSanitizerCommand);
					end = appendText(end, "' for sanitizing the backtrace, writing to stderr directly...\n");
					write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
					backtrace_symbols_fd(backtraceStore, frames, STDERR_FILENO);
				}
			}

		} else {
			backtrace_symbols_fd(backtraceStore, frames, STDERR_FILENO);
		}
	}
#endif

static void
runCustomDiagnosticsDumper(AbortHandlerState &state, void *userData) {
	customDiagnosticsDumper(customDiagnosticsDumperUserData);
}

// This function is performed in a child process.
static void
dumpDiagnostics(AbortHandlerState &state) {
	char *messageBuf = state.messageBuf;
	char *end;
	pid_t pid;
	int status;

	end = messageBuf;
	end = appendText(end, state.messagePrefix);
	end = appendText(end, " ] Date, uname and ulimits:\n");
	write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);

	// Dump human-readable time string and string.
	pid = asyncFork();
	if (pid == 0) {
		closeAllFileDescriptors(2, true);
		execlp("date", "date", (const char * const) 0);
		_exit(1);
	} else if (pid == -1) {
		safePrintErr("ERROR: Could not fork a process to dump the time!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		safePrintErr("ERROR: Could not run 'date'!\n");
	}

	// Dump system uname.
	pid = asyncFork();
	if (pid == 0) {
		closeAllFileDescriptors(2, true);
		execlp("uname", "uname", "-mprsv", (const char * const) 0);
		_exit(1);
	} else if (pid == -1) {
		safePrintErr("ERROR: Could not fork a process to dump the uname!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		safePrintErr("ERROR: Could not run 'uname -mprsv'!\n");
	}

	// Dump ulimit.
	pid = asyncFork();
	if (pid == 0) {
		closeAllFileDescriptors(2, true);
		execlp("ulimit", "ulimit", "-a", (const char * const) 0);
		// On Linux 'ulimit' is a shell builtin, not a command.
		execlp("/bin/sh", "/bin/sh", "-c", "ulimit -a", (const char * const) 0);
		_exit(1);
	} else if (pid == -1) {
		safePrintErr("ERROR: Could not fork a process to dump the ulimit!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		safePrintErr("ERROR: Could not run 'ulimit -a'!\n");
	}

	end = messageBuf;
	end = appendText(end, state.messagePrefix);
	end = appendText(end, " ] " PROGRAM_NAME " version: " PASSENGER_VERSION "\n");
	write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);

	if (lastAssertionFailure.filename != NULL) {
		end = messageBuf;
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] Last assertion failure: (");
		end = appendText(end, lastAssertionFailure.expression);
		end = appendText(end, "), ");
		if (lastAssertionFailure.function != NULL) {
			end = appendText(end, "function ");
			end = appendText(end, lastAssertionFailure.function);
			end = appendText(end, ", ");
		}
		end = appendText(end, "file ");
		end = appendText(end, lastAssertionFailure.filename);
		end = appendText(end, ", line ");
		end = appendULL(end, lastAssertionFailure.line);
		end = appendText(end, ".\n");
		write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);
	}

	// It is important that writing the message and the backtrace are two
	// seperate operations because it's not entirely clear whether the
	// latter is async signal safe and thus can crash.
	end = messageBuf;
	end = appendText(end, state.messagePrefix);
	#ifdef LIBC_HAS_BACKTRACE_FUNC
		end = appendText(end, " ] libc backtrace available!\n");
	#else
		end = appendText(end, " ] libc backtrace not available.\n");
	#endif
	write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);

	#ifdef LIBC_HAS_BACKTRACE_FUNC
		runInSubprocessWithTimeLimit(state, dumpBacktrace, NULL, 4000);
	#endif

	safePrintErr("--------------------------------------\n");

	if (customDiagnosticsDumper != NULL) {
		end = messageBuf;
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] Dumping additional diagnostical information...\n");
		write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);
		safePrintErr("--------------------------------------\n");
		runInSubprocessWithTimeLimit(state, runCustomDiagnosticsDumper, NULL, 2000);
		safePrintErr("--------------------------------------\n");
	}

	dumpFileDescriptorInfo(state);
	safePrintErr("--------------------------------------\n");

	if (shouldDumpWithCrashWatch) {
		end = messageBuf;
		end = appendText(end, state.messagePrefix);
		#ifdef LIBC_HAS_BACKTRACE_FUNC
			end = appendText(end, " ] Dumping a more detailed backtrace with crash-watch...\n");
		#else
			end = appendText(end, " ] Dumping a backtrace with crash-watch...\n");
		#endif
		write_nowarn(STDERR_FILENO, messageBuf, end - messageBuf);
		dumpWithCrashWatch(state);
	} else {
		write_nowarn(STDERR_FILENO, "\n", 1);
	}
}

static bool
createCrashLogFile(char *filename, time_t t) {
	char *end = filename;
	end = appendText(end, "/var/tmp/passenger-crash-log.");
	end = appendULL(end, (unsigned long long) t);
	*end = '\0';

	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		end = filename;
		end = appendText(end, "/tmp/passenger-crash-log.");
		end = appendULL(end, (unsigned long long) t);
		*end = '\0';
		fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	}
	if (fd == -1) {
		*filename = '\0';
		return false;
	} else {
		close(fd);
		return true;
	}
}

static void
forkAndRedirectToTee(char *filename) {
	pid_t pid;
	int p[2];

	if (pipe(p) == -1) {
		// Signal error condition.
		*filename = '\0';
		return;
	}

	pid = asyncFork();
	if (pid == 0) {
		close(p[1]);
		dup2(p[0], STDIN_FILENO);
		execlp("tee", "tee", filename, (const char * const) 0);
		execlp("/usr/bin/tee", "tee", filename, (const char * const) 0);
		execlp("cat", "cat", (const char * const) 0);
		execlp("/bin/cat", "cat", (const char * const) 0);
		execlp("/usr/bin/cat", "cat", (const char * const) 0);
		safePrintErr("ERROR: cannot execute 'tee' or 'cat'; crash log will be lost!\n");
		_exit(1);
	} else if (pid == -1) {
		safePrintErr("ERROR: cannot fork a process for executing 'tee'\n");
		*filename = '\0';
	} else {
		close(p[0]);
		dup2(p[1], STDOUT_FILENO);
		dup2(p[1], STDERR_FILENO);
	}
}

static void
abortHandler(int signo, siginfo_t *info, void *ctx) {
	AbortHandlerState state;
	state.pid = getpid();
	state.signo = signo;
	state.info = info;
	pid_t child;
	time_t t = time(NULL);
	char crashLogFile[256];

	abortHandlerCalled++;
	if (abortHandlerCalled > 1) {
		// The abort handler itself crashed!
		char *end = state.messageBuf;
		end = appendText(end, "[ origpid=");
		end = appendULL(end, (unsigned long long) state.pid);
		end = appendText(end, ", pid=");
		end = appendULL(end, (unsigned long long) getpid());
		end = appendText(end, ", timestamp=");
		end = appendULL(end, (unsigned long long) t);
		if (abortHandlerCalled == 2) {
			// This is the first time it crashed.
			end = appendText(end, " ] Abort handler crashed! signo=");
			end = appendSignalName(end, state.signo);
			end = appendText(end, ", reason=");
			end = appendSignalReason(end, state.info);
			end = appendText(end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
			// Run default signal handler.
			raise(signo);
		} else {
			// This is the second time it crashed, meaning it failed to
			// invoke the default signal handler to abort the process!
			end = appendText(end, " ] Abort handler crashed again! Force exiting this time. signo=");
			end = appendSignalName(end, state.signo);
			end = appendText(end, ", reason=");
			end = appendSignalReason(end, state.info);
			end = appendText(end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
			_exit(1);
		}
		return;
	}

	if (emergencyPipe1[0] != -1) {
		close(emergencyPipe1[0]);
	}
	if (emergencyPipe1[1] != -1) {
		close(emergencyPipe1[1]);
	}
	if (emergencyPipe2[0] != -1) {
		close(emergencyPipe2[0]);
	}
	if (emergencyPipe2[1] != -1) {
		close(emergencyPipe2[1]);
	}
	emergencyPipe1[0] = emergencyPipe1[1] = -1;
	emergencyPipe2[0] = emergencyPipe2[1] = -1;

	/* We want to dump the entire crash log to both stderr and a log file.
	 * We use 'tee' for this.
	 */
	if (createCrashLogFile(crashLogFile, t)) {
		forkAndRedirectToTee(crashLogFile);
	}

	char *end = state.messagePrefix;
	end = appendText(end, "[ pid=");
	end = appendULL(end, (unsigned long long) state.pid);
	*end = '\0';

	end = state.messageBuf;
	end = appendText(end, state.messagePrefix);
	end = appendText(end, ", timestamp=");
	end = appendULL(end, (unsigned long long) t);
	end = appendText(end, " ] Process aborted! signo=");
	end = appendSignalName(end, state.signo);
	end = appendText(end, ", reason=");
	end = appendSignalReason(end, state.info);
	end = appendText(end, ", randomSeed=");
	end = appendULL(end, (unsigned long long) randomSeed);
	end = appendText(end, "\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);

	end = state.messageBuf;
	if (*crashLogFile != '\0') {
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] Crash log dumped to ");
		end = appendText(end, crashLogFile);
		end = appendText(end, "\n");
	} else {
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] Could not create crash log file, so dumping to stderr only.\n");
	}
	write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);

	if (beepOnAbort) {
		end = state.messageBuf;
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] PASSENGER_BEEP_ON_ABORT on, executing beep...\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);

		child = asyncFork();
		if (child == 0) {
			closeAllFileDescriptors(2, true);
			#ifdef __APPLE__
				execlp("osascript", "osascript", "-e", "beep 2", (const char * const) 0);
				safePrintErr("Cannot execute 'osascript' command\n");
			#else
				execlp("beep", "beep", (const char * const) 0);
				safePrintErr("Cannot execute 'beep' command\n");
			#endif
			_exit(1);

		} else if (child == -1) {
			int e = errno;
			end = state.messageBuf;
			end = appendText(end, state.messagePrefix);
			end = appendText(end, " ] Could fork a child process for invoking a beep: fork() failed with errno=");
			end = appendULL(end, e);
			end = appendText(end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
		}
	}

	if (stopOnAbort) {
		end = state.messageBuf;
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] PASSENGER_STOP_ON_ABORT on, so process stopped. Send SIGCONT when you want to continue.\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
		raise(SIGSTOP);
	}

	// It isn't safe to call any waiting functions in this signal handler,
	// not even read() and waitpid() even though they're async signal safe.
	// So we fork a child process and let it dump as much diagnostics as possible
	// instead of doing it in this process.
	child = asyncFork();
	if (child == 0) {
		// Sleep for a short while to allow the parent process to raise SIGSTOP.
		// usleep() and nanosleep() aren't async signal safe so we use select()
		// instead.
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		select(0, NULL, NULL, NULL, &tv);

		resetSignalHandlersAndMask();

		child = asyncFork();
		if (child == 0) {
			// OS X: for some reason the SIGPIPE handler may be reset to default after forking.
			// Later in this program we're going to pipe backtrace_symbols_fd() into the backtrace
			// sanitizer, which may fail, and we don't want the diagnostics process to crash
			// with SIGPIPE as a result, so we ignore SIGPIPE again.
			ignoreSigpipe();
			dumpDiagnostics(state);
			// The child process may or may or may not resume the original process.
			// We do it ourselves just to be sure.
			kill(state.pid, SIGCONT);
			_exit(0);

		} else if (child == -1) {
			int e = errno;
			end = state.messageBuf;
			end = appendText(end, state.messagePrefix);
			end = appendText(end, "] Could fork a child process for dumping diagnostics: fork() failed with errno=");
			end = appendULL(end, e);
			end = appendText(end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);
			_exit(1);

		} else {
			// Exit immediately so that child process is adopted by init process.
			_exit(0);
		}

	} else if (child == -1) {
		int e = errno;
		end = state.messageBuf;
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] Could fork a child process for dumping diagnostics: fork() failed with errno=");
		end = appendULL(end, e);
		end = appendText(end, "\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, end - state.messageBuf);

	} else {
		raise(SIGSTOP);
		// Will continue after the child process has done its job.
	}

	// Run default signal handler.
	raise(signo);
}

/*
 * Override assert() to add more features and to fix bugs. We save the information
 * of the last assertion failure in a global variable so that we can print it
 * to the crash diagnostics report.
 */
#if defined(__GLIBC__)
	extern "C" __attribute__ ((__noreturn__))
	void
	__assert_fail(__const char *__assertion, __const char *__file,
		unsigned int __line, __const char *__function)
	{
		lastAssertionFailure.filename = __file;
		lastAssertionFailure.line = __line;
		lastAssertionFailure.function = __function;
		lastAssertionFailure.expression = __assertion;
		fprintf(stderr, "Assertion failed! %s:%u: %s: %s\n", __file, __line, __function, __assertion);
		fflush(stderr);
		abort();
	}

#elif defined(__APPLE__)
	/* On OS X, raise() and abort() unfortunately send SIGABRT to the main thread,
	 * causing the original backtrace to be lost in the signal handler.
	 * We work around this for anything in the same linkage unit by just definin
	 * our own versions of the assert handler and abort.
	 */

	#include <pthread.h>

	extern "C" int
	raise(int sig) {
		return pthread_kill(pthread_self(), sig);
	}

	extern "C" void
	__assert_rtn(const char *func, const char *file, int line, const char *expr) {
		lastAssertionFailure.filename = file;
		lastAssertionFailure.line = line;
		lastAssertionFailure.function = func;
		lastAssertionFailure.expression = expr;
		if (func) {
			fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n",
				expr, func, file, line);
		} else {
			fprintf(stderr, "Assertion failed: (%s), file %s, line %d.\n",
				expr, file, line);
		}
		fflush(stderr);
		abort();
	}

	extern "C" void
	abort() {
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGABRT);
		pthread_sigmask(SIG_UNBLOCK, &set, NULL);
		raise(SIGABRT);
		usleep(1000);
		__builtin_trap();
	}
#endif /* __APPLE__ */

void
installAgentAbortHandler() {
	alternativeStackSize = MINSIGSTKSZ + 128 * 1024;
	alternativeStack = (char *) malloc(alternativeStackSize);
	if (alternativeStack == NULL) {
		fprintf(stderr, "Cannot allocate an alternative with a size of %u bytes!\n",
			alternativeStackSize);
		fflush(stderr);
		abort();
	}

	stack_t stack;
	stack.ss_sp = alternativeStack;
	stack.ss_size = alternativeStackSize;
	stack.ss_flags = 0;
	if (sigaltstack(&stack, NULL) != 0) {
		int e = errno;
		fprintf(stderr, "Cannot install an alternative stack for use in signal handlers: %s (%d)\n",
			strerror(e), e);
		fflush(stderr);
		abort();
	}

	struct sigaction action;
	action.sa_sigaction = abortHandler;
	action.sa_flags = SA_RESETHAND | SA_SIGINFO;
	sigemptyset(&action.sa_mask);
	sigaction(SIGABRT, &action, NULL);
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);
	sigaction(SIGFPE, &action, NULL);
	sigaction(SIGILL, &action, NULL);
}

void
installDiagnosticsDumper(DiagnosticsDumper func, void *userData) {
	customDiagnosticsDumper = func;
	customDiagnosticsDumperUserData = userData;
}

bool
feedbackFdAvailable() {
	return _feedbackFdAvailable;
}

static int
lookupErrno(const char *name) {
	struct Entry {
		int errorCode;
		const char * const name;
	};
	static const Entry entries[] = {
		{ EPERM, "EPERM" },
		{ ENOENT, "ENOENT" },
		{ ESRCH, "ESRCH" },
		{ EINTR, "EINTR" },
		{ EBADF, "EBADF" },
		{ ENOMEM, "ENOMEM" },
		{ EACCES, "EACCES" },
		{ EBUSY, "EBUSY" },
		{ EEXIST, "EEXIST" },
		{ ENOTDIR, "ENOTDIR" },
		{ EISDIR, "EISDIR" },
		{ EINVAL, "EINVAL" },
		{ ENFILE, "ENFILE" },
		{ EMFILE, "EMFILE" },
		{ ENOTTY, "ENOTTY" },
		{ ETXTBSY, "ETXTBSY" },
		{ ENOSPC, "ENOSPC" },
		{ ESPIPE, "ESPIPE" },
		{ EMLINK, "EMLINK" },
		{ EPIPE, "EPIPE" },
		{ EAGAIN, "EAGAIN" },
		{ EWOULDBLOCK, "EWOULDBLOCK" },
		{ EINPROGRESS, "EINPROGRESS" },
		{ EADDRINUSE, "EADDRINUSE" },
		{ EADDRNOTAVAIL, "EADDRNOTAVAIL" },
		{ ENETUNREACH, "ENETUNREACH" },
		{ ECONNABORTED, "ECONNABORTED" },
		{ ECONNRESET, "ECONNRESET" },
		{ EISCONN, "EISCONN" },
		{ ENOTCONN, "ENOTCONN" },
		{ ETIMEDOUT, "ETIMEDOUT" },
		{ ECONNREFUSED, "ECONNREFUSED" },
		{ EHOSTDOWN, "EHOSTDOWN" },
		{ EHOSTUNREACH, "EHOSTUNREACH" },
		#ifdef EIO
			{ EIO, "EIO" },
		#endif
		#ifdef ENXIO
			{ ENXIO, "ENXIO" },
		#endif
		#ifdef E2BIG
			{ E2BIG, "E2BIG" },
		#endif
		#ifdef ENOEXEC
			{ ENOEXEC, "ENOEXEC" },
		#endif
		#ifdef ECHILD
			{ ECHILD, "ECHILD" },
		#endif
		#ifdef EDEADLK
			{ EDEADLK, "EDEADLK" },
		#endif
		#ifdef EFAULT
			{ EFAULT, "EFAULT" },
		#endif
		#ifdef ENOTBLK
			{ ENOTBLK, "ENOTBLK" },
		#endif
		#ifdef EXDEV
			{ EXDEV, "EXDEV" },
		#endif
		#ifdef ENODEV
			{ ENODEV, "ENODEV" },
		#endif
		#ifdef EFBIG
			{ EFBIG, "EFBIG" },
		#endif
		#ifdef EROFS
			{ EROFS, "EROFS" },
		#endif
		#ifdef EDOM
			{ EDOM, "EDOM" },
		#endif
		#ifdef ERANGE
			{ ERANGE, "ERANGE" },
		#endif
		#ifdef EALREADY
			{ EALREADY, "EALREADY" },
		#endif
		#ifdef ENOTSOCK
			{ ENOTSOCK, "ENOTSOCK" },
		#endif
		#ifdef EDESTADDRREQ
			{ EDESTADDRREQ, "EDESTADDRREQ" },
		#endif
		#ifdef EMSGSIZE
			{ EMSGSIZE, "EMSGSIZE" },
		#endif
		#ifdef EPROTOTYPE
			{ EPROTOTYPE, "EPROTOTYPE" },
		#endif
		#ifdef ENOPROTOOPT
			{ ENOPROTOOPT, "ENOPROTOOPT" },
		#endif
		#ifdef EPROTONOSUPPORT
			{ EPROTONOSUPPORT, "EPROTONOSUPPORT" },
		#endif
		#ifdef ESOCKTNOSUPPORT
			{ ESOCKTNOSUPPORT, "ESOCKTNOSUPPORT" },
		#endif
		#ifdef ENOTSUP
			{ ENOTSUP, "ENOTSUP" },
		#endif
		#ifdef EOPNOTSUPP
			{ EOPNOTSUPP, "EOPNOTSUPP" },
		#endif
		#ifdef EPFNOSUPPORT
			{ EPFNOSUPPORT, "EPFNOSUPPORT" },
		#endif
		#ifdef EAFNOSUPPORT
			{ EAFNOSUPPORT, "EAFNOSUPPORT" },
		#endif
		#ifdef ENETDOWN
			{ ENETDOWN, "ENETDOWN" },
		#endif
		#ifdef ENETRESET
			{ ENETRESET, "ENETRESET" },
		#endif
		#ifdef ENOBUFS
			{ ENOBUFS, "ENOBUFS" },
		#endif
		#ifdef ESHUTDOWN
			{ ESHUTDOWN, "ESHUTDOWN" },
		#endif
		#ifdef ETOOMANYREFS
			{ ETOOMANYREFS, "ETOOMANYREFS" },
		#endif
		#ifdef ELOOP
			{ ELOOP, "ELOOP" },
		#endif
		#ifdef ENAMETOOLONG
			{ ENAMETOOLONG, "ENAMETOOLONG" },
		#endif
		#ifdef ENOTEMPTY
			{ ENOTEMPTY, "ENOTEMPTY" },
		#endif
		#ifdef EPROCLIM
			{ EPROCLIM, "EPROCLIM" },
		#endif
		#ifdef EUSERS
			{ EUSERS, "EUSERS" },
		#endif
		#ifdef EDQUOT
			{ EDQUOT, "EDQUOT" },
		#endif
		#ifdef ESTALE
			{ ESTALE, "ESTALE" },
		#endif
		#ifdef EREMOTE
			{ EREMOTE, "EREMOTE" },
		#endif
		#ifdef EBADRPC
			{ EBADRPC, "EBADRPC" },
		#endif
		#ifdef ERPCMISMATCH
			{ ERPCMISMATCH, "ERPCMISMATCH" },
		#endif
		#ifdef EPROGUNAVAIL
			{ EPROGUNAVAIL, "EPROGUNAVAIL" },
		#endif
		#ifdef EPROGMISMATCH
			{ EPROGMISMATCH, "EPROGMISMATCH" },
		#endif
		#ifdef EPROCUNAVAIL
			{ EPROCUNAVAIL, "EPROCUNAVAIL" },
		#endif
		#ifdef ENOLCK
			{ ENOLCK, "ENOLCK" },
		#endif
		#ifdef ENOSYS
			{ ENOSYS, "ENOSYS" },
		#endif
		#ifdef EFTYPE
			{ EFTYPE, "EFTYPE" },
		#endif
		#ifdef EAUTH
			{ EAUTH, "EAUTH" },
		#endif
		#ifdef ENEEDAUTH
			{ ENEEDAUTH, "ENEEDAUTH" },
		#endif
		#ifdef EPWROFF
			{ EPWROFF, "EPWROFF" },
		#endif
		#ifdef EDEVERR
			{ EDEVERR, "EDEVERR" },
		#endif
		#ifdef EOVERFLOW
			{ EOVERFLOW, "EOVERFLOW" },
		#endif
		#ifdef EBADEXEC
			{ EBADEXEC, "EBADEXEC" },
		#endif
		#ifdef EBADARCH
			{ EBADARCH, "EBADARCH" },
		#endif
		#ifdef ESHLIBVERS
			{ ESHLIBVERS, "ESHLIBVERS" },
		#endif
		#ifdef EBADMACHO
			{ EBADMACHO, "EBADMACHO" },
		#endif
		#ifdef ECANCELED
			{ ECANCELED, "ECANCELED" },
		#endif
		#ifdef EIDRM
			{ EIDRM, "EIDRM" },
		#endif
		#ifdef ENOMSG
			{ ENOMSG, "ENOMSG" },
		#endif
		#ifdef EILSEQ
			{ EILSEQ, "EILSEQ" },
		#endif
		#ifdef ENOATTR
			{ ENOATTR, "ENOATTR" },
		#endif
		#ifdef EBADMSG
			{ EBADMSG, "EBADMSG" },
		#endif
		#ifdef EMULTIHOP
			{ EMULTIHOP, "EMULTIHOP" },
		#endif
		#ifdef ENODATA
			{ ENODATA, "ENODATA" },
		#endif
		#ifdef ENOLINK
			{ ENOLINK, "ENOLINK" },
		#endif
		#ifdef ENOSR
			{ ENOSR, "ENOSR" },
		#endif
		#ifdef ENOSTR
			{ ENOSTR, "ENOSTR" },
		#endif
		#ifdef EPROTO
			{ EPROTO, "EPROTO" },
		#endif
		#ifdef ETIME
			{ ETIME, "ETIME" },
		#endif
		#ifdef EOPNOTSUPP
			{ EOPNOTSUPP, "EOPNOTSUPP" },
		#endif
		#ifdef ENOPOLICY
			{ ENOPOLICY, "ENOPOLICY" },
		#endif
		#ifdef ENOTRECOVERABLE
			{ ENOTRECOVERABLE, "ENOTRECOVERABLE" },
		#endif
		#ifdef EOWNERDEAD
			{ EOWNERDEAD, "EOWNERDEAD" },
		#endif
	};

	for (unsigned int i = 0; i < sizeof(entries) / sizeof(Entry); i++) {
		if (strcmp(entries[i].name, name) == 0) {
			return entries[i].errorCode;
		}
	}
	return -1;
}

static void
initializeSyscallFailureSimulation(const char *processName) {
	// Format:
	// PassengerAgent watchdog=EMFILE:0.1,ECONNREFUSED:0.25;PassengerAgent core=ESPIPE=0.4
	const char *spec = getenv("PASSENGER_SIMULATE_SYSCALL_FAILURES");
	string prefix = string(processName) + "=";
	vector<string> components;
	unsigned int i;

	// Lookup this process in the specification string.
	split(spec, ';', components);
	for (i = 0; i < components.size(); i++) {
		if (startsWith(components[i], prefix)) {
			// Found!
			string value = components[i].substr(prefix.size());
			split(value, ',', components);
			vector<string> keyAndValue;
			vector<ErrorChance> chances;

			// Process each errorCode:chance pair.
			for (i = 0; i < components.size(); i++) {
				split(components[i], ':', keyAndValue);
				if (keyAndValue.size() != 2) {
					fprintf(stderr, "%s: invalid syntax in PASSENGER_SIMULATE_SYSCALL_FAILURES: '%s'\n",
						processName, components[i].c_str());
					continue;
				}

				int e = lookupErrno(keyAndValue[0].c_str());
				if (e == -1) {
					fprintf(stderr, "%s: invalid error code in PASSENGER_SIMULATE_SYSCALL_FAILURES: '%s'\n",
						processName, components[i].c_str());
					continue;
				}

				ErrorChance chance;
				chance.chance = atof(keyAndValue[1].c_str());
				if (chance.chance < 0 || chance.chance > 1) {
					fprintf(stderr, "%s: invalid chance PASSENGER_SIMULATE_SYSCALL_FAILURES: '%s' - chance must be between 0 and 1\n",
						processName, components[i].c_str());
					continue;
				}
				chance.errorCode = e;
				chances.push_back(chance);
			}

			// Install the chances.
			setup_random_failure_simulation(&chances[0], chances.size());
			return;
		}
	}
}

static bool isBlank(const char *str) {
	while (*str != '\0') {
		if (*str != ' ') {
			return false;
		}
		str++;
	}
	return true;
}

static bool
extraArgumentsPassed(int argc, char *argv[], int argStartIndex) {
	assert(argc >= argStartIndex);
	return argc > argStartIndex + 1
		// Allow the Watchdog to pass an all-whitespace argument. This
		// argument provides the memory space for us to change the process title.
		|| (argc == argStartIndex + 1 && !isBlank(argv[argStartIndex]));
}

VariantMap
initializeAgent(int argc, char **argv[], const char *processName,
	OptionParserFunc optionParser, PreinitializationFunc preinit,
	int argStartIndex)
{
	VariantMap options;
	const char *seedStr;

	seedStr = getEnvString("PASSENGER_RANDOM_SEED");
	if (seedStr == NULL) {
		randomSeed = (unsigned int) time(NULL);
	} else {
		randomSeed = (unsigned int) atoll(seedStr);
	}
	srand(randomSeed);
	srandom(randomSeed);

	ignoreSigpipe();
	if (hasEnvOption("PASSENGER_ABORT_HANDLER", true)) {
		shouldDumpWithCrashWatch = hasEnvOption("PASSENGER_DUMP_WITH_CRASH_WATCH", true);
		beepOnAbort = hasEnvOption("PASSENGER_BEEP_ON_ABORT");
		stopOnAbort = hasEnvOption("PASSENGER_STOP_ON_ABORT");
		IGNORE_SYSCALL_RESULT(pipe(emergencyPipe1));
		IGNORE_SYSCALL_RESULT(pipe(emergencyPipe2));
		installAgentAbortHandler();
	}
	oxt::initialize();
	setup_syscall_interruption_support();
	if (hasEnvOption("PASSENGER_SIMULATE_SYSCALL_FAILURES")) {
		initializeSyscallFailureSimulation(processName);
	}
	SystemTime::initialize();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	TRACE_POINT();
	try {
		if (hasEnvOption("PASSENGER_USE_FEEDBACK_FD")) {
			if (extraArgumentsPassed(argc, *argv, argStartIndex)) {
				fprintf(stderr, "No arguments may be passed when using the feedback FD.\n");
				exit(1);
			}
			_feedbackFdAvailable = true;
			options.readFrom(FEEDBACK_FD);
		} else if (optionParser != NULL) {
			optionParser(argc, (const char **) *argv, options);
		} else {
			options.readFrom((const char **) *argv + argStartIndex,
				argc - argStartIndex);
		}

		initializeAgentOptions(processName, options, preinit);
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		exit(1);
	}

	// Make a copy of the arguments before changing process title.
	origArgv = (char **) malloc(argc * sizeof(char *));
	for (int i = 0; i < argc; i++) {
		origArgv[i] = strdup((*argv)[i]);
	}

	// Change process title.
	size_t totalArgLen = strlen((*argv)[0]);
	for (int i = 1; i < argc; i++) {
		size_t len = strlen((*argv)[i]);
		totalArgLen += len + 1;
		memset((*argv)[i], '\0', len);
	}
	strncpy((*argv)[0], processName, totalArgLen);
	*argv = origArgv;

	P_DEBUG("Random seed: " << randomSeed);

	return options;
}

void
initializeAgentOptions(const char *processName, VariantMap &options,
	PreinitializationFunc preinit)
{
	ResourceLocator locator;
	string ruby;

	if (options.has("passenger_root")) {
		string path;
		locator = ResourceLocator(options.get("passenger_root", true));
		ruby = options.get("default_ruby", false, DEFAULT_RUBY);

		rubyLibDir    = strdup(locator.getRubyLibDir().c_str());
		passengerRoot = strdup(options.get("passenger_root", true).c_str());
		defaultRuby   = strdup(ruby.c_str());

		#ifdef __linux__
			path = ruby + " \"" + locator.getHelperScriptsDir() +
				"/backtrace-sanitizer.rb\"";
			backtraceSanitizerCommand = strdup(path.c_str());
		#endif

		path = locator.getHelperScriptsDir() + "/crash-watch.rb";
		crashWatch = strdup(path.c_str());
	} else {
		shouldDumpWithCrashWatch = false;
	}

	if (backtraceSanitizerCommand == NULL) {
		backtraceSanitizerCommand = "c++filt -n";
		backtraceSanitizerPassProgramInfo = false;
	}

	if (preinit != NULL) {
		preinit(options);
	}

	options.setDefaultInt("log_level", DEFAULT_LOG_LEVEL);
	setLogLevel(options.getInt("log_level"));
	string logFile;
	if (options.has("log_file")) {
		logFile = options.get("log_file");
	} else if (options.has("debug_log_file")) {
		logFile = options.get("debug_log_file");
	}
	if (!logFile.empty()) {
		try {
			logFile = absolutizePath(logFile);
		} catch (const SystemException &e) {
			P_WARN("Cannot absolutize filename '" << logFile
				<< "': " << e.what());
		}
		setLogFile(logFile);
	}

	if (options.has("file_descriptor_log_file")) {
		logFile = options.get("file_descriptor_log_file");
		try {
			logFile = absolutizePath(logFile);
		} catch (const SystemException &e) {
			P_WARN("Cannot absolutize filename '" << logFile
				<< "': " << e.what());
		}
		setFileDescriptorLogFile(logFile);

		// This information helps dev/parse_file_descriptor_log.
		FastStringStream<> stream;
		_prepareLogEntry(stream, __FILE__, __LINE__);
		stream << "Starting agent: " << processName << "\n";
		_writeFileDescriptorLogEntry(stream.data(), stream.size());

		P_LOG_FILE_DESCRIPTOR_OPEN4(getFileDescriptorLogFileFd(), __FILE__, __LINE__,
			"file descriptor log file " << options.get("file_descriptor_log_file"));
	} else {
		// This information helps dev/parse_file_descriptor_log.
		P_DEBUG("Starting agent: " << processName);
	}

	if (hasEnvOption("PASSENGER_USE_FEEDBACK_FD")) {
		P_LOG_FILE_DESCRIPTOR_OPEN2(FEEDBACK_FD, "feedback FD");
	}
	if (emergencyPipe1[0] != -1) {
		P_LOG_FILE_DESCRIPTOR_OPEN4(emergencyPipe1[0], __FILE__, __LINE__,
			"Emergency pipe 1-0");
		P_LOG_FILE_DESCRIPTOR_OPEN4(emergencyPipe1[1], __FILE__, __LINE__,
			"Emergency pipe 1-1");
		P_LOG_FILE_DESCRIPTOR_OPEN4(emergencyPipe2[0], __FILE__, __LINE__,
			"Emergency pipe 2-0");
		P_LOG_FILE_DESCRIPTOR_OPEN4(emergencyPipe2[1], __FILE__, __LINE__,
			"Emergency pipe 2-1");
	}
}

void
shutdownAgent(VariantMap *agentOptions) {
	delete agentOptions;
	oxt::shutdown();
}

/**
 * Linux-only way to change OOM killer configuration for
 * current process. Requires root privileges, which we
 * should have.
 */
void
restoreOomScore(VariantMap *agentOptions) {
	TRACE_POINT();

	string score = agentOptions->get("original_oom_score", false);
	if (score.empty()) {
		return;
	}

	FILE *f;
	bool legacy = false;

	if (score.at(0) == 'l') {
		legacy = true;
		score = score.substr(1);
		f = fopen("/proc/self/oom_adj", "w");
	} else {
		f = fopen("/proc/self/oom_score_adj", "w");
	}

	if (f != NULL) {
		fprintf(f, "%s\n", score.c_str());
		fclose(f);
	} else {
		P_WARN("Unable to set OOM score to " << score << " (legacy: " << legacy
				<< ") due to error: " << strerror(errno)
				<< " (process will remain at inherited OOM score)");
	}
}

} // namespace Passenger
