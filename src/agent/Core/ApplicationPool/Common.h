/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL2_COMMON_H_
#define _PASSENGER_APPLICATION_POOL2_COMMON_H_

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/function.hpp>
#include <oxt/tracable_exception.hpp>
#include <ResourceLocator.h>
#include <RandomGenerator.h>
#include <StaticString.h>
#include <MemoryKit/palloc.h>
#include <DataStructures/StringKeyTable.h>
#include <Core/ApplicationPool/Options.h>
#include <Core/ApplicationPool/Context.h>
#include <Core/SpawningKit/Config.h>

namespace tut {
	struct ApplicationPool2_PoolTest;
}

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;

class Pool;
class Group;
class Process;
class Socket;
class AbstractSession;
class Session;

/**
 * The result of a Group::spawn() call.
 */
enum SpawnResult {
	// The spawn request has been honored. One or more processes are now being spawned.
	SR_OK,

	// A previous spawn request is still in progress, so this spawn request has been
	// ignored. Having said that, the desired result (increasing the number of processes
	// by one, within imposed constraints) will still be achieved.
	SR_IN_PROGRESS,

	// A non-rolling restart is currently in progress, so the spawn request cannot
	// be honored.
	SR_ERR_RESTARTING,

	// Unable to spawn a new process: the upper bound of the group process limits have
	// already been reached.
	// The group limit is checked before checking whether the pool is at full capacity,
	// so if you get this result then it is possible that the pool is also at full
	// capacity at the same time.
	SR_ERR_GROUP_UPPER_LIMITS_REACHED,

	// Unable to spawn a new process: the pool is at full capacity. Pool capacity is
	// checked after checking the group upper bound limits, so if you get this result
	// then it is guaranteed that the group upper bound limits have not been reached.
	SR_ERR_POOL_AT_FULL_CAPACITY
};

/**
 * The result of a Group::attach() call.
 */
enum AttachResult {
	// Attaching succeeded.
	AR_OK,

	// Attaching failed: the upper bound of the group process limits have
	// already been reached.
	// The group limit is checked before checking whether the pool is at full capacity,
	// so if you get this result then it is possible that the pool is also at full
	// capacity at the same time.
	AR_GROUP_UPPER_LIMITS_REACHED,

	// Attaching failed: the pool is at full capacity. Pool capacity is
	// checked after checking the group upper bound limits, so if you get this result
	// then it is guaranteed that the group upper bound limits have not been reached.
	AR_POOL_AT_FULL_CAPACITY,

	// Attaching failed: another group is waiting for capacity, while this group is
	// not waiting for capacity. You should throw away the current process and let the
	// other group spawn, e.g. by calling `pool->possiblySpawnMoreProcessesForExistingGroups()`.
	// This is checked after checking for the group upper bound limits and the pool
	// capacity, so if you get this result then there is guaranteed to be capacity
	// in the current group and in the pool.
	AR_ANOTHER_GROUP_IS_WAITING_FOR_CAPACITY
};

/**
 * The result of a Pool::disableProcess/Group::disable() call. Some values are only
 * returned by the functions, some values are only passed to the Group::disable()
 * callback, some values appear in all cases.
 */
enum DisableResult {
	// The process has been successfully disabled.
	// Returned by functions and passed to the callback.
	DR_SUCCESS,

	// The disabling of the process was canceled before completion.
	// The process still exists.
	// Only passed to the callback.
	DR_CANCELED,

	// Nothing happened: the requested process does not exist (anymore)
	// or was already disabled.
	// Returned by functions and passed to the callback.
	DR_NOOP,

	// The disabling of the process failed: an error occurred.
	// Returned by functions and passed to the callback.
	DR_ERROR,

	// Indicates that the process cannot be disabled immediately
	// and that the callback will be called later.
	// Only returned by functions.
	DR_DEFERRED
};

/**
 * Determines the behavior of Pool::restartGroupsByName() and Group::restart().
 * Specifically, determines whether to perform a rolling restart or not.
 */
enum RestartMethod {
	// Whether a rolling restart is performed, is determined by whether rolling restart
	// was enabled in the web server configuration (i.e. whether group->options.rollingRestart
	// is already true).
	RM_DEFAULT,
	// Perform a blocking restart. group->options.rollingRestart will not be changed.
	RM_BLOCKING,
	// Perform a rolling restart. group->options.rollingRestart will not be changed.
	RM_ROLLING
};

typedef boost::shared_ptr<Pool> PoolPtr;
typedef boost::shared_ptr<Group> GroupPtr;
typedef boost::intrusive_ptr<Process> ProcessPtr;
typedef boost::intrusive_ptr<AbstractSession> AbstractSessionPtr;
typedef boost::intrusive_ptr<Session> SessionPtr;
typedef boost::shared_ptr<tracable_exception> ExceptionPtr;
typedef StringKeyTable<GroupPtr> GroupMap;
typedef boost::function<void (const ProcessPtr &process, DisableResult result)> DisableCallback;
typedef boost::function<void ()> Callback;

struct GetCallback {
	void (*func)(const AbstractSessionPtr &session, const ExceptionPtr &e, void *userData);
	mutable void *userData;

	void operator()(const AbstractSessionPtr &session, const ExceptionPtr &e) const {
		func(session, e, userData);
	}

	static void call(GetCallback cb, const AbstractSessionPtr &session, const ExceptionPtr &e) {
		cb(session, e);
	}
};

struct GetWaiter {
	Options options;
	GetCallback callback;

	GetWaiter(const Options &o, const GetCallback &cb)
		: options(o),
		  callback(cb)
	{
		options.persist(o);
	}
};

struct Ticket {
	boost::mutex syncher;
	boost::condition_variable cond;
	SessionPtr session;
	ExceptionPtr exception;
};

ExceptionPtr copyException(const tracable_exception &e);
void rethrowException(const ExceptionPtr &e);
void processAndLogNewSpawnException(SpawningKit::SpawnException &e, const Options &options,
	const Context *context);
void recreateString(psg_pool_t *pool, StaticString &str);

} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_COMMON_H_ */
