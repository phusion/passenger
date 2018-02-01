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

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE // according to Linux man page for syscall()
#endif

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <poll.h>
#ifdef __linux__
	#include <sys/syscall.h>
	#include <features.h>
#endif

#include <string>
#include <cstddef>
#include <cstring>
#include <climits>
#include <cerrno>

#include <ProcessManagement/Utils.h>
#include <Utils/AsyncSignalSafeUtils.h>

#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__sun)
	// Introduced in Solaris 9. Let's hope nobody actually uses
	// a version that doesn't support this.
	#define HAS_CLOSEFROM
#endif

namespace Passenger {

using namespace std;


#ifdef __APPLE__
	// http://www.opensource.apple.com/source/Libc/Libc-825.26/sys/fork.c
	// This bypasses atfork handlers.
	extern "C" {
		extern pid_t __fork(void);
	}
#endif

pid_t
asyncFork() {
	#if defined(__linux__)
		#if defined(SYS_fork)
			return (pid_t) syscall(SYS_fork);
		#else
			return syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0);
		#endif
	#elif defined(__APPLE__)
		return __fork();
	#else
		return fork();
	#endif
}

void
resetSignalHandlersAndMask() {
	struct sigaction action;
	action.sa_handler = SIG_DFL;
	action.sa_flags   = SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(SIGHUP,  &action, NULL);
	sigaction(SIGINT,  &action, NULL);
	sigaction(SIGQUIT, &action, NULL);
	sigaction(SIGILL,  &action, NULL);
	sigaction(SIGTRAP, &action, NULL);
	sigaction(SIGABRT, &action, NULL);
	#ifdef SIGEMT
		sigaction(SIGEMT,  &action, NULL);
	#endif
	sigaction(SIGFPE,  &action, NULL);
	sigaction(SIGBUS,  &action, NULL);
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGSYS,  &action, NULL);
	sigaction(SIGPIPE, &action, NULL);
	sigaction(SIGALRM, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGURG,  &action, NULL);
	sigaction(SIGSTOP, &action, NULL);
	sigaction(SIGTSTP, &action, NULL);
	sigaction(SIGCONT, &action, NULL);
	sigaction(SIGCHLD, &action, NULL);
	#ifdef SIGINFO
		sigaction(SIGINFO, &action, NULL);
	#endif
	sigaction(SIGUSR1, &action, NULL);
	sigaction(SIGUSR2, &action, NULL);

	// We reset the signal mask after resetting the signal handlers,
	// because prior to calling resetSignalHandlersAndMask(), the
	// process might be blocked on some signals. We want those signals
	// to be processed after installing the new signal handlers
	// so that bugs like https://github.com/phusion/passenger/pull/97
	// can be prevented.

	sigset_t signal_set;
	int ret;

	sigemptyset(&signal_set);
	do {
		ret = sigprocmask(SIG_SETMASK, &signal_set, NULL);
	} while (ret == -1 && errno == EINTR);
}

void
disableMallocDebugging() {
	unsetenv("MALLOC_FILL_SPACE");
	unsetenv("MALLOC_PROTECT_BEFORE");
	unsetenv("MallocGuardEdges");
	unsetenv("MallocScribble");
	unsetenv("MallocPreScribble");
	unsetenv("MallocCheckHeapStart");
	unsetenv("MallocCheckHeapEach");
	unsetenv("MallocCheckHeapAbort");
	unsetenv("MallocBadFreeAbort");
	unsetenv("MALLOC_CHECK_");

	const char *libs = getenv("DYLD_INSERT_LIBRARIES");
	if (libs != NULL && strstr(libs, "/usr/lib/libgmalloc.dylib")) {
		string newLibs = libs;
		string::size_type pos = newLibs.find("/usr/lib/libgmalloc.dylib");
		size_t len = strlen("/usr/lib/libgmalloc.dylib");

		// Erase all leading ':' too.
		while (pos > 0 && newLibs[pos - 1] == ':') {
			pos--;
			len++;
		}
		// Erase all trailing ':' too.
		while (pos + len < newLibs.size() && newLibs[pos + len] == ':') {
			len++;
		}

		newLibs.erase(pos, len);
		if (newLibs.empty()) {
			unsetenv("DYLD_INSERT_LIBRARIES");
		} else {
			setenv("DYLD_INSERT_LIBRARIES", newLibs.c_str(), 1);
		}
	}
}

