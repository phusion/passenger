#ifndef _PASSENGER_APPLICATION_POOL2_POOL_H_
#define _PASSENGER_APPLICATION_POOL2_POOL_H_

#include <string>
#include <vector>
#include <utility>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <cassert>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Group.h>
#include <ApplicationPool2/SuperGroup.h>
#include <ApplicationPool2/Session.h>
#include <ApplicationPool2/Spawner.h>
#include <ApplicationPool2/Options.h>
#include <SafeLibev.h>
#include <Exceptions.h>
#include <RandomGenerator.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


class Pool: public enable_shared_from_this<Pool> {
private:
	friend class SuperGroup;
	friend class Group;
	
	struct GetWaiter {
		Options options;
		GetCallback callback;
		
		GetWaiter(const Options &o, const GetCallback &cb)
			: options(o),
			  callback(cb)
		{
			options.persist();
		}
	};
	
	mutable boost::mutex syncher;
	
	SpawnerFactoryPtr spawnerFactory;
	SafeLibev *libev;
	unsigned int max;
	unsigned long long maxIdleTime;
	
	RandomGeneratorPtr randomGenerator;
	ev::timer garbageCollectionTimer;
	SuperGroupMap superGroups;
	/**
	 * get() requests that...
	 * - cannot be immediately satisfied because the pool is at full
	 *   capacity and existing no processes can be killed,
	 * - and for which the group isn't in the pool,
	 * ...are put on this wait list. This wait list is processed when one
	 * of the following things happen:
	 *
	 * - A process has been spawned but has no get waiters.
	 *   This process can be killed and the resulting free capacity
	 *   will be used to use spawn a process for this get request.
	 * - A process (that has apparently been spawned in the mean time) is
	 *   done processing a request. This process can then be killed
	 *   to free capacity.
	 * - A process has failed to spawn, resulting in capacity to
	 *   become free.
	 *
	 * Invariant 1:
	 *    for all options in getWaitlist:
	 *       options.getAppGroupName() is not in 'groups'.
	 *
	 * Invariant 2:
	 *    if getWaitlist is non-empty:
	 *       atFullCapacity()
	 * Equivalently:
	 *    if !atFullCapacity():
	 *       getWaitlist is empty.
	 */
	vector<GetWaiter> getWaitlist;
	
	void verifyInvariants() const {
		// !a || b: logical equivalent of a IMPLIES b.
		assert(!( !getWaitlist.empty() ) || ( atFullCapacity(false) ));
		assert(!( !atFullCapacity(false) ) || ( getWaitlist.empty() ));
	}
	
	void verifyExpensiveInvariants() const {
		vector<GetWaiter>::const_iterator it, end = getWaitlist.end();
		for (it = getWaitlist.begin(); it != end; it++) {
			const GetWaiter &waiter = *it;
			assert(groups.find(waiter.options.getAppGroupName()) == groups.end());
		}
	}
	
	ProcessPtr findMostIdleProcess() const {
		// TODO
		return ProcessPtr();
	}
	
	ProcessPtr findBestProcessToTrash() const {
		ProcessPtr oldestProcess;
		
		GroupMap::const_iterator it, end = groups.end();
		for (it = groups.begin(); it != end; it++) {
			const GroupPtr group = it->second;
			const ProcessList &processes = group->processes;
			ProcessList::const_iterator it2, end2 = processes.end();
			for (it2 = processes.begin(); it2 != end2; it2++) {
				const ProcessPtr process = *it2;
				if (oldestProcess == NULL || process->lastUsed < oldestProcess->lastUsed) {
					oldestProcess = process;
				}
			}
		}
		
		return oldestProcess;
	}
	
	void assignSessionsToGetWaiters(vector<Callback> &postLockActions) {
		bool done = false;
		vector<GetWaiter>::iterator it, end = getWaitlist.end();
		vector<GetWaiter> newWaitlist;
		
		for (it = getWaitlist.begin(); it != end && !done; it++) {
			GetWaiter &waiter = *it;
			
			Group *group = findMatchingGroup(waiter.options);
			if (group != NULL) {
				SessionPtr session = group->get(waiter.options, waiter.callback);
				if (session != NULL) {
					postLockActions.push_back(boost::bind(
						waiter.callback, session, ExceptionPtr()));
				}
				/* else: the callback has now been put in
				 *       the group's get wait list.
				 */
			} else if (!atFullCapacity(false)) {
				createGroupAndAsyncGetFromIt(waiter.options, waiter.callback);
			} else {
				/* Still cannot satisfy this get request. Keep it on the get
				 * wait list and try again later.
				 */
				newWaitlist.push_back(waiter);
			}
		}
		
		getWaitlist = newWaitlist;
	}
	
