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
#include <oxt/dynamic_thread_group.hpp>
#include <oxt/backtrace.hpp>
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
#include <Utils/Lock.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class Pool: public enable_shared_from_this<Pool> {
public:
	friend class tut::ApplicationPool2_PoolTest;
	friend class SuperGroup;
	friend class Group;
	
	mutable boost::mutex syncher;
	
	SpawnerFactoryPtr spawnerFactory;
	SafeLibev *libev;
	unsigned int max;
	unsigned long long maxIdleTime;
	
	RandomGeneratorPtr randomGenerator;
	ev::timer garbageCollectionTimer;
	
	/**
	 * Code can register background threads in one of these dynamic thread groups
	 * to ensure that threads are interrupted and/or joined properly upon Pool
	 * destruction.
	 * All threads in 'interruptableThreads' will be interrupted and joined upon
	 * Pool destruction.
	 * All threads in 'nonInterruptableThreads' will be joined, but not interrupted,
	 * upon Pool destruction.
	 */
	dynamic_thread_group interruptableThreads;
	dynamic_thread_group nonInterruptableThreads;
	
	SuperGroupMap superGroups;
	
	/**
	 * get() requests that...
	 * - cannot be immediately satisfied because the pool is at full
	 *   capacity and no existing processes can be killed,
	 * - and for which the super group isn't in the pool,
	 * ...are put on this wait list.
	 *
	 * This wait list is processed when one of the following things happen:
	 *
	 * - A process has been spawned but its associated group has
	 *   no get waiters. This process can be killed and the resulting
	 *   free capacity will be used to use spawn a process for this
	 *   get request.
	 * - A process (that has apparently been spawned after getWaitlist
	 *   was populated) is done processing a request. This process can
	 *   then be killed to free capacity.
	 * - A process has failed to spawn, resulting in capacity to
	 *   become free.
	 * - A SuperGroup failed to initialize, resulting in free capacity.
	 * - Someone commanded Pool to detach a process, resulting in free
	 *   capacity.
	 * - Someone commanded Pool to detach a SuperGroup, resulting in
	 *   free capacity.
	 * - The 'max' option has been increased, resulting in free capacity.
	 *
	 * Invariant 1:
	 *    for all options in getWaitlist:
	 *       options.getAppGroupName() is not in 'superGroups'.
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
		#ifndef NDEBUG
		vector<GetWaiter>::const_iterator it, end = getWaitlist.end();
		for (it = getWaitlist.begin(); it != end; it++) {
			const GetWaiter &waiter = *it;
			assert(superGroups.get(waiter.options.getAppGroupName()) == NULL);
		}
		#endif
	}
	
	ProcessPtr findOldestIdleProcess() const {
		ProcessPtr oldestIdleProcess;
		
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); it != end; it++) {
			const SuperGroupPtr &superGroup = it->second;
			const vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
				const ProcessList &processes = group->processes;
				ProcessList::const_iterator p_it, p_end = processes.end();
				for (p_it = processes.begin(); p_it != p_end; p_it++) {
					const ProcessPtr process = *p_it;
					if (process->usage() == 0
					     && (oldestIdleProcess == NULL
					         || process->lastUsed < oldestIdleProcess->lastUsed)
					) {
						oldestIdleProcess = process;
					}
				}
			}
		}
		
		return oldestIdleProcess;
	}
	
	ProcessPtr findBestProcessToTrash() const {
		ProcessPtr oldestProcess;
		
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); it != end; it++) {
			const SuperGroupPtr &superGroup = it->second;
			const vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
				const ProcessList &processes = group->processes;
				ProcessList::const_iterator p_it, p_end = processes.end();
				for (p_it = processes.begin(); p_it != p_end; p_it++) {
					const ProcessPtr process = *p_it;
					if (oldestProcess == NULL
					 || process->lastUsed < oldestProcess->lastUsed) {
						oldestProcess = process;
					}
				}
			}
		}
		
		return oldestProcess;
	}
	
	/** Process all waiters on the getWaitlist. Call when capacity has become free.
	 * This function assigns sessions to them by calling get() on the corresponding
	 * SuperGroups, or by creating more SuperGroups, in so far the new capacity allows.
	 */
	void assignSessionsToGetWaiters(vector<Callback> &postLockActions) {
		bool done = false;
		vector<GetWaiter>::iterator it, end = getWaitlist.end();
		vector<GetWaiter> newWaitlist;
		
		for (it = getWaitlist.begin(); it != end && !done; it++) {
			GetWaiter &waiter = *it;
			
			SuperGroup *superGroup = findMatchingSuperGroup(waiter.options);
			if (superGroup != NULL) {
				SessionPtr session = superGroup->get(waiter.options, waiter.callback);
				if (session != NULL) {
					postLockActions.push_back(boost::bind(
						waiter.callback, session, ExceptionPtr()));
				}
				/* else: the callback has now been put in
				 *       the group's get wait list.
				 */
			} else if (!atFullCapacity(false)) {
				createSuperGroupAndAsyncGetFromIt(waiter.options, waiter.callback);
			} else {
				/* Still cannot satisfy this get request. Keep it on the get
				 * wait list and try again later.
				 */
				newWaitlist.push_back(waiter);
			}
		}
		
		getWaitlist = newWaitlist;
	}
	
	void possiblySpawnMoreProcessesForExistingGroups() {
		SuperGroupMap::iterator it, end = superGroups.end();
		for (it = superGroups.begin(); it != end; it++) {
			SuperGroupPtr &superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				GroupPtr &group = *g_it;
				if (!group->getWaitlist.empty() && group->shouldSpawn()) {
					group->spawn();
				}
				group->verifyInvariants();
			}
		}
	}
	
	void migrateGroupGetWaitlistToPool(const GroupPtr &group) {
		getWaitlist.reserve(getWaitlist.size() + group->getWaitlist.size());
		while (!group->getWaitlist.empty()) {
			getWaitlist.push_back(group->getWaitlist.front());
			group->getWaitlist.pop();
		}
	}
	
	void migrateSuperGroupGetWaitlistToPool(const SuperGroupPtr &superGroup) {
		getWaitlist.reserve(getWaitlist.size() + superGroup->getWaitlist.size());
		while (!superGroup->getWaitlist.empty()) {
			getWaitlist.push_back(superGroup->getWaitlist.front());
			superGroup->getWaitlist.pop();
		}
	}
	
	/**
	 * Forcefully destroys and detaches the given SuperGroup. After detaching
	 * the SuperGroup may have a non-empty getWaitlist so be sure to do
	 * something with it.
	 */
	void forceDetachSuperGroup(const SuperGroupPtr &superGroup) {
		bool removed = superGroups.remove(superGroup->name);
		assert(removed);
		(void) removed; // Shut up compiler warning.
		superGroup->destroy(false);
		superGroup->setPool(PoolPtr());
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
	
	SuperGroup *findMatchingSuperGroup(const Options &options) {
		return superGroups.get(options.getAppGroupName()).get();
	}
	
	void garbageCollect(ev::timer &timer, int revents) {
		ScopedLock lock(syncher);
		SuperGroupMap::iterator it, end = superGroups.end();
		vector<SuperGroupPtr> superGroupsToDetach;
		unsigned long long now = SystemTime::getUsec();
		unsigned long long nextDeadline = 0;
		
		verifyInvariants();
		
		for (it = superGroups.begin(); it != end; it++) {
			SuperGroupPtr superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::iterator g_it, g_end = groups.end();
			unsigned long long earliestGroupDeadline = 0;
			
			superGroup->verifyInvariants();
			
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				GroupPtr group = *g_it;
				ProcessList &processes = group->processes;
				ProcessList::iterator p_it, p_end = processes.end();
				
				for (p_it = processes.begin(); p_it != p_end; p_it++) {
					ProcessPtr process = *p_it;
					
					if (process->sessions == 0
					 && process->lastUsed - now >= maxIdleTime) {
						ProcessList::iterator prev = p_it;
						prev--;
						group->detach(process);
						p_it = prev;
					} else {
						unsigned long long deadline =
							process->lastUsed + maxIdleTime;
						if (nextDeadline == 0
						 || deadline < nextDeadline) {
							nextDeadline = deadline;
						}
					}
				}
				
				group->verifyInvariants();
				
				unsigned long long deadline =
					group->spawner->lastUsed() + maxIdleTime;
				if (earliestGroupDeadline == 0
				 || deadline < earliestGroupDeadline) {
					earliestGroupDeadline = deadline;
				}
				// TODO: actually garbage collect the spawners
			}
			
			if (superGroup->garbageCollectable(now)) {
				superGroupsToDetach.push_back(superGroup);
			} else {
				if (nextDeadline == 0 || earliestGroupDeadline < nextDeadline) {
					nextDeadline = earliestGroupDeadline;
				}
			}
			
			superGroup->verifyInvariants();
		}
		
		vector<SuperGroupPtr>::const_iterator it2;
		vector<Callback> actions;
		for (it2 = superGroupsToDetach.begin(); it2 != superGroupsToDetach.end(); it2++) {
			detachSuperGroup(*it2, false, &actions);
		}
		
		verifyInvariants();
		lock.unlock();
		runAllActions(actions);
		
		if (nextDeadline == 0) {
			timer.start(maxIdleTime / 1000000.0, 0.0);
		} else {
			timer.start((nextDeadline - now + 1000000) / 1000000.0, 0.0);
		}
	}
	
	SuperGroupPtr createSuperGroupAndAsyncGetFromIt(const Options &options,
		const GetCallback &callback)
	{
		SuperGroupPtr superGroup = make_shared<SuperGroup>(shared_from_this(),
			options);
		superGroup->initialize();
		superGroups.set(options.getAppGroupName(), superGroup);
		SessionPtr session = superGroup->get(options, callback);
		/* Callback should now have been put on the wait list,
		 * unless something has changed and we forgot to update
		 * some code here...
		 */
		assert(session == NULL);
		return superGroup;
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
		TRACE_POINT();
		interruptableThreads.interrupt_and_join_all();
		nonInterruptableThreads.join_all();
		
		libev->stop(garbageCollectionTimer);
		
		UPDATE_TRACE_POINT();
		SuperGroupMap::iterator it;
		vector<SuperGroupPtr>::iterator it2;
		vector<SuperGroupPtr> superGroupsToDetach;
		vector<Callback> actions;
		for (it = superGroups.begin(); it != superGroups.end(); it++) {
			superGroupsToDetach.push_back(it->second);
		}
		for (it2 = superGroupsToDetach.begin(); it2 != superGroupsToDetach.end(); it2++) {
			detachSuperGroup(*it2, false, &actions);
		}
		
		verifyInvariants();
		verifyExpensiveInvariants();
	}
	
	// 'lockNow == false' may only be used during unit tests. Normally we
	// should never call the callback while holding the lock.
	void asyncGet(const Options &options, const GetCallback &callback, bool lockNow = true) {
		DynamicScopedLock lock(syncher, lockNow);
		
		verifyInvariants();
		
		SuperGroup *existingSuperGroup = findMatchingSuperGroup(options);
		if (OXT_LIKELY(existingSuperGroup != NULL)) {
			/* Best case: the app super group is already in the pool. Let's use it. */
			existingSuperGroup->verifyInvariants();
			SessionPtr session = existingSuperGroup->get(options, callback);
			existingSuperGroup->verifyInvariants();
			verifyInvariants();
			if (lockNow) {
				lock.unlock();
			}
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
			 *
			 * First, try to trash an idle process that's the oldest.
			 */
			ProcessPtr process = findOldestIdleProcess();
			if (process == NULL) {
				/* All processes are doing something. We have no choice
				 * but to trash a non-idle process.
				 */
				process = findBestProcessToTrash();
			} else {
				// Check invariant.
				assert(process->getGroup()->getWaitlist.empty());
			}
			if (process == NULL) {
				/* All (super)groups are currently initializing/restarting/spawning/etc
				 * so nothing can be killed. We have no choice but to satisfy this
				 * get() action later when resources become available.
				 */
				getWaitlist.push_back(GetWaiter(options, callback));
			} else {
				GroupPtr group;
				SuperGroupPtr superGroup;
				
				group = process->getGroup();
				assert(group != NULL);
				superGroup = group->getSuperGroup();
				assert(superGroup != NULL);
				
				group->detach(process);
				if (superGroup->garbageCollectable()) {
					assert(group->garbageCollectable());
					forceDetachSuperGroup(superGroup);
					assert(superGroup->getWaitlist.empty());
				} else if (group->processes.empty()
				        && !group->spawning()
				        && !group->getWaitlist.empty())
				{
					/* This group no longer has any processes - either
					 * spawning or alive - to satisfy its get waiters.
					 * We migrate the group's get wait list to the pool's
					 * get wait list. The group's original get waiters
					 * will get their chances later.
					 */
					migrateGroupGetWaitlistToPool(group);
				}
				group->verifyInvariants();
				superGroup->verifyInvariants();
				
				/* Now that a process has been trashed we can create
				 * the missing SuperGroup.
				 */
				superGroup = make_shared<SuperGroup>(shared_from_this(), options);
				superGroup->initialize();
				superGroups.set(options.getAppGroupName(), superGroup);
				SessionPtr session = superGroup->get(options, callback);
				/* The SuperGroup is still initializing so the callback
				 * should now have been put on the wait list,
				 * unless something has changed and we forgot to update
				 * some code here...
				 */
				assert(session == NULL);
				superGroup->verifyInvariants();
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
			possiblySpawnMoreProcessesForExistingGroups();
			
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
		SuperGroupMap::const_iterator it, end = superGroups.end();
		int result = 0;
		for (it = superGroups.begin(); it != end; it++) {
			const SuperGroupPtr &superGroup = it->second;
			result += superGroup->usage();
		}
		return result;
	}
	
	bool atFullCapacity(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		return usage(false) >= max;
	}
	
	unsigned int getProcessCount(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		unsigned int result = 0;
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); OXT_LIKELY(it != end); it++) {
			const SuperGroupPtr &superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
				result += group->count;
			}
		}
		return result;
	}
	
	SuperGroupPtr findSuperGroupBySecret(const string &secret, bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); OXT_LIKELY(it != end); it++) {
			const SuperGroupPtr &superGroup = it->second;
			if (superGroup->secret == secret) {
				return superGroup;
			}
		}
		return SuperGroupPtr();
	}
	
	ProcessPtr findProcessByGupid(const string &gupid, bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); OXT_LIKELY(it != end); it++) {
			const SuperGroupPtr &superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
				const ProcessList &processes = group->processes;
				ProcessList::const_iterator p_it, p_end = processes.end();
				for (p_it = processes.begin(); p_it != p_end; p_it++) {
					const ProcessPtr &process = *p_it;
					if (process->gupid == gupid) {
						return process;
					}
				}
			}
		}
		return ProcessPtr();
	}
	
	bool detachSuperGroup(const SuperGroupPtr &superGroup, bool lock = true,
		vector<Callback> *postLockActions = NULL)
	{
		assert(lock || postLockActions != NULL);
		DynamicScopedLock l(syncher, lock);
		
		if (OXT_LIKELY(superGroup->getPool().get() == this)) {
			if (OXT_LIKELY(superGroups.get(superGroup->name) != NULL)) {
				verifyInvariants();
				verifyExpensiveInvariants();
				
				forceDetachSuperGroup(superGroup);
				/* If this SuperGroup had get waiters, either
				 * on itself or in one of its groups, then we must
				 * reprocess them immediately. Detaching such a
				 * SuperGroup is essentially the same as restarting it.
				 */
				migrateSuperGroupGetWaitlistToPool(superGroup);
				
				vector<Callback> actions;
				assignSessionsToGetWaiters(actions);
				possiblySpawnMoreProcessesForExistingGroups();
				
				verifyInvariants();
				verifyExpensiveInvariants();
				
				if (lock) {
					l.unlock();
					runAllActions(actions);
				} else {
					*postLockActions = actions;
				}
				
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
			
			SuperGroupPtr superGroup = group->getSuperGroup();
			assert(superGroup->state != SuperGroup::INITIALIZING);
			assert(superGroup->getWaitlist.empty());
			
			group->detach(process);
			if (group->processes.empty()
			 && !group->spawning()
			 && !group->getWaitlist.empty()) {
				migrateGroupGetWaitlistToPool(group);
			}
			group->verifyInvariants();
			superGroup->verifyInvariants();
			
			vector<Callback> actions, actions2;
			assignSessionsToGetWaiters(actions);
			
			if (superGroup->garbageCollectable()) {
				// possiblySpawnMoreProcessesForExistingGroups()
				// already called via detachSuperGroup().
				detachSuperGroup(superGroup, false, &actions2);
			} else {
				possiblySpawnMoreProcessesForExistingGroups();
			}
			
			verifyInvariants();
			verifyExpensiveInvariants();
			
			l.unlock();
			runAllActions(actions);
			runAllActions(actions2);
			
			return true;
		} else {
			return false;
		}
	}
	
	bool detachSuperGroup(const string &superGroupSecret) {
		LockGuard l(syncher);
		SuperGroupPtr superGroup = findSuperGroupBySecret(superGroupSecret, false);
		if (superGroup != NULL) {
			return detachSuperGroup(superGroup, false);
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
