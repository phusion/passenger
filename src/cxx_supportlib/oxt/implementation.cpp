/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2008-2025 Asynchronous B.V.
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

#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <pthread.h>
#include "tracable_exception.hpp"
#include "backtrace.hpp"
#include "initialize.hpp"
#include "macros.hpp"
#include "thread.hpp"
#include "spin_lock.hpp"
#include "detail/context.hpp"
#if defined(__linux__)
	#include <sys/syscall.h>
#elif defined(__FreeBSD__)
	#include <pthread_np.h> // for pthread_set_name_np()
#endif

#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
	#include <cassert>
	#include <sstream>
	#include <cstring>
#endif
#include <cstring>


namespace oxt {

using namespace std;
using namespace boost;


static global_context_t *global_context = NULL;


/*
 * boost::thread_specific_storage is pretty expensive. So we use the __thread
 * keyword whenever possible - that's almost free.
 */
#ifdef OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
	static __thread thread_local_context_ptr *local_context = NULL;
	__thread void *thread_signature = NULL;

	static void
	init_thread_local_context_support() {
		/* Do nothing. */
	}

	void
	set_thread_local_context(const thread_local_context_ptr &ctx) {
		local_context = new thread_local_context_ptr(ctx);
	}

	static void
	free_thread_local_context() {
		delete local_context;
		local_context = NULL;
	}

	thread_local_context *
	get_thread_local_context() {
		if (OXT_LIKELY(local_context != NULL)) {
			return local_context->get();
		} else {
			return NULL;
		}
	}
#else
	/*
	 * This is a *pointer* to a thread_specific_ptr because, once
	 * we've created a thread_specific_ptr, we never want to destroy it
	 * in order to avoid C++'s global variable destruction. All kinds
	 * of cleanup code may depend on local_context and because global
	 * variable destruction order is undefined, we just want to keep
	 * this object alive until the OS cleans it up.
	 */
	static thread_specific_ptr<thread_local_context_ptr> *local_context = NULL;

	static void
	init_thread_local_context_support() {
		local_context = new thread_specific_ptr<thread_local_context_ptr>();
	}

	void
	set_thread_local_context(const thread_local_context_ptr &ctx) {
		if (local_context != NULL) {
			local_context->reset(new thread_local_context_ptr(ctx));
		}
	}

	static void
	free_thread_local_context() {
		if (local_context != NULL) {
			local_context->reset();
		}
	}