	void migrateGroupGetWaitlistToPool(const GroupPtr &group) {
		getWaitlist.reserve(getWaitlist.size() + group->getWaitlist.size());
		while (!group->getWaitlist.empty()) {
			getWaitlist.push_back(GetWaiter(group->options,
				group->getWaitlist.front()));
			group->getWaitlist.pop();
		}
	}
	
	void forceDetachGroup(const GroupPtr &group) {
		assert(groups.erase(group->name) == 1);
		group->detachAll();
		group->setPool(PoolPtr());
	}
	
	void runAllActions(const vector<Callback> &actions) {
		vector<Callback>::const_iterator it, end = actions.end();
		for (it = actions.begin(); it != end; it++) {
			(*it)();
		}
	}
	
	static void syncGetCallback(Ticket *ticket, const SessionPtr &session, const ExceptionPtr &e) {
		ScopedLock lock(ticket->syncher);
		if (OXT_LIKELY(session != NULL)) {
			ticket->session = session;
		} else {
			ticket->exception = e;
		}
		ticket->cond.notify_one();
	}
	
	Group *findMatchingGroup(const Options &options) {
		GroupMap::iterator it = groups.find(options.getAppGroupName());
		if (OXT_LIKELY(it != groups.end())) {
			return it->second.get();
		} else {
			return NULL;
		}
	}
	
	void garbageCollect(ev::timer &timer, int revents) {
		ScopedLock lock(syncher);
		GroupMap::iterator group_it, group_end = groups.end();
		vector<GroupPtr> groupsToDetach;
		unsigned long long now = SystemTime::getUsec();
		unsigned long long nextDeadline = 0;
		
		verifyInvariants();
		
		for (group_it = groups.begin(); group_it != group_end; group_it++) {
			GroupPtr group = group_it->second;
			ProcessList &processes = group->processes;
			ProcessList::iterator process_it, process_end = processes.end();
			
			group->verifyInvariants();
			
			for (process_it = processes.begin(); process_it != process_end; process_it++) {
				ProcessPtr process = *process_it;
				
				if (process->sessions == 0 && process->lastUsed - now >= maxIdleTime) {
					ProcessList::iterator prev = process_it;
					prev--;
					group->processes.erase(process_it);
					process_it = prev;
				} else {
					unsigned long long deadline = process->lastUsed + maxIdleTime;
					if (nextDeadline == 0 || deadline < nextDeadline) {
						nextDeadline = deadline;
					}
				}
			}
			
			if (group->garbageCollectable(now)) {
				groupsToDetach.push_back(group);
			} else {
				unsigned long long deadline = group->spawner->lastUsed() + maxIdleTime;
				if (nextDeadline == 0 || deadline < nextDeadline) {
					nextDeadline = deadline;
				}
			}
			
			group->verifyInvariants();
		}
		
		vector<GroupPtr>::const_iterator it;
		for (it = groupsToDetach.begin(); it != groupsToDetach.end(); it++) {
			detachGroup(*it, false);
		}
		
		verifyInvariants();
		lock.unlock();
		
		timer.start((nextDeadline - now + 1000000) / 1000000.0, 0.0);
	}
	
