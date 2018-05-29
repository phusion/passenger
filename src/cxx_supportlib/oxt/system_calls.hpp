/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2010-2018 Phusion Holding B.V.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef _OXT_SYSTEM_CALLS_HPP_
#define _OXT_SYSTEM_CALLS_HPP_

#include <boost/thread/tss.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <cstdio>
#include <ctime>
#include <cassert>
#include "macros.hpp"

/**
 * System call and C library call wrappers with extra features
 *
 * This file provides wrappers for many system calls and C library calls
 * and adds the following features:
 *
 * - Interruption of blocking system calls and blocking C library calls.
 * - Simulation of random failures.
 *
 * ## About system call interruption
 *
 * One must first call `oxt::setup_syscall_interruption_support()`.
 * Then one may use the functions in oxt::syscalls as drop-in replacements
 * for system calls or C library functions. These functions throw
 * boost::thread_interrupted upon interruption, instead of returning an EINTR
 * error.
 *
 * Once `setup_syscall_interruption_support()` has been called, system call
 * interruption is enabled by default. You can enable or disable system call
 * interruption in the current scope by creating instances of
 * boost::this_thread::enable_syscall_interruption or
 * boost::this_thread::disable_syscall_interruption, respectively. When system
 * call interruption is disabled, the oxt::syscall wrapper functions will
 * ignore interruption requests -- that is, they will never throw
 * boost::thread_interrupted, nor will they return EINTR errors. This is similar
 * to Boost thread interruption.
 *
 * ### How to interrupt
 *
 * Generally, oxt::thread::interrupt() and oxt::thread::interrupt_and_join()
 * should be used for interrupting threads. These methods will interrupt
 * the thread at all Boost interruption points, as well as system calls that
 * are caled through the oxt::syscalls namespace. Do *not* use
 * boost::thread::interrupt, because that will not honor system calls as
 * interruption points.
 *
 * Under the hood, system calls are interrupted by sending a signal to the
 * to a specific thread (note: sending a signal to a process will deliver the
 * signal to the main thread).
 *
 * Any signal will do, but of course, one should only send a signal whose
 * signal handler doesn't do undesirable things (such as aborting the entire
 * program). That's why it's generally recommended that you only use
 * oxt::INTERRUPTION_SIGNAL to interrupt system calls, because
 * oxt::setup_syscall_interruption_support() installs an "nice" signal
 * handler for that signal (though you should of course use
 * oxt::thread::interrupt() instead of sending signals whenever
 * possible).
 *
 * Note that sending a signal once may not interrupt the thread, because
 * the thread may not be calling a system call at the time the signal was
 * received. So one must keep sending signals periodically until the
 * thread has quit.
 *
 * **Warning**:
 *
 * After `oxt::setup_syscall_interruption_support()` is called, sending a signal
 * will cause system calls to return with an EINTR error. The oxt::syscall
 * functions will automatically take care of this, but if you're calling any
 * system calls without using that namespace, then you should check for and
 * take care of EINTR errors.
 *
 * ## About random simulation of failures
 *
 * Call `oxt::setup_random_failure_simulation()` to initialize random
 * failure simulation.
 */

// This is one of the things that Java is good at and C++ sucks at. Sigh...

namespace oxt {
	static const int INTERRUPTION_SIGNAL = SIGUSR1; // SIGUSR2 is reserved by Valgrind...
	#define OXT_MAX_ERROR_CHANCES 16

	struct ErrorChance {
		double chance;
		int errorCode;
	};

	void setup_syscall_interruption_support();

	void setup_random_failure_simulation(const ErrorChance *errorChances, unsigned int n);

	/**
	 * System call and C library call wrappers with interruption support.
	 * These functions are interruption points, i.e. they throw
	 * boost::thread_interrupted whenever the calling thread is interrupted
	 * by oxt::thread::interrupt() or oxt::thread::interrupt_and_join().
	 */
	namespace syscalls {
		using namespace std;

		int open(const char *path, int oflag);
		int open(const char *path, int oflag, mode_t mode);
		int openat(int dirfd, const char *path, int oflag);
		int openat(int dirfd, const char *path, int oflag, mode_t mode);
		ssize_t read(int fd, void *buf, size_t count);
		ssize_t write(int fd, const void *buf, size_t count);
		ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
		int close(int fd);
		int pipe(int filedes[2]);
		int dup2(int filedes, int filedes2);
		int mkdir(const char *pathname, mode_t mode);
		int chown(const char *path, uid_t owner, gid_t group);

