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
 * Process data structure utility functions for ApplicationPool2::Pool
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


ProcessPtr
Pool::findOldestIdleProcess(const Group *exclude) const {
	ProcessPtr oldestIdleProcess;

	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (group.get() == exclude) {
			g_it.next();
			continue;
		}
		const ProcessList &processes = group->enabledProcesses;
		ProcessList::const_iterator p_it, p_end = processes.end();
		for (p_it = processes.begin(); p_it != p_end; p_it++) {
			const ProcessPtr process = *p_it;
			if (process->busyness() == 0
			     && (oldestIdleProcess == NULL
			         || process->lastUsed < oldestIdleProcess->lastUsed)
			) {
				oldestIdleProcess = process;
			}
		}
		g_it.next();
	}

	return oldestIdleProcess;
}

ProcessPtr
Pool::findBestProcessToTrash() const {
	ProcessPtr oldestProcess;

	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		const ProcessList &processes = group->enabledProcesses;
		ProcessList::const_iterator p_it, p_end = processes.end();
		for (p_it = processes.begin(); p_it != p_end; p_it++) {
			const ProcessPtr process = *p_it;
			if (oldestProcess == NULL
			 || process->lastUsed < oldestProcess->lastUsed) {
				oldestProcess = process;
			}
		}
		g_it.next();
	}

	return oldestProcess;
}

/**
 * Calls Group::detach() so be sure to fix up the invariants afterwards.
 * See the comments for Group::detach() and the code for detachProcessUnlocked().
 */
ProcessPtr
Pool::forceFreeCapacity(const Group *exclude,
	boost::container::vector<Callback> &postLockActions)
{
	ProcessPtr process = findOldestIdleProcess(exclude);
	if (process != NULL) {
		P_DEBUG("Forcefully detaching process " << process->inspect() <<
			" in order to free capacity in the pool");

		Group *group = process->getGroup();
		assert(group != NULL);
		assert(group->getWaitlist.empty());

		group->detach(process, postLockActions);
	}
	return process;
}

bool
Pool::detachProcessUnlocked(const ProcessPtr &process,
	boost::container::vector<Callback> &postLockActions)
{
	if (OXT_LIKELY(process->isAlive())) {
		verifyInvariants();

		Group *group = process->getGroup();
		group->detach(process, postLockActions);
		// 'process' may now be a stale pointer so don't use it anymore.
		assignSessionsToGetWaiters(postLockActions);
		possiblySpawnMoreProcessesForExistingGroups();

		group->verifyInvariants();
		verifyInvariants();
		verifyExpensiveInvariants();

		return true;
	} else {
		return false;
	}
}

void
Pool::syncDisableProcessCallback(const ProcessPtr &process, DisableResult result,
	boost::shared_ptr<DisableWaitTicket> ticket)
{
	LockGuard l(ticket->syncher);
	ticket->done = true;
	ticket->result = result;
	ticket->cond.notify_one();
}

void
Pool::possiblySpawnMoreProcessesForExistingGroups() {
	/* Looks for Groups that are waiting for capacity to become available,
	 * and spawn processes in those groups.
	 */
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (group->isWaitingForCapacity()) {
			P_DEBUG("Group " << group->getName() << " is waiting for capacity");
			group->spawn();
			if (atFullCapacityUnlocked()) {
				return;
			}
		}
		g_it.next();
	}
	/* Now look for Groups that haven't maximized their allowed capacity
	 * yet, and spawn processes in those groups.
	 */
	g_it = GroupMap::ConstIterator(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (group->shouldSpawn()) {
			P_DEBUG("Group " << group->getName() << " requests more processes to be spawned");
			group->spawn();
			if (atFullCapacityUnlocked()) {
				return;
			}
		}
		g_it.next();
	}
}


/****************************
 *
 * Public methods
 *
 ****************************/


