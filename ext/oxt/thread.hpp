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
#ifndef _OXT_THREAD_HPP_
#define _OXT_THREAD_HPP_

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "system_calls.hpp"
#include "backtrace.hpp"
#ifdef OXT_BACKTRACE_IS_ENABLED
	#include <sstream>
#endif

namespace oxt {

extern boost::mutex _next_thread_number_mutex;
extern unsigned int _next_thread_number;thread_registration *registration;

/**
 * Enhanced thread class with support for:
 * - user-defined stack size.
 * - system call interruption.
 * - backtraces.
 */
class thread: public boost::thread {
private:
	struct thread_data {
		std::string name;
		thread_registration *registration;
		boost::mutex registration_lock;
		bool done;
	};
	
	typedef boost::shared_ptr<thread_data> thread_data_ptr;
	
	thread_data_ptr data;

	template<typename Callable>
	static void register_thread(Callable func, thread_data_ptr data) {
		register_thread_with_backtrace r(name);
		data->registration = r.registration;
		
		TRACE_POINT();
		func();
		
		boost::mutex::scoped_lock l(data->registration_lock);
		data->registration = NULL;
		data->done = true;
	}
	
	template<typename Callable, typename AnotherCallable>
	AnotherCallable create_thread_function(Callable func, const std::string &name) {
		data = thread_data_ptr(new thread_data());
		
		if (name.empty()) {
			boost::mutex::scoped_lock l(_next_thread_number_mutex);
			std::stringstream str;
			
			str << "Thread #" << _next_thread_number;
			_next_thread_number++;
			data->name = str.str();
		} else {
			data->name = name;
		}
		
		data->registration = NULL;
		data->done = false;
		return boost::bind(register_thread, func, data);
	}
	
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
	 *     size is used.
	 * @pre func must be copyable.
	 * @throws boost::thread_resource_error Something went wrong during
	 *     creation of the thread.
	 */
	template<typename Callable>
	explicit thread(Callable func, const std::string &name = "", unsigned int stack_size = 0)
	       : boost::thread(
	             create_thread_function(func, name),
	             stack_size
	         ) { }
	
	/**
	 * Return this thread's name. The name was set during construction.
	 */
	std::string name() const throw() {
		return data->name;
	}
	
	/**
	 * Return this thread's backtrace.
	 */
	std::string backtrace() const throw() {
		boost::mutex::scoped_lock l(data->registration_lock);
		if (data->registration == NULL) {
			if (data->done) {
				return "     (no backtrace: thread has quit)";
			} else {
				return "     (no backtrace: thread hasn't been started yet)";
			}
		} else {
			boost::mutex::scoped_lock l2(*data->registration->backtrace_mutex);
			return _format_backtrace(data->registration->backtrace);
		}
	}
	
	/**
	 * Interrupt the thread. This method behaves just like
	 * boost::thread::interrupt(), but will also respect the interruption
	 * points defined in oxt::syscalls.
	 *
	 * Note that an interruption request may get lost, depending on the
	 * current execution point of the thread. Thus, one should call this
	 * method in a loop, until a certain goal condition has been fulfilled.
	 * interrupt_and_join() is a convenience method that implements this
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
	 * @throws boost::thread_interrupted The calling thread has been
	 *    interrupted before we could join this thread.
	 */
	void interrupt_and_join() {
		bool done = false;
		while (!done) {
			interrupt();
			done = timed_join(boost::posix_time::millisec(10));
		}
	}
};

} // namespace oxt

#endif /* _OXT_THREAD_HPP_ */

