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

// FreeBSD 5 supports the __thread keyword, but doesn't initialize such
// variables to the correct default value.
#if defined(GCC_IS_3_3_OR_HIGHER) && !defined(__FreeBSD__)
	static __thread spin_lock *backtrace_lock = NULL;
	static __thread vector<trace_point *> *current_backtrace = NULL;
	
	void
	_init_backtrace_tls() {
		backtrace_lock = new spin_lock();
		current_backtrace = new vector<trace_point *>();
		current_backtrace->reserve(50);
	}
	
	void
	_finalize_backtrace_tls() {
		delete backtrace_lock;
		delete current_backtrace;
	}
	
	spin_lock *
	_get_backtrace_lock() {
		return backtrace_lock;
	}
	
	vector<trace_point *> *
	_get_current_backtrace() {
		return current_backtrace;
	}
#else
	static thread_specific_ptr<spin_lock> backtrace_lock;
	static thread_specific_ptr< vector<trace_point *> > current_backtrace;
	
	void _init_backtrace_tls() {
		// Not implemented.
	}
	
	void _finalize_backtrace_tls() {
		// Not implemented.
	}

	spin_lock *
	_get_backtrace_lock() {
		spin_lock *result;
	
		result = backtrace_lock.get();
		if (OXT_UNLIKELY(result == NULL)) {
			result = new spin_lock();
			backtrace_lock.reset(result);
		}
		return result;
	}

	vector<trace_point *> *
	_get_current_backtrace() {
		vector<trace_point *> *result;
	
		result = current_backtrace.get();
		if (OXT_UNLIKELY(result == NULL)) {
			result = new vector<trace_point *>();
			current_backtrace.reset(result);
		}
		return result;
	}
#endif

template<typename Iterable, typename ReverseIterator> static string
format_backtrace(Iterable backtrace_list) {
	if (backtrace_list->empty()) {
		return "     (empty)";
	} else {
		stringstream result;
		ReverseIterator it;
		
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

string
_format_backtrace(const list<trace_point *> *backtrace_list) {
	return format_backtrace<
		const list<trace_point *> *,
		list<trace_point *>::const_reverse_iterator
	>(backtrace_list);
}

string
_format_backtrace(const vector<trace_point *> *backtrace_list) {
	return format_backtrace<
		const vector<trace_point *> *,
		vector<trace_point *>::const_reverse_iterator
	>(backtrace_list);
}

} // namespace oxt

#endif

