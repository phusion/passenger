/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _OXT_THREAD_HPP_
#define _OXT_THREAD_HPP_

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "macros.hpp"
#include "system_calls.hpp"
#include "detail/context.hpp"
#include <string>
#include <list>
#include <unistd.h>
#include <limits.h>  // for PTHREAD_STACK_MIN

namespace oxt {

#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
	/** A thread-specific signature that you can use for identifying threads.
	 * It defaults to NULL. You have to set it manually in every thread.
	 */
	extern __thread void *thread_signature;
#endif

/**
 * Enhanced thread class with support for:
 * - user-defined stack size.
 * - system call interruption.
 * - backtraces.
 */
class thread: public boost::thread {
private:
	thread_local_context_ptr context;

	static std::string make_thread_name(const std::string &given_name);
	static void thread_main(const boost::function<void ()> func, thread_local_context_ptr ctx);

public:
	/**
	 * Create a new thread.
	 *
	 * @param func A function object which will be called as the thread's
	 *     main function. This object must be copyable. <tt>func</tt> is
	 *     copied into storage managed internally by the thread library,
	 *     and that copy is invoked on a newly-created thread of execution.
	 * @param name A name for this thread. If an empty string is given, then
	 *     a name will be automatically chosen.
	 * @param stack_size The stack size, in bytes, that the thread should
	 *     have. If 0 is specified, the operating system's default stack
	 *     size is used. If non-zero is specified, and the size is smaller
	 *     than the operating system's minimum stack size, then the operating
	 *     system's minimum stack size will be used.
	 * @pre func must be copyable.
	 * @throws boost::thread_resource_error Something went wrong during
	 *     creation of the thread.
	 */
	explicit thread(const boost::function<void ()> func,
		const std::string &name = std::string(),
		unsigned int stack_size = 0)
		: boost::thread()
	{
		context = thread_local_context::make_shared_ptr();
		context->thread_name = make_thread_name(name);
		thread_info = make_thread_info(boost::bind(thread_main, func, context));

		unsigned long min_stack_size;
		bool stack_min_size_defined;
		bool round_stack_size;

		#ifdef PTHREAD_STACK_MIN
			// PTHREAD_STACK_MIN may not be a constant macro so we need
			// to evaluate it dynamically.
			min_stack_size = PTHREAD_STACK_MIN;
			stack_min_size_defined = true;
		#else
			// Assume minimum stack size is 128 KB.
			min_stack_size = 128 * 1024;
			stack_min_size_defined = false;
		#endif
		if (stack_size != 0 && stack_size < min_stack_size) {
			stack_size = min_stack_size;
			round_stack_size = !stack_min_size_defined;
		} else {
			round_stack_size = true;
		}

		if (round_stack_size) {
			// Round stack size up to page boundary.
			long page_size;
			#if defined(_SC_PAGESIZE)
				page_size = sysconf(_SC_PAGESIZE);
			#elif defined(_SC_PAGE_SIZE)
				page_size = sysconf(_SC_PAGE_SIZE);
			#elif defined(PAGESIZE)
				page_size = sysconf(PAGESIZE);
			#elif defined(PAGE_SIZE)
				page_size = sysconf(PAGE_SIZE);
			#else
				page_size = getpagesize();
			#endif
			if (stack_size % page_size != 0) {
				stack_size = stack_size - (stack_size % page_size) + page_size;
			}
		}

		attributes attrs;
		attrs.set_stack_size(stack_size);
		start_thread(attrs);
	}

	/**
	 * Return this thread's name. The name was set during construction.
	 */
	std::string name() const throw();

	/**
	 * Return the current backtrace of the thread of execution, as a string.
	 */
	std::string backtrace() const throw();

	/**
	 * Return the backtraces of all oxt::thread threads, as well as that of the
	 * main thread, in a nicely formatted string.
	 */
	static std::string all_backtraces() throw();

	/**
	 * Return the current thread's backtrace, in a nicely formatted string.
	 */
	static std::string current_backtrace() throw();

