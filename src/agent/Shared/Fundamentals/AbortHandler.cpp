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
#include <RandomGenerator.h>
#include <ProcessManagement/Utils.h>
#include <Utils.h>
#include <Utils/AsyncSignalSafeUtils.h>

namespace Passenger {
namespace Agent {
namespace Fundamentals {

using namespace std;
namespace ASSU = AsyncSignalSafeUtils;

#define RANDOM_TOKEN_SIZE 6
#define MAX_RANDOM_TOKENS 256


struct AbortHandlerContext {
	const AbortHandlerConfig *config;
	char *installSpec;
	char *rubyLibDir;
	char *tmpDir;
	char *crashWatchCommand;
	char *backtraceSanitizerCommand;
	bool backtraceSanitizerPassProgramInfo;

	/**
	 * A string of RANDOM_TOKEN_SIZE * MAX_RANDOM_SIZES bytes.
	 * Used by createCrashLogDir() to find a unique directory name.
	 */
	char *randomTokens;

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

	char crashLogDir[256];
	int crashLogDirFd;
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


static void
write_nowarn(int fd, const void *buf, size_t n) {
	ASSU::writeNoWarn(fd, buf, n);
}

static void
printCrashLogFileCreated(AbortHandlerWorkingState &state, const char *fname) {
	const char *end = state.messageBuf + sizeof(state.messageBuf);
	char *pos = state.messageBuf;
	pos = ASSU::appendData(pos, end, "Dumping to ");
	pos = ASSU::appendData(pos, end, state.crashLogDir);
	pos = ASSU::appendData(pos, end, "/");
	pos = ASSU::appendData(pos, end, fname);
	pos = ASSU::appendData(pos, end, "\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
}

static void
printCrashLogFileCreationError(AbortHandlerWorkingState &state, const char *fname, int e) {
	const char *end = state.messageBuf + sizeof(state.messageBuf);
	char *pos = state.messageBuf;
	pos = ASSU::appendData(pos, end, "Error creating ");
	pos = ASSU::appendData(pos, end, state.crashLogDir);
	pos = ASSU::appendData(pos, end, "/");
	pos = ASSU::appendData(pos, end, fname);
	pos = ASSU::appendData(pos, end, ": ");
	pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
	pos = ASSU::appendData(pos, end, " (errno=");
	pos = ASSU::appendInteger<int, 10>(pos, end, e);
	pos = ASSU::appendData(pos, end, ")\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
}

static char *
appendSignalName(char *pos, const char *end, int signo) {
	switch (signo) {
	case SIGABRT:
		pos = ASSU::appendData(pos, end, "SIGABRT");
		break;
	case SIGSEGV:
		pos = ASSU::appendData(pos, end, "SIGSEGV");
		break;
	case SIGBUS:
		pos = ASSU::appendData(pos, end, "SIGBUS");
		break;
	case SIGFPE:
		pos = ASSU::appendData(pos, end, "SIGFPE");
		break;
	case SIGILL:
		pos = ASSU::appendData(pos, end, "SIGILL");
		break;
	default:
		return ASSU::appendInteger<int, 10>(pos, end, signo);
	}
	pos = ASSU::appendData(pos, end, "(");
	pos = ASSU::appendInteger<int, 10>(pos, end, signo);
	pos = ASSU::appendData(pos, end, ")");
	return pos;
}

#define SI_CODE_HANDLER(name) \
	case name: \
		buf = ASSU::appendData(buf, end, #name); \
		break

// Must be async signal safe.
static char *
appendSignalReason(char *buf, const char *end, siginfo_t *info) {
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
			buf = ASSU::appendData(buf, end, "#");
			buf = ASSU::appendInteger<int, 10>(buf, end, info->si_code);
		}
		break;
	}

	if (info->si_code <= 0) {
		buf = ASSU::appendData(buf, end, ", signal sent by PID ");
		buf = ASSU::appendInteger<pid_t, 10>(buf, end, info->si_pid);
		buf = ASSU::appendData(buf, end, " with UID ");
		buf = ASSU::appendInteger<uid_t, 10>(buf, end, info->si_uid);
	}

	buf = ASSU::appendData(buf, end, ", si_addr=0x");
	buf = ASSU::appendInteger<boost::uintptr_t, 16>(buf, end, (boost::uintptr_t) info->si_addr);

	return buf;
}

static int
runInSubprocessWithTimeLimit(AbortHandlerWorkingState &state, Callback callback, void *userData, int timeLimit) {
	char *pos;
	const char *end = state.messageBuf + sizeof(state.messageBuf);
	pid_t child;
	int p[2], e;

	if (pipe(p) == -1) {
		e = errno;
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, "Could not create subprocess: pipe() failed: ");
		pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
		pos = ASSU::appendData(pos, end, " (errno=");
		pos = ASSU::appendInteger<int, 10>(pos, end, e);
		pos = ASSU::appendData(pos, end, ")\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
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
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, "Could not create subprocess: fork() failed: ");
		pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
		pos = ASSU::appendData(pos, end, " (errno=");
		pos = ASSU::appendInteger<int, 10>(pos, end, e);
		pos = ASSU::appendData(pos, end, ")\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
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
			ASSU::printError("Could not run child process: it did not exit in time\n");
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
dumpUlimits(AbortHandlerWorkingState &state) {
	const char *end = state.messageBuf + sizeof(state.messageBuf);
	char *pos = state.messageBuf;
	pos = ASSU::appendData(pos, end, state.messagePrefix);
	pos = ASSU::appendData(pos, end, " ] Dumping ulimits...\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

	int fd = -1;
	if (state.crashLogDirFd != -1) {
		fd = openat(state.crashLogDirFd, "ulimits.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd != -1) {
			printCrashLogFileCreated(state, "ulimits.log");
		} else {
			printCrashLogFileCreationError(state, "ulimits.log", errno);
		}
	}

	pid_t pid = asyncFork();
	int status;
	if (pid == 0) {
		if (fd != -1) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
		}
		closeAllFileDescriptors(2, true);
		execlp("ulimit", "ulimit", "-a", (char *) 0);
		// On Linux 'ulimit' is a shell builtin, not a command.
		execlp("/bin/sh", "/bin/sh", "-c", "ulimit -a", (char *) 0);
		_exit(1);
	} else if (pid == -1) {
		ASSU::printError("ERROR: Could not fork a process to dump the ulimit!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		ASSU::printError("ERROR: Could not run 'ulimit -a'!\n");
	}

	if (fd != -1) {
		close(fd);
	}
}

static void
dumpFileDescriptorInfoWithLsof(AbortHandlerWorkingState &state, void *userData) {
	if (state.crashLogDirFd != -1) {
		int fd = openat(state.crashLogDirFd, "fds.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd != -1) {
			printCrashLogFileCreated(state, "fds.log");
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			close(fd);
		} else {
			printCrashLogFileCreationError(state, "fds.log", errno);
		}
	}

	char *pos = state.messageBuf;
	const char *end = state.messageBuf + sizeof(state.messageBuf) - 1;
	pos = ASSU::appendInteger<pid_t, 10>(pos, end, state.pid);
	*pos = '\0';

	closeAllFileDescriptors(2, true);

	execlp("lsof", "lsof", "-p", state.messageBuf, "-nP", (char *) 0);

	const char *command[] = { "lsof", NULL };
	printExecError2(command, errno, state.messageBuf, sizeof(state.messageBuf));
	_exit(1);
}

static void
dumpFileDescriptorInfoWithLs(AbortHandlerWorkingState &state, const char *path) {
	pid_t pid;
	int fd = -1;
	int status;

	if (state.crashLogDirFd != -1) {
		fd = openat(state.crashLogDirFd, "fds.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd != -1) {
			printCrashLogFileCreated(state, "fds.log");
		} else {
			printCrashLogFileCreationError(state, "fds.log", errno);
		}
	}

	pid = asyncFork();
	if (pid == 0) {
		if (fd != -1) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
		}

		const char *end = state.messageBuf + sizeof(state.messageBuf);
		char *pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, "Running: ls -lv ");
		pos = ASSU::appendData(pos, end, path);
		pos = ASSU::appendData(pos, end, "\n");
		pos = ASSU::appendData(pos, end, "--------------------------\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

		closeAllFileDescriptors(2, true);
		// The '-v' is for natural sorting on Linux. On BSD -v means something else but it's harmless.
		execlp("ls", "ls", "-lv", path, (char *) 0);

		const char *command[] = { "ls", NULL };
		printExecError2(command, errno, state.messageBuf, sizeof(state.messageBuf));
		_exit(1);
	} else if (pid == -1) {
		ASSU::printError("ERROR: Could not fork a process to dump file descriptor information!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		ASSU::printError("ERROR: Could not run 'ls' to dump file descriptor information!\n");
	}

	if (fd != -1) {
		close(fd);
	}
}

static void
dumpFileDescriptorInfo(AbortHandlerWorkingState &state) {
	char *messageBuf = state.messageBuf;
	char *pos;
	const char *end = state.messageBuf + sizeof(state.messageBuf) - 1;
	struct stat buf;
	int status;

	pos = messageBuf;
	pos = ASSU::appendData(pos, end, state.messagePrefix);
	pos = ASSU::appendData(pos, end, " ] Open files and file descriptors:\n");
	write_nowarn(STDERR_FILENO, messageBuf, pos - messageBuf);

	status = runInSubprocessWithTimeLimit(state, dumpFileDescriptorInfoWithLsof, NULL, 4000);

	if (status != 0) {
		char path[256];
		ASSU::printError("Falling back to another mechanism for dumping file descriptors.\n");

		pos = path;
		end = path + sizeof(path) - 1;
		pos = ASSU::appendData(pos, end, "/proc/");
		pos = ASSU::appendInteger<pid_t, 10>(pos, end, state.pid);
		pos = ASSU::appendData(pos, end, "/fd");
		*pos = '\0';
		if (stat(path, &buf) == 0) {
			dumpFileDescriptorInfoWithLs(state, path);
			return;
		}

		pos = path;
		pos = ASSU::appendData(pos, end, "/dev/fd");
		*pos = '\0';
		if (stat(path, &buf) == 0) {
			dumpFileDescriptorInfoWithLs(state, path);
			return;
		}

		pos = messageBuf;
		pos = ASSU::appendData(pos, end, "ERROR: No other file descriptor dumping mechanism on current platform detected.\n");
		write_nowarn(STDERR_FILENO, messageBuf, pos - messageBuf);
	}
}

static void
dumpWithCrashWatch(AbortHandlerWorkingState &state) {
	int fd = -1;

	if (state.crashLogDirFd != -1) {
		fd = openat(state.crashLogDirFd, "backtrace.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd != -1) {
			printCrashLogFileCreated(state, "backtrace.log");
		} else {
			printCrashLogFileCreationError(state, "backtrace.log", errno);
		}
	}

	char *pos = state.messageBuf;
	const char *end = state.messageBuf + sizeof(state.messageBuf) - 1;
	pos = ASSU::appendInteger<pid_t, 10>(pos, end, state.pid);
	*pos = '\0';

	pid_t child = asyncFork();
	if (child == 0) {
		if (fd != -1) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
		}
		closeAllFileDescriptors(2, true);
		execlp(ctx->config->ruby, ctx->config->ruby, ctx->crashWatchCommand,
			ctx->rubyLibDir, ctx->installSpec, "--dump",
			state.messageBuf, // PID string
			(char *) 0);

		const char *command[] = { "crash-watch", NULL };
		printExecError2(command, errno, state.messageBuf, sizeof(state.messageBuf));
		_exit(1);

	} else if (child == -1) {
		int e = errno;
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, "Could not execute crash-watch: fork() failed: ");
		pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
		pos = ASSU::appendData(pos, end, " (errno=");
		pos = ASSU::appendInteger<int, 10>(pos, end, e);
		pos = ASSU::appendData(pos, end, ")\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

	} else {
		waitpid(child, NULL, 0);
	}

	if (fd != -1) {
		close(fd);
	}
}

#ifdef LIBC_HAS_BACKTRACE_FUNC
	static void
	dumpBacktrace(AbortHandlerWorkingState &state, void *userData) {
		void *backtraceStore[512];
		int frames = backtrace(backtraceStore, sizeof(backtraceStore) / sizeof(void *));
		char *pos;
		const char *end = state.messageBuf + sizeof(state.messageBuf) - 1;

		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, "--------------------------------------\n");
		pos = ASSU::appendData(pos, end, "[ pid=");
		pos = ASSU::appendInteger<pid_t, 10>(pos, end, state.pid);
		pos = ASSU::appendData(pos, end, " ] Backtrace with ");
		pos = ASSU::appendInteger<int, 10>(pos, end, frames);
		pos = ASSU::appendData(pos, end, " frames:\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

		if (ctx->backtraceSanitizerCommand != NULL) {
			int p[2];
			if (pipe(p) == -1) {
				int e = errno;
				pos = state.messageBuf;
				pos = ASSU::appendData(pos, end, "Could not dump diagnostics through backtrace sanitizer: pipe() failed with errno=");
				pos = ASSU::appendInteger<int, 10>(pos, end, e);
				pos = ASSU::appendData(pos, end, "\n");
				pos = ASSU::appendData(pos, end, "Falling back to writing to stderr directly...\n");
				write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
				backtrace_symbols_fd(backtraceStore, frames, STDERR_FILENO);
				return;
			}

			pid_t pid = asyncFork();
			if (pid == 0) {
				const char *pidStr = pos = state.messageBuf;
				pos = ASSU::appendInteger<pid_t, 10>(pos, end, state.pid);
				*pos = '\0';
				pos++;

				close(p[1]);
				dup2(p[0], STDIN_FILENO);
				closeAllFileDescriptors(2, true);

				const char *command = pos;
				pos = ASSU::appendData(pos, end, "exec ");
				pos = ASSU::appendData(pos, end, ctx->backtraceSanitizerCommand);
				if (ctx->backtraceSanitizerPassProgramInfo) {
					pos = ASSU::appendData(pos, end, " \"");
					pos = ASSU::appendData(pos, end, ctx->config->origArgv[0]);
					pos = ASSU::appendData(pos, end, "\" ");
					pos = ASSU::appendData(pos, end, pidStr);
				}
				*pos = '\0';
				pos++;
				execlp("/bin/sh", "/bin/sh", "-c", command, (char *) 0);

				pos = state.messageBuf;
				pos = ASSU::appendData(pos, end, "ERROR: cannot execute '");
				pos = ASSU::appendData(pos, end, ctx->backtraceSanitizerCommand);
				pos = ASSU::appendData(pos, end, "' for sanitizing the backtrace, trying 'cat'...\n");
				write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
				execlp("cat", "cat", (char *) 0);
				execlp("/bin/cat", "cat", (char *) 0);
				execlp("/usr/bin/cat", "cat", (char *) 0);

				const char *commandArray[] = { "cat", NULL };
				printExecError2(commandArray, errno, state.messageBuf, sizeof(state.messageBuf));
				_exit(1);

			} else if (pid == -1) {
				close(p[0]);
				close(p[1]);
				int e = errno;
				pos = state.messageBuf;
				pos = ASSU::appendData(pos, end, "Could not dump diagnostics through backtrace sanitizer: fork() failed: ");
				pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
				pos = ASSU::appendData(pos, end, " (errno=");
				pos = ASSU::appendInteger<int, 10>(pos, end, e);
				pos = ASSU::appendData(pos, end, ")\n");
				pos = ASSU::appendData(pos, end, "Falling back to writing to stderr directly...\n");
				write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
				backtrace_symbols_fd(backtraceStore, frames, STDERR_FILENO);

			} else {
				int status = -1;

				close(p[0]);
				backtrace_symbols_fd(backtraceStore, frames, p[1]);
				close(p[1]);
				if (waitpid(pid, &status, 0) == -1 || status != 0) {
					pos = state.messageBuf;
					pos = ASSU::appendData(pos, end, "ERROR: cannot execute '");
					pos = ASSU::appendData(pos, end, ctx->backtraceSanitizerCommand);
					pos = ASSU::appendData(pos, end, "' for sanitizing the backtrace, writing to stderr directly...\n");
					write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
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
	unsigned int i = static_cast<unsigned int>(reinterpret_cast<boost::uintptr_t>(userData));
	const AbortHandlerConfig::DiagnosticsDumper &dumper = ctx->config->diagnosticsDumpers[i];

	if (state.crashLogDirFd != -1) {
		int fd = openat(state.crashLogDirFd, dumper.logFileName, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd != -1) {
			printCrashLogFileCreated(state, dumper.logFileName);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			close(fd);
		} else {
			printCrashLogFileCreationError(state, dumper.logFileName, errno);
		}
	}

	dumper.func(dumper.userData);
}

// This function is performed in a child process.
static void
dumpDiagnostics(AbortHandlerWorkingState &state) {
	char *pos;
	const char *end = state.messageBuf + sizeof(state.messageBuf);
	pid_t pid;
	int status;

	pos = state.messageBuf;
	pos = ASSU::appendData(pos, end, state.messagePrefix);
	pos = ASSU::appendData(pos, end, " ] Date and uname:\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

	// Dump human-readable time string and string.
	pid = asyncFork();
	if (pid == 0) {
		closeAllFileDescriptors(2, true);
		execlp("date", "date", (char *) 0);
		_exit(1);
	} else if (pid == -1) {
		ASSU::printError("ERROR: Could not fork a process to dump the time!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		ASSU::printError("ERROR: Could not run 'date'!\n");
	}

	// Dump system uname.
	pid = asyncFork();
	if (pid == 0) {
		closeAllFileDescriptors(2, true);
		execlp("uname", "uname", "-mprsv", (char *) 0);
		_exit(1);
	} else if (pid == -1) {
		ASSU::printError("ERROR: Could not fork a process to dump the uname!\n");
	} else if (waitpid(pid, &status, 0) != pid || status != 0) {
		ASSU::printError("ERROR: Could not run 'uname -mprsv'!\n");
	}

	pos = state.messageBuf;
	pos = ASSU::appendData(pos, end, state.messagePrefix);
	pos = ASSU::appendData(pos, end, " ] " PROGRAM_NAME " version: " PASSENGER_VERSION "\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

	if (LoggingKit::lastAssertionFailure.filename != NULL) {
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] Last assertion failure: (");
		pos = ASSU::appendData(pos, end, LoggingKit::lastAssertionFailure.expression);
		pos = ASSU::appendData(pos, end, "), ");
		if (LoggingKit::lastAssertionFailure.function != NULL) {
			pos = ASSU::appendData(pos, end, "function ");
			pos = ASSU::appendData(pos, end, LoggingKit::lastAssertionFailure.function);
			pos = ASSU::appendData(pos, end, ", ");
		}
		pos = ASSU::appendData(pos, end, "file ");
		pos = ASSU::appendData(pos, end, LoggingKit::lastAssertionFailure.filename);
		pos = ASSU::appendData(pos, end, ", line ");
		pos = ASSU::appendInteger<unsigned int, 10>(pos, end, LoggingKit::lastAssertionFailure.line);
		pos = ASSU::appendData(pos, end, ".\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
	}

	// It is important that writing the message and the backtrace are two
	// seperate operations because it's not entirely clear whether the
	// latter is async signal safe and thus can crash.
	pos = state.messageBuf;
	pos = ASSU::appendData(pos, end, state.messagePrefix);
	#ifdef LIBC_HAS_BACKTRACE_FUNC
		pos = ASSU::appendData(pos, end, " ] libc backtrace available!\n");
	#else
		pos = ASSU::appendData(pos, end, " ] libc backtrace not available.\n");
	#endif
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

	#ifdef LIBC_HAS_BACKTRACE_FUNC
		runInSubprocessWithTimeLimit(state, dumpBacktrace, NULL, 4000);
	#endif

	ASSU::printError("--------------------------------------\n");

	dumpUlimits(state);

	ASSU::printError("--------------------------------------\n");

	for (unsigned int i = 0; i < AbortHandlerConfig::MAX_DIAGNOSTICS_DUMPERS; i++) {
		const AbortHandlerConfig::DiagnosticsDumper &diagnosticsDumper = ctx->config->diagnosticsDumpers[i];
		if (diagnosticsDumper.func == NULL) {
			continue;
		}

		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] Dumping ");
		pos = ASSU::appendData(pos, end, diagnosticsDumper.name);
		pos = ASSU::appendData(pos, end, "...\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
		runInSubprocessWithTimeLimit(state, runCustomDiagnosticsDumper,
			reinterpret_cast<void *>(static_cast<boost::uintptr_t>(i)), 2000);
		ASSU::printError("--------------------------------------\n");
	}

	dumpFileDescriptorInfo(state);
	ASSU::printError("--------------------------------------\n");

	if (ctx->config->dumpWithCrashWatch && ctx->crashWatchCommand != NULL) {
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		#ifdef LIBC_HAS_BACKTRACE_FUNC
			pos = ASSU::appendData(pos, end, " ] Dumping a more detailed backtrace with crash-watch...\n");
		#else
			pos = ASSU::appendData(pos, end, " ] Dumping a backtrace with crash-watch...\n");
		#endif
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
		dumpWithCrashWatch(state);
	} else {
		write_nowarn(STDERR_FILENO, "\n", 1);
	}

	if (state.crashLogDir[0] != '\0') {
		ASSU::printError("--------------------------------------\n");
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] **************** LOOK HERE FOR CRASH DETAILS *****************\n\n");
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] Crash log dumped to this directory:\n");
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] ");
		pos = ASSU::appendData(pos, end, state.crashLogDir);
		pos = ASSU::appendData(pos, end, "\n\n");
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] **************** LOOK ABOVE FOR CRASH DETAILS ****************\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
	}
}

static bool
createCrashLogDir(AbortHandlerWorkingState &state, time_t t) {
	char *suffixBegin = state.crashLogDir;
	const char *end = state.crashLogDir + sizeof(state.crashLogDir) - 1;
	suffixBegin = ASSU::appendData(suffixBegin, end, "/var/tmp/passenger-crash-log.");
	suffixBegin = ASSU::appendInteger<time_t, 10>(suffixBegin, end, t);
	suffixBegin = ASSU::appendData(suffixBegin, end, ".");

	// Try a bunch of times to find and create a unique path.
	for (unsigned int i = 0; i < MAX_RANDOM_TOKENS; i++) {
		char *pos = suffixBegin;
		pos = ASSU::appendData(pos, end, ctx->randomTokens + RANDOM_TOKEN_SIZE * i,
			RANDOM_TOKEN_SIZE);
		*pos = '\0';

		int ret;
		do {
			ret = mkdir(state.crashLogDir, 0700);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			if (errno == EEXIST) {
				// Directory exists; try again.
				continue;
			} else {
				int e = errno;
				end = state.messageBuf + sizeof(state.messageBuf);
				pos = state.messageBuf;
				pos = ASSU::appendData(pos, end, state.messagePrefix);
				pos = ASSU::appendData(pos, end, " ] Error creating directory ");
				pos = ASSU::appendData(pos, end, state.crashLogDir);
				pos = ASSU::appendData(pos, end, " for storing crash log: ");
				pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
				pos = ASSU::appendData(pos, end, " (errno=");
				pos = ASSU::appendInteger<int, 10>(pos, end, e);
				pos = ASSU::appendData(pos, end, ")\n");
				write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
				state.crashLogDir[0] = '\0';
				return false;
			}
		}

		do {
			state.crashLogDirFd = open(state.crashLogDir, O_RDONLY);
		} while (state.crashLogDirFd == -1 && errno == EINTR);
		if (state.crashLogDirFd == -1) {
			int e = errno;
			end = state.messageBuf + sizeof(state.messageBuf);
			pos = state.messageBuf;
			pos = ASSU::appendData(pos, end, state.messagePrefix);
			pos = ASSU::appendData(pos, end, " ] Error opening created directory ");
			pos = ASSU::appendData(pos, end, state.crashLogDir);
			pos = ASSU::appendData(pos, end, " for storing crash log: ");
			pos = ASSU::appendData(pos, end, ASSU::limitedStrerror(e));
			pos = ASSU::appendData(pos, end, " (errno=");
			pos = ASSU::appendInteger<int, 10>(pos, end, e);
			pos = ASSU::appendData(pos, end, ")\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
			state.crashLogDir[0] = '\0';
			return false;
		}

		return true;
	}

	state.crashLogDir[0] = '\0';
	return false;
}

static bool
forkAndRedirectToTeeAndMainLogFile(const char *crashLogDir) {
	int p[2];
	if (pipe(p) == -1) {
		return false;
	}

	char filename[300];
	char *pos = filename;
	const char *end = filename + sizeof(filename) - 1;

	pos = ASSU::appendData(pos, end, crashLogDir);
	pos = ASSU::appendData(pos, end, "/");
	pos = ASSU::appendData(pos, end, "main.log");
	*pos = '\0';

	pid_t pid = asyncFork();
	if (pid == 0) {
		close(p[1]);
		dup2(p[0], STDIN_FILENO);
		execlp("tee", "tee", filename, (char *) 0);
		execlp("/usr/bin/tee", "tee", filename, (char *) 0);
		execlp("cat", "cat", (char *) 0);
		execlp("/bin/cat", "cat", (char *) 0);
		execlp("/usr/bin/cat", "cat", (char *) 0);
		ASSU::printError("ERROR: cannot execute 'tee' or 'cat'; crash log will be lost!\n");
		_exit(1);
		return false;
	} else if (pid == -1) {
		ASSU::printError("ERROR: cannot fork a process for executing 'tee'\n");
		return false;
	} else {
		close(p[0]);
		dup2(p[1], STDOUT_FILENO);
		dup2(p[1], STDERR_FILENO);
		return true;
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

	ctx->callCount++;
	if (ctx->callCount > 1) {
		// The abort handler itself crashed!
		const char *end = state.messageBuf + sizeof(state.messageBuf);
		char *pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, "[ origpid=");
		pos = ASSU::appendInteger<pid_t, 10>(pos, end, state.pid);
		pos = ASSU::appendData(pos, end, ", pid=");
		pos = ASSU::appendInteger<pid_t, 10>(pos, end, getpid());
		pos = ASSU::appendData(pos, end, ", timestamp=");
		pos = ASSU::appendInteger<time_t, 10>(pos, end, t);
		if (ctx->callCount == 2) {
			// This is the first time it crashed.
			pos = ASSU::appendData(pos, end, " ] Abort handler crashed! signo=");
			pos = appendSignalName(pos, end, state.signo);
			pos = ASSU::appendData(pos, end, ", reason=");
			pos = appendSignalReason(pos, end, state.info);
			pos = ASSU::appendData(pos, end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
			// Run default signal handler.
			raise(signo);
		} else {
			// This is the second time it crashed, meaning it failed to
			// invoke the default signal handler to abort the process!
			pos = ASSU::appendData(pos, end, " ] Abort handler crashed again! Force exiting this time. signo=");
			pos = appendSignalName(pos, end, state.signo);
			pos = ASSU::appendData(pos, end, ", reason=");
			pos = appendSignalReason(pos, end, state.info);
			pos = ASSU::appendData(pos, end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
			_exit(1);
		}
		return;
	}

	closeEmergencyPipes();

	{
		const char *end = state.messagePrefix + sizeof(state.messagePrefix);
		char *pos = state.messagePrefix;
		pos = ASSU::appendData(pos, end, "[ pid=");
		pos = ASSU::appendInteger<pid_t, 10>(pos, end, state.pid);
		*pos = '\0';
	}

	/* We want to dump the entire crash log to both stderr and a log file.
	 * We use 'tee' for this.
	 */
	state.crashLogDir[0] = '\0';
	state.crashLogDirFd = -1;
	if (createCrashLogDir(state, t)) {
		forkAndRedirectToTeeAndMainLogFile(state.crashLogDir);
	}

	const char *end = state.messageBuf + sizeof(state.messageBuf);
	char *pos = state.messageBuf;
	// Print a \n just in case we're aborting in the middle of a non-terminated line.
	pos = ASSU::appendData(pos, end, "\n");
	pos = ASSU::appendData(pos, end, state.messagePrefix);
	pos = ASSU::appendData(pos, end, ", timestamp=");
	pos = ASSU::appendInteger<time_t, 10>(pos, end, t);
	pos = ASSU::appendData(pos, end, " ] Process aborted! signo=");
	pos = appendSignalName(pos, end, state.signo);
	pos = ASSU::appendData(pos, end, ", reason=");
	pos = appendSignalReason(pos, end, state.info);
	pos = ASSU::appendData(pos, end, ", randomSeed=");
	pos = ASSU::appendInteger<unsigned int, 10>(pos, end, ctx->config->randomSeed);
	pos = ASSU::appendData(pos, end, "\n");
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

	pos = state.messageBuf;
	if (state.crashLogDir[0] != '\0') {
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] Crash log files will be dumped to ");
		pos = ASSU::appendData(pos, end, state.crashLogDir);
		pos = ASSU::appendData(pos, end, " <--- ******* LOOK HERE FOR DETAILS!!! *******\n");
	} else {
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] Could not create crash log directory, so dumping to stderr only.\n");
	}
	write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

	if (ctx->config->beep) {
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] PASSENGER_BEEP_ON_ABORT on, executing beep...\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

		child = asyncFork();
		if (child == 0) {
			closeAllFileDescriptors(2, true);
			#ifdef __APPLE__
				const char *command[] = { "osascript", NULL };
				execlp("osascript", "osascript", "-e", "beep 2", (char *) 0);
				printExecError2(command, errno, state.messageBuf, sizeof(state.messageBuf));
			#else
				const char *command[] = { "beep", NULL };
				execlp("beep", "beep", (char *) 0);
				printExecError2(command, errno, state.messageBuf, sizeof(state.messageBuf));
			#endif
			_exit(1);

		} else if (child == -1) {
			int e = errno;
			pos = state.messageBuf;
			pos = ASSU::appendData(pos, end, state.messagePrefix);
			pos = ASSU::appendData(pos, end, " ] Could fork a child process for invoking a beep: fork() failed with errno=");
			pos = ASSU::appendInteger<int, 10>(pos, end, e);
			pos = ASSU::appendData(pos, end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
		}
	}

	if (ctx->config->stopProcess) {
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] PASSENGER_STOP_ON_ABORT on, so process stopped. Send SIGCONT when you want to continue.\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
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
			pos = state.messageBuf;
			pos = ASSU::appendData(pos, end, state.messagePrefix);
			pos = ASSU::appendData(pos, end, "] Could not fork a child process for dumping diagnostics: fork() failed with errno=");
			pos = ASSU::appendInteger<int, 10>(pos, end, e);
			pos = ASSU::appendData(pos, end, "\n");
			write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);
			_exit(1);

		} else {
			// Exit immediately so that child process is adopted by init process.
			_exit(0);
		}

	} else if (child == -1) {
		int e = errno;
		pos = state.messageBuf;
		pos = ASSU::appendData(pos, end, state.messagePrefix);
		pos = ASSU::appendData(pos, end, " ] Could not fork a child process for dumping diagnostics: fork() failed with errno=");
		pos = ASSU::appendInteger<int, 10>(pos, end, e);
		pos = ASSU::appendData(pos, end, "\n");
		write_nowarn(STDERR_FILENO, state.messageBuf, pos - state.messageBuf);

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
	ctx->randomTokens = strdup(RandomGenerator().generateAsciiString(
		MAX_RANDOM_TOKENS * RANDOM_TOKEN_SIZE).c_str());
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
	char *oldTmpDir = ctx->tmpDir;
	char *oldCrashWatchCommand = ctx->crashWatchCommand;
	char *oldBacktraceSanitizerCommand = ctx->backtraceSanitizerCommand;

	if (config->resourceLocator != NULL) {
		string path;
		const ResourceLocator *locator = config->resourceLocator;

		ctx->installSpec = strdup(locator->getInstallSpec().c_str());
		ctx->rubyLibDir = strdup(locator->getRubyLibDir().c_str());
		ctx->tmpDir = strdup(getSystemTempDir());

		path = locator->getHelperScriptsDir() + "/crash-watch.rb";
		ctx->crashWatchCommand = strdup(path.c_str());

		if (ctx->installSpec == NULL || ctx->rubyLibDir == NULL
			|| ctx->tmpDir == NULL || ctx->crashWatchCommand == NULL)
		{
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
		ctx->tmpDir = NULL;
		ctx->crashWatchCommand = NULL;
		useCxxFiltAsBacktraceSanitizer();
	}

	free(oldInstallSpec);
	free(oldRubyLibDir);
	free(oldTmpDir);
	free(oldCrashWatchCommand);
	free(oldBacktraceSanitizerCommand);
}

void
shutdownAbortHandler() {
	free(ctx->installSpec);
	free(ctx->rubyLibDir);
	free(ctx->tmpDir);
	free(ctx->crashWatchCommand);
	free(ctx->backtraceSanitizerCommand);
	free(ctx->randomTokens);
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
