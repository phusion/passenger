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
#if !(defined(NDEBUG) || defined(OXT_DISABLE_BACKTRACES))

#include <boost/thread/mutex.hpp>
#include <boost/thread/tss.hpp>
#include <sstream>
#include "backtrace.hpp"

namespace oxt {

boost::mutex _thread_registration_mutex;
list<thread_registration *> _registered_threads;

struct init_main_thread_tls {
	init_main_thread_tls() {
		_init_backtrace_tls();
	}
	
	~init_main_thread_tls() {
		_finalize_backtrace_tls();
	}
};

// Register main thread.
static init_main_thread_tls imtl;
static register_thread_with_backtrace main_thread_registration("Main thread");
static trace_point main_thread_entry_point("main thread entry point", NULL, 0);

/*
 * boost::thread_specific_storage is pretty expensive. So we use the __thread
 * keyword whenever possible - that's almost free.
 * GCC supports the __thread keyword on x86 since version 3.3. Not sure
 * about other architectures.
 */

#if defined(__GNUC__) && (                       \
   __GNUC__ > 3 || (                             \
       __GNUC__ == 3 && __GNUC_MINOR__ >= 3      \
   )                                             \
)
	#define GCC_IS_3_3_OR_HIGHER
#endif

#ifdef GCC_IS_3_3_OR_HIGHER
	static __thread boost::mutex *backtrace_mutex = NULL;
	static __thread list<trace_point *> *current_backtrace = NULL;
	
	void
	_init_backtrace_tls() {
		backtrace_mutex = new boost::mutex();
		current_backtrace = new list<trace_point *>();
	}
	
	void
	_finalize_backtrace_tls() {
		delete backtrace_mutex;
		delete current_backtrace;
	}
	
	boost::mutex *
	_get_backtrace_mutex() {
		return backtrace_mutex;
	}
	
	list<trace_point *> *
	_get_current_backtrace() {
		return current_backtrace;
	}
#else
	static thread_specific_ptr<boost::mutex> backtrace_mutex;
	static thread_specific_ptr< list<trace_point *> > current_backtrace;
	
	void _init_backtrace_tls() {
		// Not implemented.
	}
	
	void _finalize_backtrace_tls() {
		// Not implemented.
	}

	boost::mutex *
	_get_backtrace_mutex() {
		boost::mutex *result;
	
		result = backtrace_mutex.get();
		if (OXT_UNLIKELY(result == NULL)) {
			result = new boost::mutex();
			backtrace_mutex.reset(result);
		}
		return result;
	}

	list<trace_point *> *
	_get_current_backtrace() {
		list<trace_point *> *result;
	
		result = current_backtrace.get();
		if (OXT_UNLIKELY(result == NULL)) {
			result = new list<trace_point *>();
			current_backtrace.reset(result);
		}
		return result;
	}
#endif

string
_format_backtrace(const list<trace_point *> *backtrace_list) {
	if (backtrace_list->empty()) {
		return "     (empty)";
	} else {
		stringstream result;
		list<trace_point *>::const_reverse_iterator it;
		
		for (it = backtrace_list->rbegin(); it != backtrace_list->rend(); it++) {
			trace_point *p = *it;
		
			result << "     in '" << p->function << "'";
			if (p->source != NULL) {
				result << " (" << p->source << ":" << p->line << ")";
			}
			result << endl;
		}
		return result.str();
	}
}

} // namespace oxt

#endif

