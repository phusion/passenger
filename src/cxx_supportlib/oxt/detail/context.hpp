/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2012-2017 Phusion Holding B.V.
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
#ifndef _OXT_DETAIL_CONTEXT_HPP_
#define _OXT_DETAIL_CONTEXT_HPP_

#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <list>
#include <vector>
#include <string>
#include <pthread.h>
#ifdef __linux__
	#include <sys/types.h>
#endif
#include "../spin_lock.hpp"

namespace oxt {


struct thread_local_context;

typedef boost::shared_ptr<thread_local_context> thread_local_context_ptr;

struct global_context_t {
	boost::mutex next_thread_number_mutex;
	/** Thread numbering begins at 2. The main thread has number 1.
	 * A thread number of 0 is invalid. */
	unsigned int next_thread_number;

	boost::mutex thread_registration_mutex;
	std::list<thread_local_context_ptr> registered_threads;

	global_context_t();
};

struct thread_local_context {
	std::list<thread_local_context_ptr>::iterator iterator;

	pthread_t thread;
	#ifdef __linux__
		pid_t tid;
	#endif
	unsigned int thread_number;
	std::string thread_name;

	/** This lock is normally locked, but only unlocked during an oxt::sycall function,
	 * and is relocked when that function returns. One can use try_lock to find out
	 * whether the code is inside an oxt::syscall function.
	 */
	spin_lock syscall_interruption_lock;

	#ifdef OXT_BACKTRACE_IS_ENABLED
		std::vector<trace_point *> backtrace_list;
		spin_lock backtrace_lock;
	#endif

	static thread_local_context_ptr make_shared_ptr();

	thread_local_context();
};


void set_thread_local_context(const thread_local_context_ptr &ctx);
thread_local_context *get_thread_local_context();


} // namespace oxt

#endif /* _OXT_DETAIL_CONTEXT_HPP_ */
