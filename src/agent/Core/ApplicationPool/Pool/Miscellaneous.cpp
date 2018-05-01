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
#include <Core/ApplicationPool/Pool.h>

/*************************************************************************
 *
 * Miscellaneous functions for ApplicationPool2::Pool
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


// 'lockNow == false' may only be used during unit tests. Normally we
// should never call the callback while holding the lock.
void
Pool::asyncGet(const Options &options, const GetCallback &callback, bool lockNow) {
	DynamicScopedLock lock(syncher, lockNow);

	assert(lifeStatus == ALIVE || lifeStatus == PREPARED_FOR_SHUTDOWN);
	verifyInvariants();
	P_TRACE(2, "asyncGet(appGroupName=" << options.getAppGroupName() << ")");
	boost::container::vector<Callback> actions;

	Group *existingGroup = findMatchingGroup(options);

	if (OXT_LIKELY(existingGroup != NULL)) {
		/* Best case: the app group is already in the pool. Let's use it. */
		P_TRACE(2, "Found existing Group");
		existingGroup->verifyInvariants();
		SessionPtr session = existingGroup->get(options, callback, actions);
		existingGroup->verifyInvariants();
		verifyInvariants();
		P_TRACE(2, "asyncGet() finished");
		if (lockNow) {
			lock.unlock();
		}
		if (session != NULL) {
			callback(session, ExceptionPtr());
		}

	} else if (!atFullCapacityUnlocked()) {
		/* The app super group isn't in the pool and we have enough free
		 * resources to make a new one.
		 */
		P_DEBUG("Spawning new Group");
		GroupPtr group = createGroupAndAsyncGetFromIt(options,
			callback, actions);
		group->verifyInvariants();
		verifyInvariants();
		P_DEBUG("asyncGet() finished");

	} else {
		/* Uh oh, the app super group isn't in the pool but we don't
		 * have the resources to make a new one. The sysadmin should
		 * configure the system to let something like this happen
		 * as least as possible, but let's try to handle it as well
		 * as we can.
		 */
		ProcessPtr freedProcess = forceFreeCapacity(NULL, actions);
		if (freedProcess == NULL) {
			/* No process is eligible for killing. This could happen if, for example,
			 * all (super)groups are currently initializing/restarting/spawning/etc.
			 * We have no choice but to satisfy this get() action later when resources
			 * become available.
			 */
			P_DEBUG("Could not free a process; putting request to top-level getWaitlist");
			getWaitlist.push_back(GetWaiter(
				options.copyAndPersist(),
				callback));
		} else {
			/* Now that a process has been trashed we can create
			 * the missing Group.
			 */
			P_DEBUG("Creating new Group");
			GroupPtr group = createGroup(options);
			SessionPtr session = group->get(options, callback,
				actions);
			/* The Group is now spawning a process so the callback
			 * should now have been put on the wait list,
			 * unless something has changed and we forgot to update
			 * some code here or if options.noop...
			 */
			if (session != NULL) {
				assert(options.noop);
				actions.push_back(boost::bind(GetCallback::call,
					callback, session, ExceptionPtr()));
			}
			freedProcess->getGroup()->verifyInvariants();
			group->verifyInvariants();
		}

		assert(atFullCapacityUnlocked());
		verifyInvariants();
		verifyExpensiveInvariants();
		P_TRACE(2, "asyncGet() finished");
	}

	if (!actions.empty()) {
		if (lockNow) {
			if (lock.owns_lock()) {
				lock.unlock();
			}
			runAllActions(actions);
		} else {
			// This state is not allowed. If we reach
			// here then it probably indicates a bug in
			// the test suite.
			abort();
		}
	}
}

// TODO: 'ticket' should be a boost::shared_ptr for interruption-safety.
SessionPtr
Pool::get(const Options &options, Ticket *ticket) {
	ticket->session.reset();
	ticket->exception.reset();

	GetCallback callback;
	callback.func = syncGetCallback;
	callback.userData = ticket;
	asyncGet(options, callback);

	ScopedLock lock(ticket->syncher);
	while (ticket->session == NULL && ticket->exception == NULL) {
		ticket->cond.wait(lock);
	}
	lock.unlock();

	if (OXT_LIKELY(ticket->session != NULL)) {
		SessionPtr session = ticket->session;
		ticket->session.reset();
		return session;
	} else {
		rethrowException(ticket->exception);
		return SessionPtr(); // Shut up compiler warning.
	}
}

void
Pool::setMax(unsigned int max) {
	ScopedLock l(syncher);
	assert(max > 0);
	fullVerifyInvariants();
	bool bigger = max > this->max;
	this->max = max;
	if (bigger) {
		/* If there are clients waiting for resources
		 * to become free, spawn more processes now that
		 * we have the capacity.
		 *
		 * We favor waiters on the pool over waiters on the
		 * the groups because the latter already have the
		 * resources to eventually complete. Favoring waiters
		 * on the pool should be fairer.
		 */
		boost::container::vector<Callback> actions;
		assignSessionsToGetWaiters(actions);
		possiblySpawnMoreProcessesForExistingGroups();

		fullVerifyInvariants();
		l.unlock();
		runAllActions(actions);
	} else {
		fullVerifyInvariants();
	}
}

void
Pool::setMaxIdleTime(unsigned long long value) {
	LockGuard l(syncher);
	maxIdleTime = value;
	wakeupGarbageCollector();
}

void
Pool::enableSelfChecking(bool enabled) {
	LockGuard l(syncher);
	selfchecking = enabled;
}

/**
 * Checks whether at least one process is being spawned.
 */
bool
Pool::isSpawning(bool lock) const {
	DynamicScopedLock l(syncher, lock);
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (group->spawning()) {
			return true;
		}
		g_it.next();
	}
	return false;
}

bool
Pool::authorizeByApiKey(const ApiKey &key, bool lock) const {
	return key.isSuper() || findGroupByApiKey(key.toStaticString(), lock) != NULL;
}

bool
Pool::authorizeByUid(uid_t uid, bool lock) const {
	if (uid == 0 || uid == geteuid()) {
		return true;
	}

	DynamicScopedLock l(syncher, lock);
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (group->authorizeByUid(uid)) {
			return true;
		}
		g_it.next();
	}
	return false;
}


} // namespace ApplicationPool2
} // namespace Passenger