		int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
		int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
		int connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen);
		int listen(int sockfd, int backlog);
		int socket(int domain, int type, int protocol);
		int socketpair(int d, int type, int protocol, int sv[2]);
		ssize_t recvmsg(int s, struct msghdr *msg, int flags);
		ssize_t sendmsg(int s, const struct msghdr *msg, int flags);
		int setsockopt(int s, int level, int optname, const void *optval,
			socklen_t optlen);
		int shutdown(int s, int how);

		int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds,
		           struct timeval *timeout);
		int poll(struct pollfd fds[], nfds_t nfds, int timeout);

		FILE *fopen(const char *path, const char *mode);
		size_t fread(void *ptr, size_t size, size_t nitems, FILE *stream);
		int fclose(FILE *fp);
		int unlink(const char *pathname);
		int stat(const char *path, struct stat *buf);
		int lstat(const char *path, struct stat *buf);

		time_t time(time_t *t);
		unsigned int sleep(unsigned int seconds);
		int usleep(useconds_t usec);
		int nanosleep(const struct timespec *req, struct timespec *rem);

		pid_t fork();
		int kill(pid_t pid, int sig);
		int killpg(pid_t pgrp, int sig);
		pid_t waitpid(pid_t pid, int *status, int options);
	}

} // namespace oxt

namespace boost {
namespace this_thread {

	/**
	 * @intern
	 */
	#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
		extern __thread int _syscalls_interruptable;
	#else
		extern boost::thread_specific_ptr<bool> _syscalls_interruptable;
	#endif

	/**
	 * Check whether system calls should be interruptable in
	 * the calling thread.
	 */
	bool syscalls_interruptable();

	class restore_syscall_interruption;

	/**
	 * Create this struct on the stack to temporarily enable system
	 * call interruption, until the object goes out of scope.
	 */
	class enable_syscall_interruption {
	private:
		bool last_value;
	public:
		enable_syscall_interruption() {
			#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
				last_value = !!_syscalls_interruptable;
				_syscalls_interruptable = 1;
			#else
				if (_syscalls_interruptable.get() == NULL) {
					last_value = true;
					_syscalls_interruptable.reset(new bool(true));
				} else {
					last_value = *_syscalls_interruptable;
					*_syscalls_interruptable = true;
				}
			#endif
		}

		~enable_syscall_interruption() {
			#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
				_syscalls_interruptable = last_value;
			#else
				*_syscalls_interruptable = last_value;
			#endif
		}
	};

	/**
	 * Create this struct on the stack to temporarily disable system
	 * call interruption, until the object goes out of scope.
	 * While system call interruption is disabled, the functions in
	 * InterruptableCalls will try until the return code is not EINTR.
	 */
	class disable_syscall_interruption {
	private:
		friend class restore_syscall_interruption;
		bool last_value;
	public:
		disable_syscall_interruption() {
			#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
				last_value = !!_syscalls_interruptable;
				_syscalls_interruptable = 0;
			#else
				if (_syscalls_interruptable.get() == NULL) {
					last_value = true;
					_syscalls_interruptable.reset(new bool(false));
				} else {
					last_value = *_syscalls_interruptable;
					*_syscalls_interruptable = false;
				}
			#endif
		}

		~disable_syscall_interruption() {
			#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
				_syscalls_interruptable = last_value;
			#else
				*_syscalls_interruptable = last_value;
			#endif
		}
	};

	/**
	 * Creating an object of this class on the stack will restore the
	 * system call interruption state to what it was before.
	 */
	class restore_syscall_interruption {
	private:
		bool last_value;
	public:
		restore_syscall_interruption(const disable_syscall_interruption &intr) {
			#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
				last_value = !!_syscalls_interruptable;
				_syscalls_interruptable = intr.last_value;
			#else
				assert(_syscalls_interruptable.get() != NULL);
				last_value = *_syscalls_interruptable;
				*_syscalls_interruptable = intr.last_value;
			#endif
		}

		~restore_syscall_interruption() {
			#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
				_syscalls_interruptable = last_value;
			#else
				*_syscalls_interruptable = last_value;
			#endif
		}
	};

} // namespace this_thread
} // namespace boost

#endif /* _OXT_SYSTEM_CALLS_HPP_ */

