/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "System.h"

/*************************************
 * boost::this_thread
 *************************************/

using namespace boost;

thread_specific_ptr<bool> this_thread::_syscalls_interruptable;


bool
this_thread::syscalls_interruptable() {
	return _syscalls_interruptable.get() == NULL || *_syscalls_interruptable;
}


/*************************************
 * Passenger
 *************************************/

using namespace Passenger;

static bool interrupted = false;

static void
interruptionSignalHandler(int sig) {
	interrupted = true;
}

void
Passenger::setupSyscallInterruptionSupport() {
	signal(INTERRUPTION_SIGNAL, interruptionSignalHandler);
	siginterrupt(INTERRUPTION_SIGNAL, 1);
}


/*************************************
 * Passenger::InterruptableCalls
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
InterruptableCalls::read(int fd, void *buf, size_t count) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::read(fd, buf, count)
	);
	return ret;
}

ssize_t
InterruptableCalls::write(int fd, const void *buf, size_t count) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::write(fd, buf, count)
	);
	return ret;
}

int
InterruptableCalls::close(int fd) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::close(fd)
	);
	return ret;
}

int
InterruptableCalls::socketpair(int d, int type, int protocol, int sv[2]) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::socketpair(d, type, protocol, sv)
	);
	return ret;
}

ssize_t
InterruptableCalls::recvmsg(int s, struct msghdr *msg, int flags) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::recvmsg(s, msg, flags)
	);
	return ret;
}

ssize_t
InterruptableCalls::sendmsg(int s, const struct msghdr *msg, int flags) {
	ssize_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::sendmsg(s, msg, flags)
	);
	return ret;
}

int
InterruptableCalls::shutdown(int s, int how) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::shutdown(s, how)
	);
	return ret;
}

FILE *
InterruptableCalls::fopen(const char *path, const char *mode) {
	FILE *ret;
	CHECK_INTERRUPTION(
		ret == NULL,
		ret = ::fopen(path, mode)
	);
	return ret;
}

int
InterruptableCalls::fclose(FILE *fp) {
	int ret;
	CHECK_INTERRUPTION(
		ret == EOF,
		ret = ::fclose(fp)
	);
	return ret;
}

time_t
InterruptableCalls::time(time_t *t) {
	time_t ret;
	CHECK_INTERRUPTION(
		ret == (time_t) -1,
		ret = ::time(t)
	);
	return ret;
}

int
InterruptableCalls::usleep(useconds_t usec) {
	struct timespec spec;
	spec.tv_sec = usec / 1000000;
	spec.tv_nsec = usec % 1000000;
	return InterruptableCalls::nanosleep(&spec, NULL);
}

int
InterruptableCalls::nanosleep(const struct timespec *req, struct timespec *rem) {
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
InterruptableCalls::fork() {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::fork()
	);
	return ret;
}

int
InterruptableCalls::kill(pid_t pid, int sig) {
	int ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::kill(pid, sig)
	);
	return ret;
}

pid_t
InterruptableCalls::waitpid(pid_t pid, int *status, int options) {
	pid_t ret;
	CHECK_INTERRUPTION(
		ret == -1,
		ret = ::waitpid(pid, status, options)
	);
	return ret;
}