// Async-signal safe way to get the current process's hard file descriptor limit.
static int
getFileDescriptorLimit() {
	long long sysconfResult = sysconf(_SC_OPEN_MAX);

	struct rlimit rl;
	long long rlimitResult;
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
		rlimitResult = 0;
	} else {
		rlimitResult = (long long) rl.rlim_max;
	}

	long result;
	// OS X 10.9 returns LLONG_MAX. It doesn't make sense
	// to use that result so we limit ourselves to the
	// sysconf result.
	if (rlimitResult >= INT_MAX || sysconfResult > rlimitResult) {
		result = sysconfResult;
	} else {
		result = rlimitResult;
	}

	if (result < 0) {
		// Unable to query the file descriptor limit.
		result = 9999;
	} else if (result < 2) {
		// The calls reported broken values.
		result = 2;
	}
	return result;
}

// Async-signal safe function to get the highest file
// descriptor that the process is currently using.
// See also http://stackoverflow.com/questions/899038/getting-the-highest-allocated-file-descriptor
static int
getHighestFileDescriptor(bool asyncSignalSafe) {
#if defined(F_MAXFD)
	int ret;

	do {
		ret = fcntl(0, F_MAXFD);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		ret = getFileDescriptorLimit();
	}
	return ret;

#else
	int p[2], ret, flags;
	pid_t pid = -1;
	int result = -1;

	/* Since opendir() may not be async signal safe and thus may lock up
	 * or crash, we use it in a child process which we kill if we notice
	 * that things are going wrong.
	 */

	// Make a pipe.
	p[0] = p[1] = -1;
	do {
		ret = pipe(p);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		goto done;
	}

	// Make the read side non-blocking.
	do {
		flags = fcntl(p[0], F_GETFL);
	} while (flags == -1 && errno == EINTR);
	if (flags == -1) {
		goto done;
	}
	do {
		fcntl(p[0], F_SETFL, flags | O_NONBLOCK);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		goto done;
	}

	if (asyncSignalSafe) {
		do {
			pid = asyncFork();
		} while (pid == -1 && errno == EINTR);
	} else {
		do {
			pid = fork();
		} while (pid == -1 && errno == EINTR);
	}

	if (pid == 0) {
		// Don't close p[0] here or it might affect the result.

		resetSignalHandlersAndMask();

		struct sigaction action;
		action.sa_handler = _exit;
		action.sa_flags   = SA_RESTART;
		sigemptyset(&action.sa_mask);
		sigaction(SIGSEGV, &action, NULL);
		sigaction(SIGPIPE, &action, NULL);
		sigaction(SIGBUS, &action, NULL);
		sigaction(SIGILL, &action, NULL);
		sigaction(SIGFPE, &action, NULL);
		sigaction(SIGABRT, &action, NULL);

		DIR *dir = NULL;
		#ifdef __APPLE__
			/* /dev/fd can always be trusted on OS X. */
			dir = opendir("/dev/fd");
		#else
			/* On FreeBSD and possibly other operating systems, /dev/fd only
			 * works if fdescfs is mounted. If it isn't mounted then /dev/fd
			 * still exists but always returns [0, 1, 2] and thus can't be
			 * trusted. If /dev and /dev/fd are on different filesystems
			 * then that probably means fdescfs is mounted.
			 */
			struct stat dirbuf1, dirbuf2;
			if (stat("/dev", &dirbuf1) == -1
			 || stat("/dev/fd", &dirbuf2) == -1) {
				_exit(1);
			}
			if (dirbuf1.st_dev != dirbuf2.st_dev) {
				dir = opendir("/dev/fd");
			}
		#endif
		if (dir == NULL) {
			dir = opendir("/proc/self/fd");
			if (dir == NULL) {
				_exit(1);
			}
		}

		struct dirent *ent;
		union {
			int highest;
			char data[sizeof(int)];
		} u;
		u.highest = -1;

		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] != '.') {
				int number = atoi(ent->d_name);
				if (number > u.highest) {
					u.highest = number;
				}
			}
		}
		if (u.highest != -1) {
			ssize_t ret, written = 0;
			do {
				ret = write(p[1], u.data + written, sizeof(int) - written);
				if (ret == -1) {
					_exit(1);
				}
				written += ret;
			} while (written < (ssize_t) sizeof(int));
		}
		closedir(dir);
		_exit(0);

	} else if (pid == -1) {
		goto done;

	} else {
		close(p[1]); // Do not retry on EINTR: http://news.ycombinator.com/item?id=3363819
		p[1] = -1;

		union {
			int highest;
			char data[sizeof(int)];
		} u;
		ssize_t ret, bytesRead = 0;
		struct pollfd pfd;
		pfd.fd = p[0];
		pfd.events = POLLIN;

		do {
			do {
				// The child process must finish within 30 ms, otherwise
				// we might as well query sysconf.
				ret = poll(&pfd, 1, 30);
			} while (ret == -1 && errno == EINTR);
			if (ret <= 0) {
				goto done;
			}

			do {
				ret = read(p[0], u.data + bytesRead, sizeof(int) - bytesRead);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				if (errno != EAGAIN) {
					goto done;
				}
			} else if (ret == 0) {
				goto done;
			} else {
				bytesRead += ret;
			}
		} while (bytesRead < (ssize_t) sizeof(int));

		result = u.highest;
		goto done;
	}

