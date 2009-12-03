/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2008 Phusion
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
#include "system_calls.hpp"
#include <boost/thread.hpp>
#include <cerrno>

using namespace boost;
using namespace oxt;


/*************************************
 * oxt
 *************************************/

static void
interruption_signal_handler(int sig) {
	// Do nothing.
}

void
oxt::setup_syscall_interruption_support() {
	struct sigaction action;
	sigset_t signal_set;
	int ret;
	
	/* Very important! The signal mask is inherited across fork()
	 * and exec() and we don't know what the parent process did to
	 * us. At least on OS X, having a signal mask blocking important
	 * signals can lead to stuff like waitpid() malfunction.
	 */
	sigemptyset(&signal_set);
	do {
		ret = sigprocmask(SIG_SETMASK, &signal_set, NULL);
	} while (ret == -1 && errno == EINTR);
	
	action.sa_handler = interruption_signal_handler;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	do {
		ret = sigaction(INTERRUPTION_SIGNAL, &action, NULL);
	} while (ret == -1 && errno == EINTR);
	do {
		ret = siginterrupt(INTERRUPTION_SIGNAL, 1);
	} while (ret == -1 && errno == EINTR);
}


/*************************************
 * Passenger::syscalls
 *************************************/

#define CHECK_INTERRUPTION(error_expression, code) \
	do { \
		int _my_errno; \
		do { \
			code; \
			_my_errno = errno; \
		} while ((error_expression) && _my_errno == EINTR \
			&& !this_thread::syscalls_interruptable()); \
		if ((error_expression) && _my_errno == EINTR && this_thread::syscalls_interruptable()) { \
			throw thread_interrupted(); \
		} \
		errno = _my_errno; \
	} while (false)

ssize_t
syscalls::read(int fd, void *buf, size_t count) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::read(fd, buf, count)
	);
	return ret;
}

ssize_t
syscalls::write(int fd, const void *buf, size_t count) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::write(fd, buf, count)
	);
	return ret;
}

int
syscalls::close(int fd) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::close(fd)
	);
	return ret;
}

int
syscalls::accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::accept(sockfd, addr, addrlen)
	);
	return ret;
}

int
syscalls::bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::bind(sockfd, addr, addrlen)
	);
	return ret;
}

int
syscalls::connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen) {
	int ret;
	// FIXME: I don't think this is entirely correct.
	// http://www.madore.org/~david/computers/connect-intr.html
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::connect(sockfd, serv_addr, addrlen);
	);
	return ret;
}

int
syscalls::listen(int sockfd, int backlog) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::listen(sockfd, backlog)
	);
	return ret;
}

int
syscalls::socket(int domain, int type, int protocol) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::socket(domain, type, protocol)
	);
	return ret;
}

int
syscalls::socketpair(int d, int type, int protocol, int sv[2]) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::socketpair(d, type, protocol, sv)
	);
	return ret;
}

ssize_t
syscalls::recvmsg(int s, struct msghdr *msg, int flags) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		#ifdef _AIX53
			ret = ::nrecvmsg(s, msg, flags)
		#else
			ret = ::recvmsg(s, msg, flags)
		#endif
	);
	return ret;
}

ssize_t
syscalls::sendmsg(int s, const struct msghdr *msg, int flags) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		#ifdef _AIX53
			ret = ::nsendmsg(s, msg, flags)
		#else
			ret = ::sendmsg(s, msg, flags)
		#endif
	);
	return ret;
}

int
syscalls::setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::setsockopt(s, level, optname, optval, optlen)
	);
	return ret;
}

int
syscalls::shutdown(int s, int how) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::shutdown(s, how)
	);
	return ret;
}

FILE *
syscalls::fopen(const char *path, const char *mode) {
	FILE *ret;
	CHECK_INTERRUPTION(
		ret == NULL,
		ret = ::fopen(path, mode)
	);
	return ret;
}

int
syscalls::fclose(FILE *fp) {
	int ret;
	CHECK_INTERRUPTION(
		ret == EOF,
		ret = ::fclose(fp)
	);
	return ret;
}

int
syscalls::unlink(const char *pathname) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::unlink(pathname)
	);
	return ret;
}

int
syscalls::stat(const char *path, struct stat *buf) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::stat(path, buf)
	);
	return ret;
}

time_t
syscalls::time(time_t *t) {
	time_t ret;
	CHECK_INTERRUPTION(
		ret == (time_t) -1,
		ret = ::time(t)
	);
	return ret;
}

int
syscalls::usleep(useconds_t usec) {
	// We use syscalls::nanosleep() here to reuse the code that sleeps
	// for the remaining amount of time, if a signal was received but
	// system call interruption is disabled.
	struct timespec spec;
	spec.tv_sec = usec / 1000000;
	spec.tv_nsec = usec % 1000000 * 1000;
	return syscalls::nanosleep(&spec, NULL);
}

int
syscalls::nanosleep(const struct timespec *req, struct timespec *rem) {
	struct timespec req2 = *req;
	struct timespec rem2;
	int ret, e;
	do {
		ret = ::nanosleep(&req2, &rem2);
		e = errno;
		req2 = rem2;
	} while (ret == -1 && e == EINTR && !this_thread::syscalls_interruptable());
	if (ret == -1 && e == EINTR && this_thread::syscalls_interruptable()) {
		throw thread_interrupted();
	}
	errno = e;
	if (ret == 0 && rem) {
		*rem = rem2;
	}
	return ret;
}

pid_t
syscalls::fork() {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::fork()
	);
	return ret;
}

int
syscalls::kill(pid_t pid, int sig) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::kill(pid, sig)
	);
	return ret;
}

pid_t
syscalls::waitpid(pid_t pid, int *status, int options) {
	pid_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::waitpid(pid, status, options)
	);
	return ret;
}


/*************************************
 * boost::this_thread
 *************************************/

thread_specific_ptr<bool> this_thread::_syscalls_interruptable;


bool
this_thread::syscalls_interruptable() {
	return _syscalls_interruptable.get() == NULL || *_syscalls_interruptable;
}

