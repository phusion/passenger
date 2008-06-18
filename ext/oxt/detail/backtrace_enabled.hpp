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
string _format_backtrace(list<trace_point *> *backtrace_list);

/**
 * A single point in a backtrace. Implementation detail - do not use directly!
 *
 * @internal
 */
struct trace_point {
	string function;
	string source;
	unsigned int line;
	bool detached;

	trace_point(const string &function, const string &source, unsigned int line) {
		this->function = function;
		this->source = source;
		this->line = line;
		detached = false;
		boost::mutex::scoped_lock l(_get_backtrace_mutex());
		_get_current_backtrace()->push_back(this);
	}

	~trace_point() {
		if (!detached) {
			boost::mutex::scoped_lock l(_get_backtrace_mutex());
			_get_current_backtrace()->pop_back();
		}
	}

	/**
	 * Tell this trace_point not to remove itself from the backtrace list
	 * upon destruction.
	 */
	void detach() {
		detached = true;
	}

	void update(const string &source, unsigned int line) {
		this->source = source;
		this->line = line;
	}
};

/**
 * Exception class with backtrace support.
 */
class tracable_exception: public exception {
private:
	list<trace_point> backtrace_copy;
	
	void copy_backtrace() {
		boost::mutex::scoped_lock l(_get_backtrace_mutex());
		list<trace_point *>::const_iterator it;
		list<trace_point *> *bt = _get_current_backtrace();
		for (it = bt->begin(); it != bt->end(); it++) {
			backtrace_copy.push_back(**it);
			(*it)->detach();
		}
	}
public:
	tracable_exception() {
		copy_backtrace();
	}
	
	virtual string backtrace() const throw() {
		list<trace_point *> backtrace_list;
		list<trace_point>::const_iterator it;
		
		for (it = backtrace_copy.begin(); it != backtrace_copy.end(); it++) {
			backtrace_list.push_back(const_cast<trace_point *>(&(*it)));
		}
		return _format_backtrace(&backtrace_list);
	}
};

#define TRACE_POINT() oxt::trace_point __p(__PRETTY_FUNCTION__, __FILE__, __LINE__)
#define UPDATE_TRACE_POINT() __p.update(__FILE__, __LINE__)

/**
 * @internal
 */
struct thread_registration {
	string name;
	boost::mutex *backtraceMutex;
	list<trace_point *> *backtrace;
};

/**
 * @internal
 */
class register_thread_with_backtrace {
private:
	thread_registration *registration;
	list<thread_registration *>::iterator it;
public:
	register_thread_with_backtrace(const string &name) {
		registration = new thread_registration();
		registration->name = name;
		registration->backtraceMutex = &_get_backtrace_mutex();
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

