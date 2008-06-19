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

// Actual implementation for backtrace.hpp.

#define OXT_BACKTRACE_IS_ENABLED

#include <boost/thread/mutex.hpp>
#include <exception>
#include <string>
#include <list>
#include "../macros.hpp"

namespace oxt {

using namespace std;
using namespace boost;
class trace_point;
class tracable_exception;
class thread_registration;

extern boost::mutex _thread_registration_mutex;
extern list<thread_registration *> _registered_threads;

boost::mutex &_get_backtrace_mutex();
list<trace_point *> *_get_current_backtrace();
string _format_backtrace(const list<trace_point *> *backtrace_list);

/**
 * A single point in a backtrace. Implementation detail - do not use directly!
 *
 * @internal
 */
struct trace_point {
	const char *function;
	const char *source;
	unsigned int line;
	bool m_detached;

	trace_point(const char *function, const char *source, unsigned int line, bool detached = false) {
		this->function = function;
		this->source = source;
		this->line = line;
		m_detached = detached;
		if (OXT_LIKELY(!detached)) {
			boost::mutex::scoped_lock l(_get_backtrace_mutex());
			_get_current_backtrace()->push_back(this);
		}
	}

	~trace_point() {
		if (OXT_LIKELY(!m_detached)) {
			boost::mutex::scoped_lock l(_get_backtrace_mutex());
			_get_current_backtrace()->pop_back();
		}
	}

	void update(const char *source, unsigned int line) {
		this->source = source;
		this->line = line;
	}
};

#define TRACE_POINT() oxt::trace_point __p(__PRETTY_FUNCTION__, __FILE__, __LINE__)
#define TRACE_POINT_WITH_NAME(name) oxt::trace_point __p(name, __FILE__, __LINE__)
#define UPDATE_TRACE_POINT() __p.update(__FILE__, __LINE__)

/**
 * @internal
 */
struct thread_registration {
	string name;
	boost::mutex *backtrace_mutex;
	list<trace_point *> *backtrace;
};

/**
 * @internal
 */
struct register_thread_with_backtrace {
	thread_registration *registration;
	list<thread_registration *>::iterator it;
	
	register_thread_with_backtrace(const string &name) {
		registration = new thread_registration();
		registration->name = name;
		registration->backtrace_mutex = &_get_backtrace_mutex();
		registration->backtrace = _get_current_backtrace();
		
		boost::mutex::scoped_lock l(_thread_registration_mutex);
		_registered_threads.push_back(registration);
		it = _registered_threads.end();
		it--;
	}
	
	~register_thread_with_backtrace() {
		boost::mutex::scoped_lock l(_thread_registration_mutex);
		_registered_threads.erase(it);
		delete registration;
	}
};

} // namespace oxt