vector<ProcessPtr>
Pool::getProcesses(bool lock) const {
	DynamicScopedLock l(syncher, lock);
	vector<ProcessPtr> result;
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		ProcessList::const_iterator p_it;

		for (p_it = group->enabledProcesses.begin(); p_it != group->enabledProcesses.end(); p_it++) {
			result.push_back(*p_it);
		}
		for (p_it = group->disablingProcesses.begin(); p_it != group->disablingProcesses.end(); p_it++) {
			result.push_back(*p_it);
		}
		for (p_it = group->disabledProcesses.begin(); p_it != group->disabledProcesses.end(); p_it++) {
			result.push_back(*p_it);
		}

		g_it.next();
	}
	return result;
}

ProcessPtr
Pool::findProcessByGupid(const StaticString &gupid, bool lock) const {
	vector<ProcessPtr> processes = getProcesses(lock);
	vector<ProcessPtr>::const_iterator it, end = processes.end();
	for (it = processes.begin(); it != end; it++) {
		const ProcessPtr &process = *it;
		if (process->getGupid() == gupid) {
			return process;
		}
	}
	return ProcessPtr();
}

ProcessPtr
Pool::findProcessByPid(pid_t pid, bool lock) const {
	vector<ProcessPtr> processes = getProcesses(lock);
	vector<ProcessPtr>::const_iterator it, end = processes.end();
	for (it = processes.begin(); it != end; it++) {
		const ProcessPtr &process = *it;
		if (process->getPid() == pid) {
			return process;
		}
	}
	return ProcessPtr();
}

bool
Pool::detachProcess(const ProcessPtr &process) {
	ScopedLock l(syncher);
	boost::container::vector<Callback> actions;
	bool result = detachProcessUnlocked(process, actions);
	fullVerifyInvariants();
	l.unlock();
	runAllActions(actions);
	return result;
}

bool
Pool::detachProcess(pid_t pid, const AuthenticationOptions &options) {
	ScopedLock l(syncher);
	ProcessPtr process = findProcessByPid(pid, false);
	if (process != NULL) {
		const Group *group = process->getGroup();
		if (group->authorizeByUid(options.uid)
		 || group->authorizeByApiKey(options.apiKey))
		{
			boost::container::vector<Callback> actions;
			bool result = detachProcessUnlocked(process, actions);
			fullVerifyInvariants();
			l.unlock();
			runAllActions(actions);
			return result;
		} else {
			throw SecurityException("Operation unauthorized");
		}
	} else {
		return false;
	}
}

bool
Pool::detachProcess(const string &gupid, const AuthenticationOptions &options) {
	ScopedLock l(syncher);
	ProcessPtr process = findProcessByGupid(gupid, false);
	if (process != NULL) {
		const Group *group = process->getGroup();
		if (group->authorizeByUid(options.uid)
		 || group->authorizeByApiKey(options.apiKey))
		{
			boost::container::vector<Callback> actions;
			bool result = detachProcessUnlocked(process, actions);
			fullVerifyInvariants();
			l.unlock();
			runAllActions(actions);
			return result;
		} else {
			throw SecurityException("Operation unauthorized");
		}
	} else {
		return false;
	}
}

DisableResult
Pool::disableProcess(const StaticString &gupid) {
	ScopedLock l(syncher);
	ProcessPtr process = findProcessByGupid(gupid, false);
	if (process != NULL) {
		Group *group = process->getGroup();
		// Must be a boost::shared_ptr to be interruption-safe.
		boost::shared_ptr<DisableWaitTicket> ticket = boost::make_shared<DisableWaitTicket>();
		DisableResult result = group->disable(process,
			boost::bind(syncDisableProcessCallback, _1, _2, ticket));
		group->verifyInvariants();
		group->verifyExpensiveInvariants();
		if (result == DR_DEFERRED) {
			l.unlock();
			ScopedLock l2(ticket->syncher);
			while (!ticket->done) {
				ticket->cond.wait(l2);
			}
			return ticket->result;
		} else {
			return result;
		}
	} else {
		return DR_NOOP;
	}
}


} // namespace ApplicationPool2
} // namespace Passenger
