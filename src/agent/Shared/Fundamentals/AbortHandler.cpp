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

#include <Shared/Fundamentals/AbortHandler.h>

#include <boost/cstdint.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>

#ifdef __linux__
	#include <sys/syscall.h>
	#include <features.h>
#endif
#if defined(__APPLE__) || defined(__GNU_LIBRARY__)
	#define LIBC_HAS_BACKTRACE_FUNC
#endif
#ifdef LIBC_HAS_BACKTRACE_FUNC
	#include <execinfo.h>
#endif

#include <Shared/Fundamentals/AbortHandler.h>
#include <Shared/Fundamentals/Utils.h>
#include <Constants.h>
#include <LoggingKit/LoggingKit.h>
#include <LoggingKit/Context.h>
#include <ResourceLocator.h>
#include <ProcessManagement/Utils.h>
#include <Utils.h>

namespace Passenger {
namespace Agent {
namespace Fundamentals {

using namespace std;


struct AbortHandlerContext {
	const AbortHandlerConfig *config;
	char *installSpec;
	char *rubyLibDir;
	char *crashWatchCommand;
	char *backtraceSanitizerCommand;
	bool backtraceSanitizerPassProgramInfo;

	int emergencyPipe1[2];
	int emergencyPipe2[2];

	char *alternativeStack;

