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

// Actual implementation for backtrace.hpp.

#define OXT_BACKTRACE_IS_ENABLED

#include <boost/current_function.hpp>

namespace oxt {

/**
 * A single point in a backtrace. Creating this object will cause it
 * to push itself to the thread's backtrace list. This backtrace list
 * is stored in a thread local storage, and so is unique for each
 * thread. Upon destruction, the object will pop itself from the thread's
 * backtrace list.
 *
 * Except if you set the 'detached' argument to true.
 */
struct trace_point {
	typedef bool (*DataFunction)(char *output, unsigned int size, void *userData);

	struct detached { };

	const char *function;
	const char *source;
	union {
		const char *data;
		struct {
			DataFunction func;
			void *userData;
		} dataFunc;
	} u;
	unsigned short line;
	bool m_detached;
	bool m_hasDataFunc;

	trace_point(const char *function, const char *source, unsigned short line,
		const char *data = 0);
	trace_point(const char *function, const char *source, unsigned short line,
		DataFunction dataFunc, void *userData, bool detached = false);
	trace_point(const char *function, const char *source, unsigned short line,
		const char *data, const detached &detached_tag);
	~trace_point();
	void update(const char *source, unsigned short line);
};

#define TRACE_POINT() oxt::trace_point __p(BOOST_CURRENT_FUNCTION, __FILE__, __LINE__)
#define TRACE_POINT_WITH_NAME(name) oxt::trace_point __p(name, __FILE__, __LINE__)
#define TRACE_POINT_WITH_DATA(data) oxt::trace_point __p(BOOST_CURRENT_FUNCTION, __FILE__, __LINE__, data)
#define TRACE_POINT_WITH_DATA_FUNCTION(func, userData) \
	oxt::trace_point __p(BOOST_CURRENT_FUNCTION, __FILE__, __LINE__, func, (void *) userData)
#define UPDATE_TRACE_POINT() __p.update(__FILE__, __LINE__)

} // namespace oxt
