#ifndef _ADHOC_LVE_H_
#define _ADHOC_LVE_H_

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>

#include <boost/shared_ptr.hpp>
#include <cstdlib>
#include <sstream>

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
 * enter into virtual environment
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

const int JAIL_ERRMSG_MAX = 8192;
typedef int (*jail_function_ptr_t)(struct passwd *, char*);

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

/**
 * Must be used once per app instances
 */
class LveInit
{
public:

#ifdef LIBLVE_LOAD
#	error Macro redifinition
#else
#	define LIBLVE_LOAD(fname) \
		fname ## _function_ptr = \
				(fname ## _function_ptr_t) dlsym(liblve_handle.get(), #fname); \
		if (!fname ## _function_ptr && !isError()) \
			init_error << "Failed to init LVE library " << dlerror();

	LveInit()
	  : init_lve_function_ptr        (NULL)
	  , destroy_lve_function_ptr     (NULL)
	  , lve_enter_flags_function_ptr (NULL)
	  , lve_exit_function_ptr        (NULL)
	  , jail_function_ptr            (NULL)
	  , lve_init_handle              (NULL)
	  , liblve_handle( ::dlopen("liblve.so.0", RTLD_LAZY), ::dlclose )
	{
		if (!liblve_handle)
			return;

		LIBLVE_LOAD(init_lve);
		LIBLVE_LOAD(destroy_lve);
		LIBLVE_LOAD(lve_enter_flags);
		LIBLVE_LOAD(lve_exit);
		LIBLVE_LOAD(jail);

		if (isError())
			return;

		lve_init_handle = init_lve_function_ptr(std::malloc, std::free);
		if (!lve_init_handle)
		{
			init_error << "init_lve error [" << errno << "]";
		}
	}

#undef LIBLVE_LOAD
#endif

	~LveInit()
	{
		if (lve_init_handle)
			destroy_lve_function_ptr(lve_init_handle);
	}

	bool isLveAvailable() const { return (bool) liblve_handle; }

	bool isError() const { return !init_error.str().empty(); }
	std::string errorString() const { return init_error.str(); }

	bool isLveReady() const { return isLveAvailable() && !isError(); }

	/**
	 * Enter CafeFS environment
	 *
	 * @param pw user info
	 * @param out_err will be assigned with error message, if any
	 * @return < 0 if error
	 */
	int jail(struct passwd *pw, std::string& out_err)
	{
		if (!isLveAvailable())
		{
			out_err = "LVE lib is not available";
			return -1;
		}

		char err_buf[JAIL_ERRMSG_MAX];
		int result = jail_function_ptr(pw, err_buf);
		if (result < 0)
			out_err.assign(err_buf);
		return result;
	}

protected:
	friend class LveEnter;

	init_lve_function_ptr_t        init_lve_function_ptr;
	destroy_lve_function_ptr_t     destroy_lve_function_ptr;
	lve_enter_flags_function_ptr_t lve_enter_flags_function_ptr;
	lve_exit_function_ptr_t        lve_exit_function_ptr;
	jail_function_ptr_t            jail_function_ptr;
	struct liblve*                 lve_init_handle;

private:
	boost::shared_ptr<void> liblve_handle;
	std::ostringstream init_error;
private: //non-copiable
	LveInit(const LveInit&);
	void operator=(const LveInit&);
};

class LveEnter
{
public:
	typedef void (*exit_callback_t)(bool entered, const std::string& exit_error);

	LveEnter(LveInit& lve, uint32_t uid, uint32_t cfgMinUid, exit_callback_t cb)
		: ctx(lve)
		, cookie(0)
		, entered(false)
		, exit_callback(cb)
	{
		enter(uid, cfgMinUid);
	}
	~LveEnter()
	{
		exit();
	}

	LveEnter& enter(uint32_t uid, uint32_t cfgMinUid)
	{
		bool is_enter_lve_allowed = (cfgMinUid <= uid);

		if (!is_enter_lve_allowed || !ctx.isLveReady() || entered)
			return *this;

		int err = ctx.lve_enter_flags_function_ptr(ctx.lve_init_handle,
		                                           uid,
		                                           &cookie,
		                                           LVE_NO_MAXENTER|LVE_SILENCE);
		if (!err)
			entered = true;
		else
			enter_exit_error << "lve_enter_flags error [" << err << "]";

		return *this;
	}

	LveEnter& exit()
	{
		const bool memento (entered);

		if (entered)
		{
			int err = ctx.lve_exit_function_ptr(ctx.lve_init_handle, &cookie);
			if (err)
				enter_exit_error << "lve_exit error [" << err << "]";

			entered = false;
		}

		if (exit_callback)
			exit_callback(memento, enter_exit_error.str());

		return *this;
	}

	bool isEntered() const { return entered; }
	bool isError() const { return !enter_exit_error.str().empty(); }
	std::string errorString() const { return enter_exit_error.str(); }

private:
	LveInit& ctx;
	uint32_t cookie;
	bool entered;
	std::ostringstream enter_exit_error;
	exit_callback_t exit_callback;
private: //non-copiable
	LveEnter(const LveInit&);
	void operator=(const LveEnter&);
};

} // adhoc_lve ns

extern adhoc_lve::LveInit global_lveInit;

#endif /* _ADHOC_LVE_H_ */