	GroupPtr createGroupAndAsyncGetFromIt(const Options &options, const GetCallback &callback) {
		GroupPtr group = make_shared<Group>(shared_from_this(), options);
		groups.insert(make_pair(options.getAppGroupName(), group));
		SessionPtr session = group->get(options, callback);
		/* Callback should now have been put on the wait list,
		 * unless something has changed and we forgot to update
		 * some code here...
		 */
		assert(session == NULL);
		return group;
	}
	
public:
	Pool(SafeLibev *libev, const SpawnerFactoryPtr &spawnerFactory,
		const RandomGeneratorPtr &randomGenerator = RandomGeneratorPtr())
	{
		this->libev = libev;
		this->spawnerFactory = spawnerFactory;
		if (randomGenerator != NULL) {
			this->randomGenerator = randomGenerator;
		} else {
			this->randomGenerator = make_shared<RandomGenerator>();
		}
		
		max         = 6;
		maxIdleTime = 30 * 1000000;
		
		garbageCollectionTimer.set<Pool, &Pool::garbageCollect>(this);
		garbageCollectionTimer.set(maxIdleTime / 1000000.0, maxIdleTime / 1000000.0);
		libev->start(garbageCollectionTimer);
	}
	
	~Pool() {
		libev->stop(garbageCollectionTimer);
		
		GroupMap::iterator it;
		vector<GroupPtr>::iterator it2;
		vector<GroupPtr> detachGroups;
		for (it = groups.begin(); it != groups.end(); it++) {
			detachGroups.push_back(it->second);
		}
		for (it2 = detachGroups.begin(); it2 != detachGroups.end(); it2++) {
			detachGroup(*it2, false);
		}
		
		verifyInvariants();
		verifyExpensiveInvariants();
	}
	
	void asyncGet(const Options &options, const GetCallback &callback) {
		ScopedLock lock(syncher);
		
		verifyInvariants();
		
		SuperGroup *existingSuperGroup = findMatchingSuperGroup(options);
		if (OXT_LIKELY(existingSuperGroup != NULL)) {
			/* Best case: the app super group is already in the pool. Let's use it. */
			existingSuperGroup->verifyInvariants();
			SessionPtr session = existingSuperGroup->get(options, callback);
			existingSuperGroup->verifyInvariants();
			verifyInvariants();
			lock.unlock();
			if (session != NULL) {
				callback(session, ExceptionPtr());
			}
		
		} else if (!atFullCapacity(false)) {
			/* The app super group isn't in the pool and we have enough free
			 * resources to make a new one.
			 */
			SuperGroupPtr superGroup = createSuperGroupAndAsyncGetFromIt(options, callback);
			superGroup->verifyInvariants();
			verifyInvariants();
			
		} else {
			/* Uh oh, the app super group isn't in the pool but we don't
			 * have the resources to make a new one. The sysadmin should
			 * configure the system to let something like this happen
			 * as least as possible, but let's try to handle it as well
			 * as we can.
			 */
			ProcessPtr process = findMostIdleProcess();
			if (process == NULL) {
				/* All processes are doing something. We have no choice
				 * but to trash a process.
				 */
				process = findBestProcessToTrash();
			} else {
				// Check invariant.
				assert(process->getGroup()->getWaitlist.empty());
			}
			if (process == NULL) {
				/* All groups are currently spawning so nothing can be killed.
				 * Have it done later.
				 */
				getWaitlist.push_back(GetWaiter(options, callback));
			} else {
				GroupPtr group;
				
				group = process->getGroup();
				assert(group != NULL);
				group->detach(process);
				
				if (group->garbageCollectable()) {
					forceDetachGroup(group);
				} else if (group->processes.empty()
				        && !group->spawning()
				        && !group->getWaitlist.empty())
				{
					/* This group no longer has any processes - either
					 * spawning or alive - to satisfy its get waiters.
					 * We migrate the group's get wait list to the pool's
					 * get wait list and detach the group. The group's original
					 * get waiters will get their chances later.
					 */
					migrateGroupGetWaitlistToPool(group);
					forceDetachGroup(group);
				}
				
				group = make_shared<Group>(shared_from_this(), options);
				groups.insert(make_pair(options.getAppGroupName(), group));
				SessionPtr session = group->get(options, callback);
				/* Callback should now have been put on the wait list,
				 * unless something has changed and we forgot to update
				 * some code here...
				 */
				assert(session == NULL);
			}
			
			assert(atFullCapacity(false));
			verifyInvariants();
			verifyExpensiveInvariants();
		}
	}
	
