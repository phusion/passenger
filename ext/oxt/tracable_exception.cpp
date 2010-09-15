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
#include "tracable_exception.hpp"
#include "backtrace.hpp"

#ifdef OXT_BACKTRACE_IS_ENABLED

#include <boost/thread/mutex.hpp>
#include "macros.hpp"

namespace oxt {

using namespace std;

tracable_exception::tracable_exception() {
	vector<trace_point *> *backtrace_list;
	spin_lock *lock;
	if (OXT_LIKELY(_get_backtrace_list_and_its_lock(&backtrace_list, &lock))) {
		spin_lock::scoped_lock l(*lock);
		vector<trace_point *>::const_iterator it;
		
		for (it = backtrace_list->begin(); it != backtrace_list->end(); it++) {
			trace_point *p = new trace_point(
				(*it)->function,
				(*it)->source,
				(*it)->line,
				true);
			backtrace_copy.push_back(p);
		}
	}
}

tracable_exception::tracable_exception(const tracable_exception &other) {
	list<trace_point *>::const_iterator it;
	for (it = other.backtrace_copy.begin(); it != other.backtrace_copy.end(); it++) {
		trace_point *p = new trace_point(
			(*it)->function,
			(*it)->source,
			(*it)->line,
			true);
		backtrace_copy.push_back(p);
	}
}

tracable_exception::~tracable_exception() throw() {
	list<trace_point *>::iterator it;
	for (it = backtrace_copy.begin(); it != backtrace_copy.end(); it++) {
		delete *it;
	}
}

string
tracable_exception::backtrace() const throw() {
	return _format_backtrace(&backtrace_copy);
}

const char *
tracable_exception::what() const throw() {
	return "oxt::tracable_exception";
}

} // namespace oxt

#endif

