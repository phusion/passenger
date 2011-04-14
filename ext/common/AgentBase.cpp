/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#if defined(__APPLE__) || defined(__linux__)
	#define LIBC_HAS_BACKTRACE_FUNC
#endif
#ifdef LIBC_HAS_BACKTRACE_FUNC
	#include <execinfo.h>
#endif

#include "Constants.h"
#include "AgentBase.h"
#include "Exceptions.h"
#include "Logging.h"

namespace Passenger {

static bool _feedbackFdAvailable = false;
static const char digits[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

// Pre-allocate an alternative stack for use in signal handlers in case
// the normal stack isn't usable.
static char *alternativeStack;
static unsigned int alternativeStackSize;

static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
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
			SI_CODE_HANDLER(SEGV_MAPERR);
			SI_CODE_HANDLER(SEGV_ACCERR);
			default:
				handled = false;
				break;
			}
			break;
		case SIGBUS:
			switch (info->si_code) {
			SI_CODE_HANDLER(BUS_ADRALN);
			SI_CODE_HANDLER(BUS_ADRERR);
			SI_CODE_HANDLER(BUS_OBJERR);
			default:
				handled = false;
				break;
			}
			break;
		};
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
	
	return buf;
}

static void
abortHandler(int signo, siginfo_t *info, void *ctx) {
	pid_t pid = getpid();
	char messageBuf[1024];
	#ifdef LIBC_HAS_BACKTRACE_FUNC
		void *backtraceStore[512];
		backtraceStore[0] = '\0'; // Don't let gdb print uninitialized contents.
	#endif
	
	char *end = messageBuf;
	end = appendText(end, "[ pid=");
	end = appendULL(end, (unsigned long long) pid);
	end = appendText(end, ", timestamp=");
	end = appendULL(end, (unsigned long long) time(NULL));
	end = appendText(end, " ] Process aborted! signo=");
	end = appendSignalName(end, signo);
	end = appendText(end, ", reason=");
	end = appendSignalReason(end, info);
	
	// It is important that writing the message and the backtrace are two
	// seperate operations because it's not entirely clear whether the
	// latter is async signal safe and thus can crash.
	#ifdef LIBC_HAS_BACKTRACE_FUNC
		end = appendText(end, ", backtrace available.\n");
	#else
		end = appendText(end, "\n");
	#endif
	write(STDERR_FILENO, messageBuf, end - messageBuf);
	
	#ifdef LIBC_HAS_BACKTRACE_FUNC
		/* For some reason, it would appear that fatal signal
		 * handlers have a deadline on some systems: the process will
		 * be killed if the signal handler doesn't finish in time.
		 * This killing appears to be triggered at some system calls,
		 * including but not limited to nanosleep().
		 * backtrace() might be slow and running crash-watch is
		 * definitely slow, so we do our work in a child process
		 * in order not to be affected by the deadline. But preferably
		 * we don't fork because forking will cause us to lose
		 * thread information.
		 */
		#ifdef __linux__
			bool hasDeadline = false;
		#else
			// Mac OS X has a deadline. Not sure about other systems.
			bool hasDeadline = true;
		#endif
		if (!hasDeadline || fork() == 0) {
			int frames = backtrace(backtraceStore, sizeof(backtraceStore) / sizeof(void *));
			end = messageBuf;
			end = appendText(end, "--------------------------------------\n");
			end = appendText(end, "[ pid=");
			end = appendULL(end, (unsigned long long) pid);
			end = appendText(end, " ] Backtrace with ");
			end = appendULL(end, (unsigned long long) frames);
			end = appendText(end, " frames:\n");
			write(STDERR_FILENO, messageBuf, end - messageBuf);
			backtrace_symbols_fd(backtraceStore, frames, STDERR_FILENO);
			
			end = messageBuf;
			end = appendText(end, "--------------------------------------\n");
			end = appendText(end, "[ pid=");
			end = appendULL(end, (unsigned long long) pid);
			end = appendText(end, " ] Dumping a more detailed backtrace with crash-watch "
				"('gem install crash-watch' if you don't have it)...\n");
			write(STDERR_FILENO, messageBuf, end - messageBuf);
			
			end = messageBuf;
			end = appendText(end, "crash-watch --dump ");
			end = appendULL(end, (unsigned long long) getpid());
			*end = '\0';
			system(messageBuf);
			_exit(1);
		}
	#endif
	
	// Run default signal handler.
	kill(getpid(), signo);
}

static void
installAbortHandler() {
	alternativeStackSize = MINSIGSTKSZ + 64 * 1024;
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
}

bool
feedbackFdAvailable() {
	return _feedbackFdAvailable;
}

VariantMap
initializeAgent(int argc, char *argv[], const char *processName) {
	TRACE_POINT();
	VariantMap options;
	
	ignoreSigpipe();
	installAbortHandler();
	setup_syscall_interruption_support();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	try {
		if (argc == 1) {
			int ret = fcntl(FEEDBACK_FD, F_GETFL);
			if (ret == -1) {
				if (errno == EBADF) {
					fprintf(stderr,
						"You're not supposed to start this program from the command line. "
						"It's used internally by Phusion Passenger.\n");
					exit(1);
				} else {
					int e = errno;
					fprintf(stderr,
						"Encountered an error in feedback file descriptor 3: %s (%d)\n",
							strerror(e), e);
					exit(1);
				}
			} else {
				_feedbackFdAvailable = true;
				options.readFrom(FEEDBACK_FD);
				if (options.getBool("fire_and_forget", false)) {
					_feedbackFdAvailable = false;
					close(FEEDBACK_FD);
				}
			}
		} else {
			options.readFrom((const char **) argv + 1, argc - 1);
		}
		
		setLogLevel(options.getInt("log_level", false, 0));
		if (!options.get("debug_log_file", false).empty()) {
			if (strcmp(processName, "PassengerWatchdog") == 0) {
				/* Have the watchdog set STDOUT and STDERR to the debug
				 * log file so that system abort() calls that stuff
				 * are properly logged.
				 */
				string filename = options.get("debug_log_file");
				options.erase("debug_log_file");
				
				int fd = open(filename.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
				if (fd == -1) {
					int e = errno;
					throw FileSystemException("Cannot open debug log file " +
						filename, e, filename);
				}
				
				dup2(fd, STDOUT_FILENO);
				dup2(fd, STDERR_FILENO);
				close(fd);
			} else {
				setDebugFile(options.get("debug_log_file").c_str());
			}
		}
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		exit(1);
	}
	
	// Change process title.
	strncpy(argv[0], processName, strlen(argv[0]));
	for (int i = 1; i < argc; i++) {
		memset(argv[i], '\0', strlen(argv[i]));
	}
	
	return options;
}

} // namespace Passenger