done:
	// Do not retry on EINTR: http://news.ycombinator.com/item?id=3363819
	if (p[0] != -1) {
		close(p[0]);
	}
	if (p[1] != -1) {
		close(p[1]);
	}
	if (pid != -1) {
		do {
			ret = kill(pid, SIGKILL);
		} while (ret == -1 && errno == EINTR);
		do {
			ret = waitpid(pid, NULL, 0);
		} while (ret == -1 && errno == EINTR);
	}

	if (result == -1) {
		result = getFileDescriptorLimit();
	}
	return result;
#endif
}

void
closeAllFileDescriptors(int lastToKeepOpen, bool asyncSignalSafe) {
	#if defined(F_CLOSEM)
		int ret;
		do {
			ret = fcntl(lastToKeepOpen + 1, F_CLOSEM);
		} while (ret == -1 && errno == EINTR);
		if (ret != -1) {
			return;
		}
	#elif defined(HAS_CLOSEFROM)
		closefrom(lastToKeepOpen + 1);
		return;
	#endif

	for (int i = getHighestFileDescriptor(asyncSignalSafe); i > lastToKeepOpen; i--) {
		/* Even though we normally shouldn't retry on EINTR
		 * (http://news.ycombinator.com/item?id=3363819)
		 * it's okay to do that here because this function
		 * may only be called in a single-threaded environment.
		 */
		int ret;
		do {
			ret = close(i);
		} while (ret == -1 && errno == EINTR);
	}
}

void
printExecError(const char **command, int errcode) {
	char buf[1024] = { };
	printExecError2(command, errcode, buf, sizeof(buf));
}

void
printExecError2(const char **command, int errcode, char *buf, size_t size) {
	char *pos = buf;
	const char *end = buf + size;

	pos = AsyncSignalSafeUtils::appendData(pos, end, "*** ERROR: cannot execute ");
	pos = AsyncSignalSafeUtils::appendData(pos, end, command[0]);
	pos = AsyncSignalSafeUtils::appendData(pos, end, ": ");
	pos = AsyncSignalSafeUtils::appendData(pos, end,
		AsyncSignalSafeUtils::limitedStrerror(errcode));
	pos = AsyncSignalSafeUtils::appendData(pos, end, " (errno=");
	pos = AsyncSignalSafeUtils::appendInteger<int, 10>(pos, end, errcode);
	pos = AsyncSignalSafeUtils::appendData(pos, end, ")\n");

	AsyncSignalSafeUtils::printError(buf, pos - buf);
}


} // namespace Passenger