	SessionPtr get(const Options &options, Ticket *ticket) {
		ticket->session.reset();
		ticket->exception.reset();
		
		asyncGet(options, boost::bind(syncGetCallback, ticket, _1, _2));
		
		ScopedLock lock(ticket->syncher);
		while (ticket->session == NULL && ticket->exception == NULL) {
			ticket->cond.wait(lock);
		}
		lock.unlock();
		
		if (OXT_LIKELY(ticket->session != NULL)) {
			return ticket->session;
		} else {
			rethrowException(ticket->exception);
			return SessionPtr(); // Shut up compiler warning.
		}
	}
	
	void setMax(unsigned int max) {
		ScopedLock l(syncher);
		assert(max > 0);
		verifyInvariants();
		verifyExpensiveInvariants();
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
			vector<Callback> actions;
			assignSessionsToGetWaiters(actions);
			
			GroupMap::iterator it, end = groups.end();
			for (it = groups.begin(); it != end; it++) {
				GroupPtr group = it->second;
				if (!group->getWaitlist.empty() && group->shouldSpawn()) {
					group->spawn();
				}
				group->verifyInvariants();
			}
			
			verifyInvariants();
			verifyExpensiveInvariants();
			l.unlock();
			runAllActions(actions);
		} else {
			verifyInvariants();
			verifyExpensiveInvariants();
		}
	}
	
	unsigned int usage(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		GroupMap::const_iterator it, end = groups.end();
		int result = 0;
		for (it = groups.begin(); it != end; it++) {
			Group *group = it->second.get();
			result += group->usage();
		}
		return result;
	}
	
	bool atFullCapacity(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		return usage(false) < max;
	}
	
	GroupPtr findGroupBySecret(const string &secret, bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		GroupMap::const_iterator it, end = groups.end();
		for (it = groups.begin(); OXT_LIKELY(it != end); it++) {
			const GroupPtr group = it->second;
			if (group->secret == secret) {
				return group;
			}
		}
		return GroupPtr();
	}
	
	ProcessPtr findProcessByGupid(const string &gupid, bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		GroupMap::const_iterator it, end = groups.end();
		for (it = groups.begin(); OXT_LIKELY(it != end); it++) {
			const GroupPtr group = it->second;
			const ProcessList &processes = group->processes;
			ProcessList::const_iterator it2, end2 = processes.end();
			for (it2 = processes.begin(); it2 != end2; it2++) {
				const ProcessPtr process = *it2;
				if (process->gupid == gupid) {
					return process;
				}
			}
		}
		return ProcessPtr();
	}
	
	bool detachGroup(const GroupPtr &group, bool lock = true) {
		DynamicScopedLock l(syncher, lock);
		if (OXT_LIKELY(group->getPool().get() == this)) {
			GroupMap::iterator it = groups.find(group->name);
			if (OXT_LIKELY(it != groups.end())) {
				verifyInvariants();
				verifyExpensiveInvariants();
				
				forceDetachGroup(group);
				if (!group->getWaitlist.empty()) {
					migrateGroupGetWaitlistToPool(group);
				}
				
				verifyInvariants();
				verifyExpensiveInvariants();
				return true;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}
	
	bool detachProcess(const ProcessPtr &process, bool lock = true) {
		DynamicScopedLock l(syncher, lock);
		GroupPtr group = process->getGroup();
		if (group != NULL && group->getPool().get() == this) {
			verifyInvariants();
			
			group->detach(process);
			if (group->processes.empty()
			 && !group->spawning()
			 && !group->getWaitlist.empty()) {
				migrateGroupGetWaitlistToPool(group);
			}
			group->verifyInvariants();
			if (group->garbageCollectable()) {
				detachGroup(group, false);
			}
			
			verifyInvariants();
			verifyExpensiveInvariants();
			return true;
		} else {
			return false;
		}
	}
	
	bool detachGroup(const string &groupSecret) {
		LockGuard l(syncher);
		GroupPtr group = findGroupBySecret(groupSecret, false);
		if (group != NULL) {
			return detachGroup(group, false);
		} else {
			return false;
		}
	}
	
	bool detachProcess(const string &gupid) {
		LockGuard l(syncher);
		ProcessPtr process = findProcessByGupid(gupid, false);
		if (process != NULL) {
			return detachProcess(process, false);
		} else {
			return false;
		}
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_POOL_H_ */
