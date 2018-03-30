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
 * General utility functions for ApplicationPool2::Pool
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


/****************************
 *
 * Private methods
 *
 ****************************/


const char *
Pool::maybeColorize(const InspectOptions &options, const char *color) {
	if (options.colorize) {
		return color;
	} else {
		return "";
	}
}

const char *
Pool::maybePluralize(unsigned int count, const char *singular, const char *plural) {
	if (count == 1) {
		return singular;
	} else {
		return plural;
	}
}

void
Pool::runAllActions(const boost::container::vector<Callback> &actions) {
	boost::container::vector<Callback>::const_iterator it, end = actions.end();
	for (it = actions.begin(); it != end; it++) {
		(*it)();
	}
}

void
Pool::runAllActionsWithCopy(boost::container::vector<Callback> actions) {
	runAllActions(actions);
}

bool
Pool::runHookScripts(const char *name,
	const boost::function<void (HookScriptOptions &)> &setup) const
{
	ScopedLock l(context->agentConfigSyncher);
	if (!context->agentConfig.isNull()) {
		string hookName = string("hook_") + name;
		string spec = context->agentConfig.get(hookName, Json::Value()).asString();
		if (!spec.empty()) {
			HookScriptOptions options;
			options.agentConfig = context->agentConfig;
			l.unlock();
			options.name = name;
			options.spec = spec;
			setup(options);
			return Passenger::runHookScripts(options);
		} else {
			return true;
		}
	} else {
		return true;
	}
}

void
Pool::verifyInvariants() const {
	// !a || b: logical equivalent of a IMPLIES b.
	#ifndef NDEBUG
	if (!selfchecking) {
		return;
	}
	assert(!( !getWaitlist.empty() ) || ( atFullCapacityUnlocked() ));
	assert(!( !atFullCapacityUnlocked() ) || ( getWaitlist.empty() ));
	#endif
}

void
Pool::verifyExpensiveInvariants() const {
	#ifndef NDEBUG
	if (!selfchecking) {
		return;
	}
	vector<GetWaiter>::const_iterator it, end = getWaitlist.end();
	for (it = getWaitlist.begin(); it != end; it++) {
		const GetWaiter &waiter = *it;
		const GroupPtr *group;
		assert(!groups.lookup(waiter.options.getAppGroupName(), &group));
	}
	#endif
}

void
Pool::fullVerifyInvariants() const {
	TRACE_POINT();
	verifyInvariants();
	UPDATE_TRACE_POINT();
	verifyExpensiveInvariants();
	UPDATE_TRACE_POINT();

	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		group->verifyInvariants();
		group->verifyExpensiveInvariants();
		g_it.next();
	}
}

/**
 * Process all waiters on the getWaitlist. Call when capacity has become free.
 * This function assigns sessions to them by calling get() on the corresponding
 * Groups, or by creating more Groups, in so far the new capacity allows.
 */
void
Pool::assignSessionsToGetWaiters(boost::container::vector<Callback> &postLockActions) {
	bool done = false;
	vector<GetWaiter>::iterator it, end = getWaitlist.end();
	vector<GetWaiter> newWaitlist;

	for (it = getWaitlist.begin(); it != end && !done; it++) {
		GetWaiter &waiter = *it;

		Group *group = findMatchingGroup(waiter.options);
		if (group != NULL) {
			SessionPtr session = group->get(waiter.options, waiter.callback,
				postLockActions);
			if (session != NULL) {
				postLockActions.push_back(boost::bind(GetCallback::call,
					waiter.callback, session, ExceptionPtr()));
			}
			/* else: the callback has now been put in
			 *       the group's get wait list.
			 */
		} else if (!atFullCapacityUnlocked()) {
			createGroupAndAsyncGetFromIt(waiter.options, waiter.callback,
				postLockActions);
		} else {
			/* Still cannot satisfy this get request. Keep it on the get
			 * wait list and try again later.
			 */
			newWaitlist.push_back(waiter);
		}
	}

	std::swap(getWaitlist, newWaitlist);
}

template<typename Queue>
void
Pool::assignExceptionToGetWaiters(Queue &getWaitlist,
	const ExceptionPtr &exception,
	boost::container::vector<Callback> &postLockActions)
{
	while (!getWaitlist.empty()) {
		postLockActions.push_back(boost::bind(GetCallback::call,
			getWaitlist.front().callback, SessionPtr(),
			exception));
		getWaitlist.pop_front();
	}
}

void
Pool::syncGetCallback(const AbstractSessionPtr &session, const ExceptionPtr &e,
	void *userData)
{
	Ticket *ticket = static_cast<Ticket *>(userData);
	ScopedLock lock(ticket->syncher);
	if (OXT_LIKELY(session != NULL)) {
		ticket->session = static_pointer_cast<Session>(session);
	} else {
		ticket->exception = e;
	}
	ticket->cond.notify_one();
}


/****************************
 *
 * Public methods
 *
 ****************************/


Context *
Pool::getContext() {
	return context;
}

SpawningKit::Context *
Pool::getSpawningKitContext() const {
	return context->getSpawningKitContext();
}

const RandomGeneratorPtr &
Pool::getRandomGenerator() const {
	return context->getRandomGenerator();
}


} // namespace ApplicationPool2
} // namespace Passenger
