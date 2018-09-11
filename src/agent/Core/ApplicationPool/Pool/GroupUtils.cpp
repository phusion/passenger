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
 * Group data structure utility functions for ApplicationPool2::Pool
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


/****************************
 *
 * Consider these to be private methods,
 * they are only marked public for unit testing!
 *
 ****************************/

const pair<uid_t, gid_t>
Pool::getGroupRunUidAndGids(const StaticString &appGroupName) {
	LockGuard l(syncher);
	GroupPtr *group;
	if (!groups.lookup(appGroupName.c_str(), &group)) {
		throw RuntimeException("Could not find group: " + appGroupName);
	} else {
		SpawningKit::UserSwitchingInfo info = SpawningKit::prepareUserSwitching((*group)->options,
			*context->getWrapperRegistry());
		return pair<uid_t, gid_t>(info.uid,info.gid);
	}
}

const GroupPtr
Pool::getGroup(const char *name) {
	GroupPtr *group;
	if (groups.lookup(name, &group)) {
		return *group;
	} else {
		return GroupPtr();
	}
}

Group *
Pool::findMatchingGroup(const Options &options) {
	GroupPtr *group;
	if (groups.lookup(options.getAppGroupName(), &group)) {
		return group->get();
	} else {
		return NULL;
	}
}

GroupPtr
Pool::createGroup(const Options &options) {
	GroupPtr group = boost::make_shared<Group>(this, options);
	group->initialize();
	groups.insert(options.getAppGroupName(), group);
	wakeupGarbageCollector();
	return group;
}

GroupPtr
Pool::createGroupAndAsyncGetFromIt(const Options &options,
	const GetCallback &callback, boost::container::vector<Callback> &postLockActions)
{
	GroupPtr group = createGroup(options);
	SessionPtr session = group->get(options, callback,
		postLockActions);
	/* If !options.noop, then the callback should now have been put on the
	 * wait list, unless something has changed and we forgot to update
	 * some code here...
	 */
	if (session != NULL) {
		assert(options.noop);
		postLockActions.push_back(boost::bind(GetCallback::call,
			callback, session, ExceptionPtr()));
	}
	return group;
}

/**
 * Forcefully destroys and detaches the given Group. After detaching
 * the Group may have a non-empty getWaitlist so be sure to do
 * something with it.
 *
 * Also, one of the post lock actions can potentially perform a long-running
 * operation, so running them in a thread is advised.
 */
void
Pool::forceDetachGroup(const GroupPtr &group,
	const Callback &callback,
	boost::container::vector<Callback> &postLockActions)
{
	assert(group->getWaitlist.empty());
	const GroupPtr p = group; // Prevent premature destruction.
	bool removed = groups.erase(group->getName());
	assert(removed);
	(void) removed; // Shut up compiler warning.
	group->shutdown(callback, postLockActions);
}

void
Pool::syncDetachGroupCallback(boost::shared_ptr<DetachGroupWaitTicket> ticket) {
	LockGuard l(ticket->syncher);
	ticket->done = true;
	ticket->cond.notify_one();
}

void
Pool::waitDetachGroupCallback(boost::shared_ptr<DetachGroupWaitTicket> ticket) {
	ScopedLock l(ticket->syncher);
	while (!ticket->done) {
		ticket->cond.wait(l);
	}
}


/****************************
 *
 * Public methods
 *
 ****************************/


GroupPtr
Pool::findOrCreateGroup(const Options &options) {
	Options options2 = options;
	options2.noop = true;

	Ticket ticket;
	{
		LockGuard l(syncher);
		GroupPtr *group;
		if (!groups.lookup(options.getAppGroupName(), &group)) {
			// Forcefully create Group, don't care whether resource limits
			// actually allow it.
			createGroup(options);
		}
	}
	return get(options2, &ticket)->getGroup()->shared_from_this();
}

GroupPtr
Pool::findGroupByApiKey(const StaticString &value, bool lock) const {
	DynamicScopedLock l(syncher, lock);
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (group->getApiKey() == value) {
			return group;
		}
		g_it.next();
	}
	return GroupPtr();
}

bool
Pool::detachGroupByName(const HashedStaticString &name) {
	TRACE_POINT();
	ScopedLock l(syncher);
	GroupPtr group = groups.lookupCopy(name);

	if (OXT_LIKELY(group != NULL)) {
		P_ASSERT_EQ(group->getName(), name);
		UPDATE_TRACE_POINT();
		verifyInvariants();
		verifyExpensiveInvariants();

		boost::container::vector<Callback> actions;
		boost::shared_ptr<DetachGroupWaitTicket> ticket =
			boost::make_shared<DetachGroupWaitTicket>();
		ExceptionPtr exception = copyException(
			GetAbortedException("The containing Group was detached."));

		assignExceptionToGetWaiters(group->getWaitlist,
			exception, actions);
		forceDetachGroup(group,
			boost::bind(syncDetachGroupCallback, ticket),
			actions);
		possiblySpawnMoreProcessesForExistingGroups();

		verifyInvariants();
		verifyExpensiveInvariants();

		l.unlock();
		UPDATE_TRACE_POINT();
		runAllActions(actions);
		actions.clear();

		UPDATE_TRACE_POINT();
		ScopedLock l2(ticket->syncher);
		while (!ticket->done) {
			ticket->cond.wait(l2);
		}
		return true;
	} else {
		return false;
	}
}

bool
Pool::detachGroupByApiKey(const StaticString &value) {
	ScopedLock l(syncher);
	GroupPtr group = findGroupByApiKey(value, false);
	if (group != NULL) {
		string name = group->getName();
		group.reset();
		l.unlock();
		return detachGroupByName(name);
	} else {
		return false;
	}
}

bool
Pool::restartGroupByName(const StaticString &name, const RestartOptions &options) {
	ScopedLock l(syncher);
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (name == group->getName()) {
			if (!group->authorizeByUid(options.uid)
			 && !group->authorizeByApiKey(options.apiKey))
			{
				throw SecurityException("Operation unauthorized");
			}
			if (!group->restarting()) {
				group->restart(group->options, options.method);
			}
			return true;
		}
		g_it.next();
	}

	return false;
}

unsigned int
Pool::restartGroupsByAppRoot(const StaticString &appRoot, const RestartOptions &options) {
	ScopedLock l(syncher);
	GroupMap::ConstIterator g_it(groups);
	unsigned int result = 0;

	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (appRoot == group->options.appRoot) {
			if (group->authorizeByUid(options.uid)
			 || group->authorizeByApiKey(options.apiKey))
			{
				result++;
				group->restart(group->options, options.method);
			}
		}
		g_it.next();
	}

	return result;
}


} // namespace ApplicationPool2
} // namespace Passenger
