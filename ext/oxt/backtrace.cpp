/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2010 Phusion
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
#include <cstring>
#include "backtrace.hpp"

namespace oxt {

boost::mutex _thread_registration_mutex;
list<thread_registration *> _registered_threads;

// Register main thread.
static initialize_backtrace_support_for_this_thread main_thread_initialization("Main thread");


namespace {
	struct backtrace_data {
		vector<trace_point *> list;
		spin_lock lock;
		
		backtrace_data() {
			list.reserve(50);
		}
	};
}


/*
 * boost::thread_specific_storage is pretty expensive. So we use the __thread
 * keyword whenever possible - that's almost free.
 * GCC supports the __thread keyword on x86 since version 3.3, but versions earlier
 * than 4.1.2 have bugs (http://gcc.gnu.org/ml/gcc-bugs/2006-09/msg02275.html).
 */

#ifndef OXT_GCC_VERSION
	#define OXT_GCC_VERSION (__GNUC__ * 10000 \
                               + __GNUC_MINOR__ * 100 \
                               + __GNUC_PATCHLEVEL__)
#endif

/*
 * FreeBSD 5 supports the __thread keyword, and everything works fine in
 * micro-tests, but in mod_passenger the thread-local variables are initialized
 * to unaligned addresses for some weird reason, thereby causing bus errors.
 *
 * GCC on OpenBSD supports __thread, but any access to such a variable
 * results in a segfault.
 *
 * Solaris does support __thread, but often it's not compiled into default GCC 
 * packages (not to mention it's not available for Sparc). Playing it safe...
 *
 * MacOS X doesn't support __thread at all.
 */
#if OXT_GCC_VERSION >= 40102 && !defined(__FreeBSD__) && \
   !defined(__SOLARIS__) && !defined(__OpenBSD__) && !defined(__APPLE__)
	static __thread backtrace_data *thread_specific_backtrace_data;
	
	void
	_init_backtrace_tls() {
		thread_specific_backtrace_data = new backtrace_data();
	}
	
	void
	_finalize_backtrace_tls() {
		delete thread_specific_backtrace_data;
	}
	
	bool
	_get_backtrace_list_and_its_lock(vector<trace_point *> **backtrace_list, spin_lock **lock) {
		if (OXT_LIKELY(thread_specific_backtrace_data != NULL)) {
			*backtrace_list = &thread_specific_backtrace_data->list;
			*lock = &thread_specific_backtrace_data->lock;
			return true;
		} else {
			return false;
		}
	}
#else
	static thread_specific_ptr<backtrace_data> thread_specific_backtrace_data;
	
	void _init_backtrace_tls() {
		/* Not implemented on purpose.
		 *
		 * It is not safe to use thread_specific_backtrace_data here
		 * because this function may be called by
		 * initialize_backtrace_support_for_this_thread's constructor,
		 * which in turn may be called by the global static variable
		 * main_thread_initialization. C++ does not guarantee initialization
		 * order so thread_specific_backtrace_data may not be usable at
		 * this time.
		 */
	}
	
	void _finalize_backtrace_tls() {
		// Not implemented either.
	}
	
	bool
	_get_backtrace_list_and_its_lock(vector<trace_point *> **backtrace_list, spin_lock **lock) {
		backtrace_data *data;
		
		data = thread_specific_backtrace_data.get();
		if (OXT_UNLIKELY(data == NULL)) {
			data = new backtrace_data();
			thread_specific_backtrace_data.reset(data);
		}
		*backtrace_list = &data->list;
		*lock = &data->lock;
		return true;
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
				const char *source = strrchr(p->source, '/');
				if (source != NULL) {
					source++;
				} else {
					source = p->source;
				}
				result << " (" << source << ":" << p->line << ")";
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