	/**
	 * Interrupt the thread. This method behaves just like
	 * boost::thread::interrupt(), but if <em>interruptSyscalls</em> is true
	 * then it will also respect the interruption points defined in
	 * oxt::syscalls.
	 *
	 * Note that an interruption request may get lost, depending on the
	 * current execution point of the thread. Thus, one should call this
	 * method in a loop, until a certain goal condition has been fulfilled.
	 * interrupt_and_join() is a convenience method that implements this
	 * pattern.
	 */
	void interrupt(bool interruptSyscalls = true);

	/**
	 * Keep interrupting the thread until it's done, then join it.
	 *
	 * @param interruptSyscalls Whether oxt::syscalls calls should also
	 *    be eligable for interruption.
	 * @throws boost::thread_interrupted The calling thread has been
	 *    interrupted before we could join this thread.
	 */
	void interrupt_and_join(bool interruptSyscalls = true) {
		bool done = false;
		while (!done) {
			interrupt(interruptSyscalls);
			done = timed_join(boost::posix_time::millisec(10));
		}
	}

	/**
	 * Keep interrupting the thread until it's done, then join it.
	 * This method will keep trying for at most <em>timeout</em> milliseconds.
	 *
	 * @param timeout The maximum number of milliseconds that this method
	 *                should keep trying.
	 * @param interruptSyscalls Whether oxt::syscalls calls should also
	 *    be eligable for interruption.
	 * @return True if the thread was successfully joined, false if the
	 *         timeout has been reached.
	 * @throws boost::thread_interrupted The calling thread has been
	 *    interrupted before we could join this thread.
	 */
	bool interrupt_and_join(unsigned int timeout, bool interruptSyscalls = true) {
		bool joined = false, timed_out = false;
		boost::posix_time::ptime deadline =
			boost::posix_time::microsec_clock::local_time() +
			boost::posix_time::millisec(timeout);
		while (!joined && !timed_out) {
			interrupt(interruptSyscalls);
			joined = timed_join(boost::posix_time::millisec(10));
			timed_out = !joined && boost::posix_time::microsec_clock::local_time() > deadline;
		}
		return joined;
	}

	/**
	 * Interrupt and join multiple threads in a way that's more efficient than calling
	 * interrupt_and_join() on each thread individually. It iterates over all threads,
	 * interrupts each one without joining it, then waits until at least one thread
	 * is joinable. This is repeated until all threads are joined.
	 *
	 * @param threads An array of threads to join.
	 * @param size The number of elements in <em>threads</em>.
	 * @param interruptSyscalls Whether oxt::syscalls calls should also
	 *    be eligable for interruption.
	 * @throws boost::thread_interrupted The calling thread has been
	 *    interrupted before all threads have been joined. Some threads
	 *    may have been successfully joined while others haven't.
	 */
	static void interrupt_and_join_multiple(oxt::thread **threads, unsigned int size,
		bool interruptSyscalls = true)
	{
		std::list<oxt::thread *> remaining_threads;
		std::list<oxt::thread *>::iterator it, current;
		oxt::thread *thread;
		unsigned int i;

		for (i = 0; i < size; i++) {
			remaining_threads.push_back(threads[i]);
		}

		while (!remaining_threads.empty()) {
			for (it = remaining_threads.begin(); it != remaining_threads.end(); it++) {
				thread = *it;
				thread->interrupt(interruptSyscalls);
			}
			for (it = remaining_threads.begin(); it != remaining_threads.end(); it++) {
				thread = *it;
				if (thread->timed_join(boost::posix_time::millisec(0))) {
					current = it;
					it--;
					remaining_threads.erase(current);
				}
			}
			if (!remaining_threads.empty()) {
				syscalls::usleep(10000);
			}
		}
	}
};

/**
 * Like boost::lock_guard, but is interruptable.
 */
template<typename TimedLockable>
class interruptable_lock_guard {
private:
	TimedLockable &mutex;
public:
	interruptable_lock_guard(TimedLockable &m): mutex(m) {
		bool locked = false;

		while (!locked) {
			locked = m.timed_lock(boost::posix_time::milliseconds(20));
			if (!locked) {
				boost::this_thread::interruption_point();
			}
		}
	}

	~interruptable_lock_guard() {
		mutex.unlock();
	}
};

} // namespace oxt

#endif /* _OXT_THREAD_HPP_ */

