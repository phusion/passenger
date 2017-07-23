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
#ifndef _OXT_BACKTRACE_HPP_
#define _OXT_BACKTRACE_HPP_

/**
 * Portable C++ backtrace support.
 *
 * C++ doesn't provide a builtin, automatic, portable way of obtaining
 * backtraces. Obtaining a backtrace via a debugger (e.g. gdb) is very
 * expensive. Furthermore, not all machines have a debugger installed.
 * This makes it very hard to debug problems on production servers.
 *
 * This file provides a portable way of specifying and obtaining
 * backtraces. Via oxt::thread::all_backtraces(), it is even possible
 * to obtain the backtraces of all running threads.
 *
 * <h2>Initialization</h2>
 * Every thread that is to contain backtrace information <b>must</b> be
 * initialized. This is done by creating a `thread_local_context` object,
 * and calling `set_thread_local_context()` with that object.
 * `oxt::initialize()` automatically does this for the calling thread,
 * and `oxt::thread` does this automatically as well.
 *
 * <h2>Basic usage</h2>
 * Backtrace points must be specified manually in the code using TRACE_POINT().
 * The TracableException class allows one to obtain the backtrace at the moment
 * the exception object was created.
 *
 * For example:
 * @code
 * void foo() {
 *     TRACE_POINT();
 *     do_something();
 *     bar();
 *     do_something_else();
 * }
 *
 * void bar() {
 *     TRACE_POINT();
 *     throw TracableException();
 * }
 * @encode
 *
 * One can obtain the backtrace string, as follows:
 * @code
 * try {
 *     foo();
 * } catch (const TracableException &e) {
 *     cout << "Something bad happened:\n" << e.backtrace();
 * }
 * @endcode
 *
 * This will print something like:
 * @code
 * Something bad happened:
 *     in 'bar' (example.cpp:123)
 *     in 'foo' (example.cpp:117)
 *     in 'example_function' (example.cpp:456)
 * @encode
 *
 * <h2>Making sure the line number is correct</h2>
 * A TRACE_POINT() call will add a backtrace point for the source line on
 * which it is written. However, this causes an undesirable effect in long
 * functions:
 * @code
 * 100   void some_long_function() {
 * 101       TRACE_POINT();
 * 102       do_something();
 * 103       for (...) {
 * 104           do_something();
 * 105       }
 * 106       do_something_else();
 * 107
 * 108       if (!write_file()) {
 * 109           throw TracableException();
 * 110       }
 * 111   }
 * @endcode
 * You will want the thrown exception to show a backtrace line number that's
 * near line 109. But as things are now, the backtrace will show line 101.
 *
 * This can be solved by calling UPDATE_TRACE_POINT() from time to time:
 * @code
 * 100   void some_long_function() {
 * 101       TRACE_POINT();
 * 102       do_something();
 * 103       for (...) {
 * 104           do_something();
 * 105       }
 * 106       do_something_else();
 * 107
 * 108       if (!write_file()) {
 * 109           UPDATE_TRACE_POINT();   // Added!
 * 110           throw TracableException();
 * 111       }
 * 112   }
 * @endcode
 *
 * <h2>Compilation options</h2>
 * Define OXT_DISABLE_BACKTRACES to disable backtrace support. The backtrace
 * functions as provided by this header will become empty stubs.
 */

#if defined(NDEBUG) || defined(OXT_DISABLE_BACKTRACES)
	#include "detail/backtrace_disabled.hpp"
#else
	#include "detail/backtrace_enabled.hpp"
#endif

#endif /* _OXT_BACKTRACE_HPP_ */