	volatile sig_atomic_t callCount;
};

struct AbortHandlerWorkingState {
	pid_t pid;
	int signo;
	siginfo_t *info;
	char messagePrefix[32];
	char messageBuf[1024];
};

typedef void (*Callback)(AbortHandlerWorkingState &state, void *userData);


#define IGNORE_SYSCALL_RESULT(code) \
	do { \
		int _ret = code; \
		(void) _ret; \
	} while (false)


static AbortHandlerContext *ctx = NULL;
static const char digits[] = "0123456789";
static const char hex_chars[] = "0123456789abcdef";


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
	return appendIntegerAsHex(appendText(buf, "0x"), (boost::uintptr_t) pointer);
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
runInSubprocessWithTimeLimit(AbortHandlerWorkingState &state, Callback callback, void *userData, int timeLimit) {
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
dumpFileDescriptorInfoWithLsof(AbortHandlerWorkingState &state, void *userData) {
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
dumpFileDescriptorInfoWithLs(AbortHandlerWorkingState &state, char *end) {
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
dumpFileDescriptorInfo(AbortHandlerWorkingState &state) {
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
dumpWithCrashWatch(AbortHandlerWorkingState &state) {
	char *messageBuf = state.messageBuf;
	const char *pidStr = messageBuf;
	char *end = messageBuf;
	end = appendULL(end, (unsigned long long) state.pid);
	*end = '\0';

	pid_t child = asyncFork();
	if (child == 0) {
		closeAllFileDescriptors(2, true);
		execlp(ctx->config->ruby, ctx->config->ruby, ctx->crashWatchCommand,
			ctx->rubyLibDir, ctx->installSpec, "--dump", pidStr, (char * const) 0);
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
	dumpBacktrace(AbortHandlerWorkingState &state, void *userData) {
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

		if (ctx->backtraceSanitizerCommand != NULL) {
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
				end = appendText(end, ctx->backtraceSanitizerCommand);
				if (ctx->backtraceSanitizerPassProgramInfo) {
					end = appendText(end, " \"");
					end = appendText(end, ctx->config->origArgv[0]);
					end = appendText(end, "\" ");
					end = appendText(end, pidStr);
				}
				*end = '\0';
				end++;
				execlp("/bin/sh", "/bin/sh", "-c", command, (const char * const) 0);

				end = state.messageBuf;
				end = appendText(end, "ERROR: cannot execute '");
				end = appendText(end, ctx->backtraceSanitizerCommand);
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
					end = appendText(end, ctx->backtraceSanitizerCommand);
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
runCustomDiagnosticsDumper(AbortHandlerWorkingState &state, void *userData) {
	ctx->config->diagnosticsDumper(ctx->config->diagnosticsDumperUserData);
}

// This function is performed in a child process.
static void
dumpDiagnostics(AbortHandlerWorkingState &state) {
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

	if (LoggingKit::lastAssertionFailure.filename != NULL) {
		end = messageBuf;
		end = appendText(end, state.messagePrefix);
		end = appendText(end, " ] Last assertion failure: (");
		end = appendText(end, LoggingKit::lastAssertionFailure.expression);
		end = appendText(end, "), ");
		if (LoggingKit::lastAssertionFailure.function != NULL) {
			end = appendText(end, "function ");
			end = appendText(end, LoggingKit::lastAssertionFailure.function);
			end = appendText(end, ", ");
		}
		end = appendText(end, "file ");
		end = appendText(end, LoggingKit::lastAssertionFailure.filename);
		end = appendText(end, ", line ");
		end = appendULL(end, LoggingKit::lastAssertionFailure.line);
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

	if (ctx->config->diagnosticsDumper != NULL) {
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

	if (ctx->config->dumpWithCrashWatch && ctx->crashWatchCommand != NULL) {
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
closeEmergencyPipes() {
	if (ctx->emergencyPipe1[0] != -1) {
		close(ctx->emergencyPipe1[0]);
	}
	if (ctx->emergencyPipe1[1] != -1) {
		close(ctx->emergencyPipe1[1]);
	}
	if (ctx->emergencyPipe2[0] != -1) {
		close(ctx->emergencyPipe2[0]);
	}
	if (ctx->emergencyPipe2[1] != -1) {
		close(ctx->emergencyPipe2[1]);
	}
	ctx->emergencyPipe1[0] = ctx->emergencyPipe1[1] = -1;
	ctx->emergencyPipe2[0] = ctx->emergencyPipe2[1] = -1;
}

static void
abortHandler(int signo, siginfo_t *info, void *_unused) {
	AbortHandlerWorkingState state;

	state.pid = getpid();
	state.signo = signo;
	state.info = info;
	pid_t child;
	time_t t = time(NULL);
	char crashLogFile[256];

	ctx->callCount++;
	if (ctx->callCount > 1) {
		// The abort handler itself crashed!
		char *end = state.messageBuf;
		end = appendText(end, "[ origpid=");
		end = appendULL(end, (unsigned long long) state.pid);
		end = appendText(end, ", pid=");
		end = appendULL(end, (unsigned long long) getpid());
		end = appendText(end, ", timestamp=");
		end = appendULL(end, (unsigned long long) t);
		if (ctx->callCount == 2) {
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

	closeEmergencyPipes();

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
	end = appendULL(end, (unsigned long long) ctx->config->randomSeed);
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

	if (ctx->config->beep) {
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

	if (ctx->config->stopProcess) {
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

void
installAbortHandler(const AbortHandlerConfig *config) {
	ctx = new AbortHandlerContext();
	memset(ctx, 0, sizeof(AbortHandlerContext));

	ctx->config = config;
	ctx->backtraceSanitizerPassProgramInfo = true;
	ctx->emergencyPipe1[0] = -1;
	ctx->emergencyPipe1[1] = -1;
	ctx->emergencyPipe2[0] = -1;
	ctx->emergencyPipe2[1] = -1;

	abortHandlerConfigChanged();

	IGNORE_SYSCALL_RESULT(pipe(ctx->emergencyPipe1));
	IGNORE_SYSCALL_RESULT(pipe(ctx->emergencyPipe2));

	size_t alternativeStackSize = MINSIGSTKSZ + 128 * 1024;
	ctx->alternativeStack = (char *) malloc(alternativeStackSize);
	if (ctx->alternativeStack == NULL) {
		fprintf(stderr, "Cannot allocate an alternative stack with a size of %lu bytes!\n",
			(unsigned long) alternativeStackSize);
		fflush(stderr);
		abort();
	}

	stack_t stack;
	stack.ss_sp = ctx->alternativeStack;
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

bool
abortHandlerInstalled() {
	return ctx != NULL;
}

void
abortHandlerLogFds() {
	if (ctx->emergencyPipe1[0] != -1) {
		P_LOG_FILE_DESCRIPTOR_OPEN4(ctx->emergencyPipe1[0], __FILE__, __LINE__,
			"Emergency pipe 1-0");
		P_LOG_FILE_DESCRIPTOR_OPEN4(ctx->emergencyPipe1[1], __FILE__, __LINE__,
			"Emergency pipe 1-1");
	}
	if (ctx->emergencyPipe2[0] != -1) {
		P_LOG_FILE_DESCRIPTOR_OPEN4(ctx->emergencyPipe2[0], __FILE__, __LINE__,
			"Emergency pipe 2-0");
		P_LOG_FILE_DESCRIPTOR_OPEN4(ctx->emergencyPipe2[1], __FILE__, __LINE__,
			"Emergency pipe 2-1");
	}
}

static void
useCxxFiltAsBacktraceSanitizer() {
	ctx->backtraceSanitizerCommand = strdup("c++filt -n");
	ctx->backtraceSanitizerPassProgramInfo = false;
}

void
abortHandlerConfigChanged() {
	const AbortHandlerConfig *config = ctx->config;
	char *oldInstallSpec = ctx->installSpec;
	char *oldRubyLibDir = ctx->rubyLibDir;
	char *oldCrashWatchCommand = ctx->crashWatchCommand;
	char *oldBacktraceSanitizerCommand = ctx->backtraceSanitizerCommand;

	if (config->resourceLocator != NULL) {
		string path;
		const ResourceLocator *locator = config->resourceLocator;

		ctx->installSpec = strdup(locator->getInstallSpec().c_str());
		ctx->rubyLibDir = strdup(locator->getRubyLibDir().c_str());

		path = locator->getHelperScriptsDir() + "/crash-watch.rb";
		ctx->crashWatchCommand = strdup(path.c_str());

		if (ctx->installSpec == NULL || ctx->rubyLibDir == NULL || ctx->crashWatchCommand == NULL) {
			fprintf(stderr, "Cannot allocate memory for abort handler!\n");
			fflush(stderr);
			abort();
		}

		#ifdef __linux__
			path = StaticString(config->ruby) + " \""
				+ locator->getHelperScriptsDir() +
				"/backtrace-sanitizer.rb\"";
			ctx->backtraceSanitizerCommand = strdup(path.c_str());
			ctx->backtraceSanitizerPassProgramInfo = true;
			if (ctx->backtraceSanitizerCommand == NULL) {
				fprintf(stderr, "Cannot allocate memory for abort handler!\n");
				fflush(stderr);
				abort();
			}
		#else
			useCxxFiltAsBacktraceSanitizer();
		#endif
	} else {
		ctx->installSpec = NULL;
		ctx->rubyLibDir = NULL;
		ctx->crashWatchCommand = NULL;
		useCxxFiltAsBacktraceSanitizer();
	}

	free(oldInstallSpec);
	free(oldRubyLibDir);
	free(oldCrashWatchCommand);
	free(oldBacktraceSanitizerCommand);
}

void
shutdownAbortHandler() {
	free(ctx->installSpec);
	free(ctx->rubyLibDir);
	free(ctx->crashWatchCommand);
	free(ctx->backtraceSanitizerCommand);
	free(ctx->alternativeStack);
	closeEmergencyPipes();
	delete ctx;
	ctx = NULL;
}


} // namespace Fundamentals
} // namespace Agent
} // namespace Passenger


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
		using namespace Passenger;

		LoggingKit::lastAssertionFailure.filename = __file;
		LoggingKit::lastAssertionFailure.line = __line;
		LoggingKit::lastAssertionFailure.function = __function;
		LoggingKit::lastAssertionFailure.expression = __assertion;
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
		using namespace Passenger;

		LoggingKit::lastAssertionFailure.filename = file;
		LoggingKit::lastAssertionFailure.line = line;
		LoggingKit::lastAssertionFailure.function = func;
		LoggingKit::lastAssertionFailure.expression = expr;
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
