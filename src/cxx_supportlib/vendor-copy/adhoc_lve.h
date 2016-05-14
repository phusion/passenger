/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _ADHOC_LVE_H_
#define _ADHOC_LVE_H_

// A library for using CloudLinux's LVE technology.
// https://www.cloudlinux.com/lve-manage.php
// http://docs.cloudlinux.com/understanding_lve.html

#include <functional>
#include <cstdlib>
#include <cstddef>
#include <sstream>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/once.hpp>
#include <boost/noncopyable.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>


namespace adhoc_lve {

struct liblve;

enum liblve_enter_flags {
	LVE_NO_UBC       = 1 << 0,
	LVE_NO_NAMESPACE = 1 << 1,
	LVE_NO_MAXENTER  = 1 << 2,
	LVE_SILENCE      = 1 << 3,
};

typedef void* (*liblve_alloc) (size_t size);
typedef void  (*liblve_free)  (void *ptr);

/**
 * initializes and create instance of LVE
 * args
 *   allocator - pointer to function to allocate memory
 * returns
 *    NULL on error, errno will be set.
 *    errno will be EINVAL if wrong version of library is used
 *    liblve otherwise
 */
typedef struct liblve* (*init_lve_function_ptr_t)(liblve_alloc alloc, liblve_free free);

/**
 * destroy lve library instance
 * args:
 *   lve = instantiated liblive instance
 * return 0 on success
 *        negative number on error. errno will be set
 */
typedef int (*destroy_lve_function_ptr_t)(struct liblve *lve);

/**
 * enter into virutal environment
 * args:
 * lve = fully initialized liblve instance
 * lve_id = id associated with LVE
 * cookie = pointer to cookie, which returned  if task correctly migrated
 * in LVE and used to exit from this LVE
 * return codes:
 * 0 = on success, negative number means error:
 * -EPERM - don't have permission to call, or called from outside root LVE
 * -ENOMEM - don't have memory to allocate new LVE
 * -EFAULT - cookie is bad pointer
 */
typedef int (*lve_enter_flags_function_ptr_t)
	(struct liblve *lve,
	 uint32_t lve_id,
	 uint32_t *cookie,
	 int liblve_enter_flags);

/**
 * exit from virtual environment, same as lve_leave
 * args:
 * lve = fully init liblve instance
 * cookie - pointer to cookie returned from lve_enter
 * return codes:
 * 0 = none error, all less zero is errors:
 * -ESRCH = task not in virutal environment
 * -EFAULT = bad cookie pointer
 * -EINVAL = cookie not match to stored in context
 */
typedef int (*lve_exit_function_ptr_t)(struct liblve *lve, uint32_t *cookie);

typedef int (*jail_function_ptr_t)(const struct passwd *, char*);

/**
 * Must be used once per app instances
 */
class LibLve : private boost::noncopyable {
public:

#ifdef LIBLVE_LOAD
#	error LIBLVE_LOAD macro redifinition
#else
#	define LIBLVE_LOAD(fname) \
		fname ## _function_ptr = \
				(fname ## _function_ptr_t) dlsym(liblve_handle.get(), #fname); \
		if (!fname ## _function_ptr && err_buf.str().empty()) \
			init_error = (std::string) "Failed to init LVE library " + ::dlerror()

	LibLve()
	  : init_lve_function_ptr        (NULL)
	  , destroy_lve_function_ptr     (NULL)
	  , lve_enter_flags_function_ptr (NULL)
	  , lve_exit_function_ptr        (NULL)
	{
		void *handle = ::dlopen("liblve.so.0", RTLD_LAZY);
		if (handle) {
			liblve_handle.reset(handle, ::dlclose);
		} else {
			// No liblve found, but it is OK and we are running
			// on non LVE capable system
			return;
		}

		std::ostringstream err_buf;

		LIBLVE_LOAD(init_lve);
		LIBLVE_LOAD(destroy_lve);
		LIBLVE_LOAD(lve_enter_flags);
		LIBLVE_LOAD(lve_exit);
		LIBLVE_LOAD(jail);

		if (!err_buf.str().empty())
		{
			init_error = err_buf.str();
			return;
		}

		struct liblve* init_handle = init_lve_function_ptr(std::malloc, std::free);
		if (!init_handle)
			err_buf << "init_lve error [" << errno << "]";
		else
			lve_init_handle.reset(init_handle, destroy_lve_function_ptr);

		init_error = err_buf.str();
	}

#undef LIBLVE_LOAD
#endif

	bool is_error() const { return !init_error.empty(); }
	const std::string& error() const { return init_error; }

	bool is_lve_available() const { return (bool) liblve_handle; }
	bool is_lve_ready() const { return is_lve_available() && !is_error(); }

	int jail(const struct passwd *pw, std::string& jail_err) const
	{
		char error_msg[8192];
		int rc = jail_function_ptr(pw, error_msg);
		if (rc < 0)
			jail_err.assign(error_msg);
		return rc;
	}

private:
	friend class LveEnter;

	/**
	 * using boost::shared_ptr because of scoped_ptr does not allow
	 * to specify custom destructor
	 */
	boost::shared_ptr<void>          liblve_handle;
	boost::shared_ptr<struct liblve> lve_init_handle;

	init_lve_function_ptr_t          init_lve_function_ptr;
	destroy_lve_function_ptr_t       destroy_lve_function_ptr;
	lve_enter_flags_function_ptr_t   lve_enter_flags_function_ptr;
	lve_exit_function_ptr_t          lve_exit_function_ptr;
	jail_function_ptr_t              jail_function_ptr;

	std::string                      init_error;
};

class LveInitSignleton {
public:
	static LibLve& getInstance(std::string* outInitOneTimeError) {
		if (!instance())
		{
			static boost::once_flag flag = BOOST_ONCE_INIT;
			boost::call_once(flag, initOnce, outInitOneTimeError);
		}

		return *instance();
	}
private:
	typedef boost::scoped_ptr<LibLve> lvelib_scoped_ptr;

	static lvelib_scoped_ptr& instance() {
		static lvelib_scoped_ptr lvelib_scoped_ptr;
		return lvelib_scoped_ptr;
	}

	static void initOnce(std::string *err_buf) {
		instance().reset(new LibLve);

		if (instance()->is_error() && err_buf)
			*err_buf = instance()->error();
	}
};

class LveEnter : private boost::noncopyable {
public:
	typedef void (*exit_callback_t)(bool entered, const std::string& exit_error);

	LveEnter(LibLve& lve, uint32_t uid, uint32_t cfg_min_uid, exit_callback_t cb)
		: ctx(lve)
		, cookie(0)
		, entered(false)
		, exit_callback(cb)
	{
		enter(uid, cfg_min_uid);
	}
	~LveEnter()
	{
		exit();
	}

	LveEnter& enter(uint32_t uid, uint32_t min_uid) {
		bool is_enter_lve_allowed = (min_uid <= uid);

		if (!is_enter_lve_allowed || !ctx.is_lve_ready() || entered)
			return *this;

		int err = ctx.lve_enter_flags_function_ptr(ctx.lve_init_handle.get(),
		                                           uid,
		                                           &cookie,
		                                           LVE_NO_MAXENTER|LVE_SILENCE);
		if (!err)
			entered = true;
		else
			enter_exit_error << "lve_enter_flags error [" << err << "]";

		return *this;
	}

	LveEnter& exit() {
		const bool memento (entered);

		if (entered)
		{
			int err = ctx.lve_exit_function_ptr(ctx.lve_init_handle.get(), &cookie);
			if (err)
				enter_exit_error << "lve_exit error [" << err << "]";

			entered = false;
		}

		if (exit_callback)
			exit_callback(memento, enter_exit_error.str());

		return *this;
	}

	bool is_entered() const { return entered; }
	bool is_error() const { return !enter_exit_error.str().empty(); }
	std::string error() const { return enter_exit_error.str(); }

	LibLve& lveInstance() { return ctx; }
	const LibLve& lveInstance() const { return ctx; }

private:
	LibLve &ctx;
	uint32_t cookie;
	bool entered;
	std::ostringstream enter_exit_error;
	exit_callback_t exit_callback;
};

} // adhoc_lve ns

#endif /* _ADHOC_LVE_H_ */
