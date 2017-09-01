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
#ifndef _OXT_DYNAMIC_THREAD_GROUP_HPP_
#define _OXT_DYNAMIC_THREAD_GROUP_HPP_

#include <vector>
#include <list>
#include <memory>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>

namespace oxt {

using namespace std;
using namespace boost;

/**
 * A thread group is a collection of threads. One can run aggregate
 * operations on it, such as interrupting and joining all threads
 * in the thread group.
 *
 * Unlike boost::thread_group, an oxt::dynamic_thread_group
 * supports oxt::thread, and automatically removes terminated threads
 * from its collection, hence 'dynamic' in the name.
 *
 * Threads in the group are guaranteed to have a shorter life time
 * than the group itself: upon destruction, all threads in the group
 * will be interrupted and joined by calling interrupt_and_join_all().
 */
class dynamic_thread_group {
private:
	struct thread_handle;
	typedef boost::shared_ptr<thread_handle> thread_handle_ptr;

	/** A container which aggregates a thread object
	 * as well as the its own iterator in the 'thread_handles'
	 * member. The latter is used for removing itself from
	 * 'thread_handles'.
	 */
	struct thread_handle {
		list<thread_handle_ptr>::iterator iterator;
		thread *thr;
		bool removed_from_list;

		thread_handle() {
			thr = NULL;
			removed_from_list = false;
		}

		~thread_handle() {
			delete thr;
		}
	};

	/** A mutex which protects thread_handles and nthreads. */
	mutable boost::mutex lock;
	/** The internal list of threads. */
	list<thread_handle_ptr> thread_handles;
	/** The number of threads in this thread group. */
	unsigned int nthreads;

	struct thread_func_data {
		boost::function<void ()> entry_point;
		thread_handle *handle;
	};

	struct thread_func_data_guard {
		thread_func_data *data;

		thread_func_data_guard(thread_func_data *_data)
			: data(_data)
			{ }

		~thread_func_data_guard() {
			delete data;
		}

		void clear() {
			data = NULL;
		}
	};

	struct thread_cleanup {
		dynamic_thread_group *thread_group;
		thread_handle *handle;

		thread_cleanup(dynamic_thread_group *g, thread_handle *h) {
			thread_group = g;
			handle = h;
		}

		~thread_cleanup() {
			boost::this_thread::disable_interruption di;
			boost::this_thread::disable_syscall_interruption dsi;
			boost::lock_guard<boost::mutex> l(thread_group->lock);
			if (!handle->removed_from_list) {
				thread_group->thread_handles.erase(handle->iterator);
				thread_group->nthreads--;
			}
		}
	};

	void thread_main(thread_func_data *data) {
		thread_func_data_guard guard(data);
		thread_cleanup c(this, data->handle);
		data->entry_point();
		// Clear the entry point object now because it may contain bound objects.
		// We want to run the destructor for these objects now, before this thread
		// is removed from the thread group list, so that if anybody calls
		// dynamic_thread_group::join_all() the caller can be sure that the
		// threads are really done.
		data->entry_point = boost::function<void ()>();
	}

public:
	dynamic_thread_group() {
		nthreads = 0;
	}

	~dynamic_thread_group() {
		interrupt_and_join_all();
	}

	/**
	 * Create a new thread that belongs to this thread group.
	 *
	 * @param func The thread main function.
	 * @param name A name for this thread. If the empty string is passed,
	 *             then an auto-generated name will be used.
	 * @param stack_size The stack size for this thread. A value of 0 means
	 *                   that the system's default stack size should be used.
	 * @throws thread_resource_error Cannot create a thread.
	 * @post this->num_threads() == old->num_threads() + 1
	 */
	void create_thread(const boost::function<void ()> &func, const string &name = "", unsigned int stack_size = 0) {
		boost::lock_guard<boost::mutex> l(lock);
		thread_handle_ptr handle(new thread_handle());
		thread_handles.push_back(handle);
		handle->iterator = thread_handles.end();
		handle->iterator--;
		try {
			thread_func_data *data = new thread_func_data();
			thread_func_data_guard guard(data);
			data->entry_point = func;
			data->handle = handle.get();
			handle->thr = new thread(
				boost::bind(&dynamic_thread_group::thread_main, this, data),
				name,
				stack_size
			);
			guard.clear();
			nthreads++;
		} catch (...) {
			thread_handles.erase(handle->iterator);
			throw;
		}
	}

	void interrupt_all() {
		boost::unique_lock<boost::mutex> l(lock);
		list<thread_handle_ptr>::iterator it;
		for (it = thread_handles.begin(); it != thread_handles.end(); it++) {
			(*it)->thr->interrupt();
		}
	}

	/**
	 * Interrupt and join all threads in this group.
	 *
	 * @post num_threads() == 0
	 */
	void interrupt_and_join_all(bool interruptSyscalls = true) {
		/* While interrupting and joining the threads, each thread
		 * will try to lock the mutex and remove itself from
		 * 'thread_handles'. We want to avoid deadlocks so we
		 * empty 'thread_handles' in the critical section and
		 * join the threads outside the critical section.
		 */
		boost::unique_lock<boost::mutex> l(lock);
		list<thread_handle_ptr> thread_handles_copy;
		list<thread_handle_ptr>::iterator it;
		thread_handle_ptr handle;
		unsigned int nthreads_copy = nthreads;
		vector<thread *> threads;
		unsigned int i = 0;

		// We make a copy so that the handles aren't destroyed prematurely.
		threads.resize(nthreads);
		thread_handles_copy = thread_handles;
		for (it = thread_handles.begin(); it != thread_handles.end(); it++, i++) {
			handle = *it;
			handle->removed_from_list = true;
			threads[i] = handle->thr;
		}
		thread_handles.clear();
		nthreads = 0;

		l.unlock();
		thread::interrupt_and_join_multiple(&threads[0], nthreads_copy, interruptSyscalls);
	}

	void join_all() {
		// See comments from interrupt_and_join_all().
		boost::unique_lock<boost::mutex> l(lock);
		list<thread_handle_ptr> thread_handles_copy;
		list<thread_handle_ptr>::iterator it;
		thread_handle_ptr handle;
		unsigned int nthreads_copy = nthreads;
		vector<thread *> threads;
		unsigned int i = 0;

		// We make a copy so that the handles aren't destroyed prematurely.
		threads.resize(nthreads);
		thread_handles_copy = thread_handles;
		for (it = thread_handles.begin(); it != thread_handles.end(); it++, i++) {
			handle = *it;
			handle->removed_from_list = true;
			threads[i] = handle->thr;
		}
		thread_handles.clear();
		nthreads = 0;

		l.unlock();
		for (i = 0; i < nthreads_copy; i++) {
			threads[i]->join();
		}
	}

	/**
	 * Returns the number of threads currently in this thread group.
	 */
	unsigned int num_threads() const {
		boost::lock_guard<boost::mutex> l(lock);
		return nthreads;
	}
};

} // namespace oxt

#endif /* _OXT_DYNAMIC_THREAD_GROUP_HPP_ */