	thread_local_context *
	get_thread_local_context() {
		if (OXT_LIKELY(local_context != NULL)) {
			thread_local_context_ptr *pointer = local_context->get();
			if (OXT_LIKELY(pointer != NULL)) {
				return pointer->get();
			} else {
				return NULL;
			}
		} else {
			return NULL;
		}
	}
#endif


#ifdef OXT_BACKTRACE_IS_ENABLED

trace_point::trace_point(const char *_function, const char *_source, unsigned short _line,
	const char *_data)
	: function(_function),
	  source(_source),
	  line(_line),
	  m_detached(false),
	  m_hasDataFunc(false)
{
	thread_local_context *ctx = get_thread_local_context();
	if (OXT_LIKELY(ctx != NULL)) {
		spin_lock::scoped_lock l(ctx->backtrace_lock);
		ctx->backtrace_list.push_back(this);
	} else {
		m_detached = true;
	}
	u.data = _data;
}

trace_point::trace_point(const char *_function, const char *_source, unsigned short _line,
	DataFunction _dataFunc, void *_userData, bool detached)
	: function(_function),
	  source(_source),
	  line(_line),
	  m_detached(detached),
	  m_hasDataFunc(true)
{
	if (!detached) {
		thread_local_context *ctx = get_thread_local_context();
		if (OXT_LIKELY(ctx != NULL)) {
			spin_lock::scoped_lock l(ctx->backtrace_lock);
			ctx->backtrace_list.push_back(this);
		} else {
			m_detached = true;
		}
	}
	u.dataFunc.func = _dataFunc;
	u.dataFunc.userData = _userData;
}

trace_point::trace_point(const char *_function, const char *_source, unsigned short _line,
	const char *_data, const detached &detached_tag)
	: function(_function),
	  source(_source),
	  line(_line),
	  m_detached(true),
	  m_hasDataFunc(false)
{
	u.data = _data;
}

trace_point::~trace_point() {
	if (OXT_LIKELY(!m_detached)) {
		thread_local_context *ctx = get_thread_local_context();
		if (OXT_LIKELY(ctx != NULL)) {
			spin_lock::scoped_lock l(ctx->backtrace_lock);
			assert(!ctx->backtrace_list.empty());
			ctx->backtrace_list.pop_back();
		}
	}
}

void
trace_point::update(const char *source, unsigned short line) {
	this->source = source;
	this->line = line;
}


tracable_exception::tracable_exception() {
	thread_local_context *ctx = get_thread_local_context();
	if (OXT_LIKELY(ctx != NULL)) {
		spin_lock::scoped_lock l(ctx->backtrace_lock);
		vector<trace_point *>::const_iterator it, end = ctx->backtrace_list.end();

		backtrace_copy.reserve(ctx->backtrace_list.size());
		for (it = ctx->backtrace_list.begin(); it != end; it++) {
			trace_point *p;
			if ((*it)->m_hasDataFunc) {
				p = new trace_point(
					(*it)->function,
					(*it)->source,
					(*it)->line,
					(*it)->u.dataFunc.func,
					(*it)->u.dataFunc.userData,
					true);
			} else {
				p = new trace_point(
					(*it)->function,
					(*it)->source,
					(*it)->line,
					(*it)->u.data,
					trace_point::detached());
			}
			backtrace_copy.push_back(p);
		}
	}
}

tracable_exception::tracable_exception(const tracable_exception &other)
	: std::exception()
{
	vector<trace_point *>::const_iterator it, end = other.backtrace_copy.end();
	backtrace_copy.reserve(other.backtrace_copy.size());
	for (it = other.backtrace_copy.begin(); it != end; it++) {
		trace_point *p;
		if ((*it)->m_hasDataFunc) {
			p = new trace_point(
				(*it)->function,
				(*it)->source,
				(*it)->line,
				(*it)->u.dataFunc.func,
				(*it)->u.dataFunc.userData,
				true);
		} else {
			p = new trace_point(
				(*it)->function,
				(*it)->source,
				(*it)->line,
				(*it)->u.data,
				trace_point::detached());
		}
		backtrace_copy.push_back(p);
	}
}

tracable_exception::tracable_exception(const no_backtrace &tag)
	: std::exception()
{
	// Do nothing.
}

tracable_exception &
tracable_exception::operator=(const tracable_exception &other) {
	if (this != &other) {
		// Clean up existing backtrace points
		for (auto p: backtrace_copy) {
			delete p;
		}
		backtrace_copy.clear();

		// Copy backtrace points from other
		backtrace_copy.reserve(other.backtrace_copy.size());
		for (const auto p2: other.backtrace_copy) {
			trace_point *p;
			if (p2->m_hasDataFunc) {
				p = new trace_point(
					p2->function,
					p2->source,
					p2->line,
					p2->u.dataFunc.func,
					p2->u.dataFunc.userData,
					true);
			} else {
				p = new trace_point(
					p2->function,
					p2->source,
					p2->line,
					p2->u.data,
					trace_point::detached());
			}
			backtrace_copy.push_back(p);
		}
	}
	return *this;
}

tracable_exception::~tracable_exception() throw() {
	vector<trace_point *>::iterator it, end = backtrace_copy.end();
	for (it = backtrace_copy.begin(); it != end; it++) {
		delete *it;
	}
}

template<typename Collection>
static string
format_backtrace(const Collection &backtrace_list) {
	if (backtrace_list.empty()) {
		return "     (empty)";
	} else {
		backtrace_list.rbegin();
		stringstream result;
		typename Collection::const_reverse_iterator it;

		for (it = backtrace_list.rbegin(); it != backtrace_list.rend(); it++) {
			const trace_point *p = *it;

			result << "     in '" << p->function << "'";
			if (p->source != NULL) {
				const char *source = strrchr(p->source, '/');
				if (source != NULL) {
					source++;
				} else {
					source = p->source;
				}
				result << " (" << source << ":" << p->line << ")";
				if (p->m_hasDataFunc) {
					if (p->u.dataFunc.func != NULL) {
						char buf[64];

						memset(buf, 0, sizeof(buf));
						if (p->u.dataFunc.func(buf, sizeof(buf) - 1, p->u.dataFunc.userData)) {
							buf[63] = '\0';
							result << " -- " << buf;
						}
					}
				} else if (p->u.data != NULL) {
					result << " -- " << p->u.data;
				}
			}
			result << endl;
		}
		return result.str();
	}
}

string
tracable_exception::backtrace() const throw() {
	return format_backtrace< vector<trace_point *> >(backtrace_copy);
}

const char *
tracable_exception::what() const throw() {
	return "oxt::tracable_exception";
}

#endif /* OXT_BACKTRACE_IS_ENABLED */


void
initialize() {
	global_context = new global_context_t();
	init_thread_local_context_support();
	thread_local_context_ptr ctx = thread_local_context::make_shared_ptr();
	ctx->thread_number = 1;
	ctx->thread_name = "Main thread";
	set_thread_local_context(ctx);

	ctx->thread = pthread_self();
	global_context->registered_threads.push_back(ctx);
	ctx->iterator = global_context->registered_threads.end();
	ctx->iterator--;
}

void shutdown() {
	free_thread_local_context();
	delete global_context;
	global_context = NULL;
}


global_context_t::global_context_t()
	: next_thread_number(2)
{ }


thread_local_context_ptr
thread_local_context::make_shared_ptr() {
	// For some reason make_shared() crashes here when compiled with clang 3.2 on OS X.
	// Clang bug? We use 'new' to work around it.
	return thread_local_context_ptr(new thread_local_context());
}

thread_local_context::thread_local_context()
	: thread_number(0)
{
	thread = pthread_self();
	#ifdef __linux__
		tid = syscall(SYS_gettid);
	#endif
	syscall_interruption_lock.lock();
	#ifdef OXT_BACKTRACE_IS_ENABLED
		backtrace_list.reserve(50);
	#endif
}


string
thread::make_thread_name(const string &given_name) {
	if (given_name.empty()) {
		if (OXT_LIKELY(global_context != NULL)) {
			stringstream str;
			str << "Thread #";
			{
				boost::lock_guard<boost::mutex> l(global_context->thread_registration_mutex);
				str << global_context->next_thread_number;
			}
			return str.str();
		} else {
			return "(unknown)";
		}
	} else {
		return given_name;
	}
}

static void
set_native_thread_name(const string &name) {
	#if defined(__linux__)
		pthread_setname_np(pthread_self(), name.c_str());
	#elif defined(__APPLE__)
		pthread_setname_np(name.c_str());
	#elif defined(__FreeBSD__)
		pthread_set_name_np(pthread_self(), name.c_str());
	#endif
}

void
thread::thread_main(const boost::function<void ()> func, thread_local_context_ptr ctx) {
	set_thread_local_context(ctx);
	set_native_thread_name(ctx->thread_name);

	if (OXT_LIKELY(global_context != NULL)) {
		boost::lock_guard<boost::mutex> l(global_context->thread_registration_mutex);

		ctx->thread = pthread_self();
		global_context->next_thread_number++;
		global_context->registered_threads.push_back(ctx);
		ctx->iterator = global_context->registered_threads.end();
		ctx->iterator--;
		// Set this after setting 'iterator' to indicate
		// that push_back() has succeeded.
		ctx->thread_number = global_context->next_thread_number;
	}

	try {
		func();
	} catch (const thread_interrupted &) {
		// Do nothing.
	}
	// We don't care about other exceptions because they'll crash the process anyway.

	if (OXT_LIKELY(global_context != NULL)) {
		boost::lock_guard<boost::mutex> l(global_context->thread_registration_mutex);
		thread_local_context *ctx = get_thread_local_context();
		if (ctx != 0 && ctx->thread_number != 0) {
			global_context->registered_threads.erase(ctx->iterator);
			ctx->thread_number = 0;
		}
	}
	free_thread_local_context();
}

std::string
thread::name() const throw() {
	return context->thread_name;
}

std::string
thread::backtrace() const throw() {
	#ifdef OXT_BACKTRACE_IS_ENABLED
		spin_lock::scoped_lock l(context->backtrace_lock);
		return format_backtrace(context->backtrace_list);
	#else
		return "    (backtrace support disabled during compile time)";
	#endif
}

string
thread::all_backtraces() throw() {
	#ifdef OXT_BACKTRACE_IS_ENABLED
		if (OXT_LIKELY(global_context != NULL)) {
			boost::lock_guard<boost::mutex> l(global_context->thread_registration_mutex);
			list<thread_local_context_ptr>::const_iterator it;
			std::stringstream result;

			for (it = global_context->registered_threads.begin();
			     it != global_context->registered_threads.end();
			     it++)
			{
				thread_local_context_ptr ctx = *it;
				result << "Thread '" << ctx->thread_name <<
					"' (" << hex << showbase << ctx->thread << dec;
				#ifdef __linux__
					result << ", LWP " << ctx->tid;
				#endif
				result << "):" << endl;

				spin_lock::scoped_lock l(ctx->backtrace_lock);
				std::string bt = format_backtrace(ctx->backtrace_list);
				result << bt;
				if (bt.empty() || bt[bt.size() - 1] != '\n') {
					result << endl;
				}
				result << endl;
			}
			return result.str();
		} else {
			return "(OXT not initialized)";
		}
	#else
		return "(backtrace support disabled during compile time)";
	#endif
}

string
thread::current_backtrace() throw() {
	#ifdef OXT_BACKTRACE_IS_ENABLED
		thread_local_context *ctx = get_thread_local_context();
		if (OXT_LIKELY(ctx != NULL)) {
			spin_lock::scoped_lock l(ctx->backtrace_lock);
			return format_backtrace(ctx->backtrace_list);
		} else {
			return "(OXT not initialized)";
		}
	#else
		return "(backtrace support disabled during compile time)";
	#endif
}

void
thread::interrupt(bool interruptSyscalls) {
	int ret;

	boost::thread::interrupt();
	if (interruptSyscalls && context->syscall_interruption_lock.try_lock()) {
		do {
			ret = pthread_kill(native_handle(),
				INTERRUPTION_SIGNAL);
		} while (ret == EINTR);
		context->syscall_interruption_lock.unlock();
	}
}


} // namespace oxt
