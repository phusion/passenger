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
#ifndef _OXT_SYSTEM_CALLS_HPP_
#define _OXT_SYSTEM_CALLS_HPP_

#include <boost/thread.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <ctime>
#include <cassert>

/**
 * Support for interruption of blocking system calls and C library calls
 *
 * This file provides a framework for writing multithreading code that can
 * be interrupted, even when blocked on system calls or C library calls.
 *
 * One must first call Passenger::setupSysCallInterruptionSupport().
 * Then one may use the functions in Passenger::InterruptableCalls
 * as drop-in replacements for system calls or C library functions.
 * These functions throw boost::thread_interrupted upon interruption.
 * Thread::interrupt() and Thread::interruptAndJoin() should be used
 * for interrupting threads.
 *
 * System call interruption is disabled by default. In other words: the
 * replacement functions in this file don't throw boost::thread_interrupted.
 * You can enable or disable system call interruption in the current scope
 * by creating instances of boost::this_thread::enable_syscall_interruption
 * and similar objects. This is similar to Boost thread interruption.
 *
 * <h2>Implementation</h2>
 * Under the hood, system calls are interrupted by sending a signal to the
 * current process, or to a specific thread. Sending a signal will cause
 * system calls to return with an EINTR error.
 *
 * Any signal will do, but of course, one should only send a signal whose
 * signal handler doesn't do undesirable things (such as aborting the entire
 * program). That's why it's generally recommended that you only use
 * Passenger::INTERRUPTION_SIGNAL to interrupt system calls, because
 * Passenger::setupSyscallInterruptionSupport() installs an "nice" signal
 * handler for that signal (though you should of course use
 * Passenger::Thread::interrupt() instead of sending signals whenever
 * possible).
 *
 * Note that sending a signal once may not interrupt the thread, because
 * the thread may not be calling a system call at the time the signal was
 * received. So one must keep sending signals periodically until the
 * thread has quit.
 */

// This is one of the things that Java is good at and C++ sucks at. Sigh...

namespace Passenger {

	using namespace boost;
	
	static const int INTERRUPTION_SIGNAL = SIGINT;
	
	/**
	 * Setup system call interruption support.
	 * This function may only be called once. It installs a signal handler
	 * for INTERRUPTION_SIGNAL, so one should not install a different signal
	 * handler for that signal after calling this function.
	 */
	void setupSyscallInterruptionSupport();
	
	/**
	 * Thread class with system call interruption support.
	 */
	class Thread: public thread {
	public:
		template <class F>
		explicit Thread(F f, unsigned int stackSize = 0)
			: thread(f, stackSize) {}
		
		/**
		 * Interrupt the thread. This method behaves just like
		 * boost::thread::interrupt(), but will also respect the interruption
		 * points defined in Passenger::InterruptableCalls.
		 *
		 * Note that an interruption request may get lost, depending on the
		 * current execution point of the thread. Thus, one should call this
		 * method in a loop, until a certain goal condition has been fulfilled.
		 * interruptAndJoin() is a convenience method that implements this
		 * pattern.
		 */
		void interrupt() {
			int ret;
			
			thread::interrupt();
			do {
				ret = pthread_kill(native_handle(),
					INTERRUPTION_SIGNAL);
			} while (ret == EINTR);
		}
		
		/**
		 * Keep interrupting the thread until it's done, then join it.
		 *
		 * @throws boost::thread_interrupted
		 */
		void interruptAndJoin() {
			bool done = false;
			while (!done) {
				interrupt();
				done = timed_join(posix_time::millisec(10));
			}
		}
	};
	
	/**
	 * System call and C library call wrappers with interruption support.
	 * These functions are interruption points, i.e. they throw
	 * boost::thread_interrupted whenever the calling thread is interrupted
	 * by Thread::interrupt() or Thread::interruptAndJoin().
	 */
	namespace InterruptableCalls {
		ssize_t read(int fd, void *buf, size_t count);
		ssize_t write(int fd, const void *buf, size_t count);
		int close(int fd);
		
		int socketpair(int d, int type, int protocol, int sv[2]);
		ssize_t recvmsg(int s, struct msghdr *msg, int flags);
		ssize_t sendmsg(int s, const struct msghdr *msg, int flags);
		int shutdown(int s, int how);
		
		FILE *fopen(const char *path, const char *mode);
		int fclose(FILE *fp);
		
		time_t time(time_t *t);
		int usleep(useconds_t usec);
		int nanosleep(const struct timespec *req, struct timespec *rem);
		
		pid_t fork();
		int kill(pid_t pid, int sig);
		pid_t waitpid(pid_t pid, int *status, int options);
	}

} // namespace Passenger

namespace boost {
namespace this_thread {

	/**
	 * @intern
	 */
	extern thread_specific_ptr<bool> _syscalls_interruptable;
	
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
		bool lastValue;
	public:
		enable_syscall_interruption() {
			if (_syscalls_interruptable.get() == NULL) {
				lastValue = true;
				_syscalls_interruptable.reset(new bool(true));
			} else {
				lastValue = *_syscalls_interruptable;
				*_syscalls_interruptable = true;
			}
		}
		
		~enable_syscall_interruption() {
			*_syscalls_interruptable = lastValue;
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
		bool lastValue;
	public:
		disable_syscall_interruption() {
			if (_syscalls_interruptable.get() == NULL) {
				lastValue = true;
				_syscalls_interruptable.reset(new bool(false));
			} else {
				lastValue = *_syscalls_interruptable;
				*_syscalls_interruptable = false;
			}
		}
		
		~disable_syscall_interruption() {
			*_syscalls_interruptable = lastValue;
		}
	};
	
	/**
	 * Creating an object of this class on the stack will restore the
	 * system call interruption state to what it was before.
	 */
	class restore_syscall_interruption {
	private:
		int lastValue;
	public:
		restore_syscall_interruption(const disable_syscall_interruption &intr) {
			assert(_syscalls_interruptable.get() != NULL);
			lastValue = *_syscalls_interruptable;
			*_syscalls_interruptable = intr.lastValue;
		}
		
		~restore_syscall_interruption() {
			*_syscalls_interruptable = lastValue;
		}
	};

} // namespace this_thread
} // namespace boost

#endif /* _OXT_SYSTEM_CALLS_HPP_ */

