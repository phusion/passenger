/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2011, 2012 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_APPLICATION_POOL2_POOL_H_
#define _PASSENGER_APPLICATION_POOL2_POOL_H_

#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <iomanip>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <oxt/dynamic_thread_group.hpp>
#include <oxt/backtrace.hpp>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Group.h>
#include <ApplicationPool2/SuperGroup.h>
#include <ApplicationPool2/Session.h>
#include <ApplicationPool2/Spawner.h>
#include <ApplicationPool2/Options.h>
#include <UnionStation.h>
#include <Logging.h>
#include <SafeLibev.h>
#include <Exceptions.h>
#include <RandomGenerator.h>
#include <Utils/Lock.h>
#include <Utils/SystemTime.h>
#include <Utils/ProcessMetricsCollector.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class Pool: public enable_shared_from_this<Pool> {
// Actually private, but marked public so that unit tests can access the fields.
public:
	friend class SuperGroup;
	friend class Group;
	typedef UnionStation::LoggerFactory LoggerFactory;
	typedef UnionStation::LoggerFactoryPtr LoggerFactoryPtr;
	typedef UnionStation::LoggerPtr LoggerPtr;
	
	SpawnerFactoryPtr spawnerFactory;
	LoggerFactoryPtr loggerFactory;
	RandomGeneratorPtr randomGenerator;

	mutable boost::mutex syncher;
	SafeLibev *libev;
	unsigned int max;
	unsigned long long maxIdleTime;
	
	ev::timer garbageCollectionTimer;
	ev::timer analyticsCollectionTimer;
	
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

	mutable boost::mutex debugSyncher;
	unsigned int spawnLoopIteration;
	
	static void runAllActions(const vector<Callback> &actions) {
		vector<Callback>::const_iterator it, end = actions.end();
		for (it = actions.begin(); it != end; it++) {
			(*it)();
		}
	}
	
	static void runAllActionsWithCopy(vector<Callback> actions) {
		runAllActions(actions);
	}
	
	void verifyInvariants() const {
		// !a || b: logical equivalent of a IMPLIES b.
		P_ASSERT(!( !getWaitlist.empty() ) || ( atFullCapacity(false) ));
		P_ASSERT(!( !atFullCapacity(false) ) || ( getWaitlist.empty() ));
	}
	
	void verifyExpensiveInvariants() const {
		#ifndef NDEBUG
		vector<GetWaiter>::const_iterator it, end = getWaitlist.end();
		for (it = getWaitlist.begin(); it != end; it++) {
			const GetWaiter &waiter = *it;
			P_ASSERT(superGroups.get(waiter.options.getAppGroupName()) == NULL);
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
	void forceDetachSuperGroup(const SuperGroupPtr &superGroup, vector<Callback> &postLockActions) {
		bool removed = superGroups.remove(superGroup->name);
		P_ASSERT(removed);
		(void) removed; // Shut up compiler warning.
		superGroup->destroy(postLockActions, false);
		superGroup->setPool(PoolPtr());
	}

	bool detachProcessUnlocked(const ProcessPtr &process, vector<Callback> &postLockActions) {
		GroupPtr group = process->getGroup();
		if (group != NULL && group->getPool().get() == this) {
			verifyInvariants();
			
			SuperGroupPtr superGroup = group->getSuperGroup();
			P_ASSERT(superGroup->state != SuperGroup::INITIALIZING);
			P_ASSERT(superGroup->getWaitlist.empty());
			
			group->detach(process, postLockActions);
			if (group->processes.empty()
			 && !group->spawning()
			 && !group->getWaitlist.empty()) {
				migrateGroupGetWaitlistToPool(group);
			}
			group->verifyInvariants();
			superGroup->verifyInvariants();
			
			assignSessionsToGetWaiters(postLockActions);
			
			if (superGroup->garbageCollectable()) {
				// possiblySpawnMoreProcessesForExistingGroups()
				// already called via detachSuperGroup().
				detachSuperGroup(superGroup, false, &postLockActions);
			} else {
				possiblySpawnMoreProcessesForExistingGroups();
			}
			
			verifyInvariants();
			verifyExpensiveInvariants();
			
			return true;
		} else {
			return false;
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
		PoolPtr self = shared_from_this(); // Keep pool object alive.
		TRACE_POINT();
		ScopedLock lock(syncher);
		SuperGroupMap::iterator it, end = superGroups.end();
		vector<SuperGroupPtr> superGroupsToDetach;
		vector<Callback> actions;
		unsigned long long now = SystemTime::getUsec();
		unsigned long long nextGcRunTime = 0;
		
		P_DEBUG("Garbage collection time");
		verifyInvariants();
		
		// For all supergroups and groups...
		for (it = superGroups.begin(); it != end; it++) {
			SuperGroupPtr superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::iterator g_it, g_end = groups.end();
			
			superGroup->verifyInvariants();
			
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				GroupPtr group = *g_it;
				ProcessList &processes = group->processes;
				ProcessList::iterator p_it, p_end = processes.end();
				
				for (p_it = processes.begin(); p_it != p_end; p_it++) {
					ProcessPtr process = *p_it;
					
					// ...detach processes that have been idle for more than maxIdleTime.
					unsigned long long processGcTime =
						process->lastUsed + maxIdleTime;
					if (process->sessions == 0
					 && now >= processGcTime
					 && (unsigned long) group->count > group->options.minProcesses) {
						ProcessList::iterator prev = p_it;
						prev--;
						P_DEBUG("Garbage collect idle process: " << process->inspect() <<
							", group=" << group->name);
						group->detach(process, actions);
						p_it = prev;
					} else if (nextGcRunTime == 0
					        || processGcTime < nextGcRunTime) {
						nextGcRunTime = processGcTime;
					}
				}
				
				group->verifyInvariants();
				
				// ...cleanup the spawner if it's been idle for more than preloaderIdleTime.
				if (group->spawner->cleanable()) {
					unsigned long long spawnerGcTime =
						group->spawner->lastUsed() +
						group->options.getMaxPreloaderIdleTime() * 1000000;
					if (now >= spawnerGcTime) {
						P_DEBUG("Garbage collect idle spawner: group=" << group->name);
						group->asyncCleanupSpawner();
					} else if (nextGcRunTime == 0
					        || spawnerGcTime < nextGcRunTime) {
						nextGcRunTime = spawnerGcTime;
					}
				}
			}
			
			// ...remove entire supergroup if it has become garbage
			// collectable after detaching idle processes.
			if (superGroup->garbageCollectable(now)) {
				superGroupsToDetach.push_back(superGroup);
			}
			
			superGroup->verifyInvariants();
		}
		
		vector<SuperGroupPtr>::const_iterator it2;
		for (it2 = superGroupsToDetach.begin(); it2 != superGroupsToDetach.end(); it2++) {
			P_DEBUG("Garbage collect SuperGroup: " << (*it2)->inspect());
			detachSuperGroup(*it2, false, &actions);
		}
		
		verifyInvariants();

		// Schedule next garbage collection run.
		ev_tstamp tstamp;
		if (nextGcRunTime == 0 || nextGcRunTime <= now) {
			tstamp = maxIdleTime / 1000000.0;
		} else {
			tstamp = (nextGcRunTime - now) / 1000000.0;
		}
		P_DEBUG("Garbage collection done; next garbage collect in " <<
			std::fixed << std::setprecision(3) << tstamp << " sec");

		lock.unlock();
		runAllActions(actions);
		timer.start(tstamp, 0.0);
	}

	struct ProcessAnalyticsLogEntry {
		string groupName;
		string key;
		stringstream data;
	};

	typedef shared_ptr<ProcessAnalyticsLogEntry> ProcessAnalyticsLogEntryPtr;

	void collectAnalytics(ev::timer &timer, int revents) {
		PoolPtr self = shared_from_this(); // Keep pool object alive.
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		vector<pid_t> pids;
		unsigned int max;
		
		// Collect all the PIDs.
		{
			UPDATE_TRACE_POINT();
			LockGuard l(syncher);
			max = this->max;
		}
		pids.reserve(max);
		{
			UPDATE_TRACE_POINT();
			LockGuard l(syncher);
			SuperGroupMap::const_iterator sg_it, sg_end = superGroups.end();
			
			for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
				const SuperGroupPtr &superGroup = sg_it->second;
				vector<GroupPtr>::const_iterator g_it, g_end = superGroup->groups.end();

				for (g_it = superGroup->groups.begin(); g_it != g_end; g_it++) {
					const GroupPtr &group = *g_it;
					ProcessList::const_iterator p_it, p_end = group->processes.end();

					for (p_it = group->processes.begin(); p_it != p_end; p_it++) {
						const ProcessPtr &process = *p_it;
						pids.push_back(process->pid);
					}

					p_end = group->disabledProcesses.end();
					for (p_it = group->disabledProcesses.begin(); p_it != p_end; p_it++) {
						const ProcessPtr &process = *p_it;
						pids.push_back(process->pid);
					}
				}
			}
		}
		
		ProcessMetricMap allMetrics;
		try {
			// Now collect the process metrics and store them in the
			// data structures, and log the state into the analytics logs.
			UPDATE_TRACE_POINT();
			allMetrics = ProcessMetricsCollector().collect(pids);
		} catch (const ProcessMetricsCollector::ParseException &) {
			P_WARN("Unable to collect process metrics: cannot parse 'ps' output.");
			goto end;
		}

		{
			UPDATE_TRACE_POINT();
			vector<ProcessAnalyticsLogEntryPtr> logEntries;
			ScopedLock l(syncher);
			SuperGroupMap::iterator sg_it, sg_end = superGroups.end();
			
			UPDATE_TRACE_POINT();
			for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
				const SuperGroupPtr &superGroup = sg_it->second;
				vector<GroupPtr>::iterator g_it, g_end = superGroup->groups.end();

				for (g_it = superGroup->groups.begin(); g_it != g_end; g_it++) {
					const GroupPtr &group = *g_it;
					ProcessList::iterator p_it, p_end = group->processes.end();

					for (p_it = group->processes.begin(); p_it != p_end; p_it++) {
						ProcessPtr &process = *p_it;
						ProcessMetricMap::const_iterator metrics_it =
							allMetrics.find(process->pid);
						if (metrics_it != allMetrics.end()) {
							process->metrics = metrics_it->second;
						}
					}

					p_end = group->disabledProcesses.end();
					for (p_it = group->disabledProcesses.begin(); p_it != p_end; p_it++) {
						ProcessPtr &process = *p_it;
						ProcessMetricMap::const_iterator metrics_it =
							allMetrics.find(process->pid);
						if (metrics_it != allMetrics.end()) {
							process->metrics = metrics_it->second;
						}
					}

					// Log to Union Station.
					if (group->options.analytics && loggerFactory != NULL) {
						ProcessAnalyticsLogEntryPtr entry = make_shared<ProcessAnalyticsLogEntry>();
						stringstream &xml = entry->data;

						entry->groupName = group->name;
						entry->key = group->options.unionStationKey;
						xml << "Group: <group>";
						group->inspectXml(xml, false);
						xml << "</group>";
					}
				}
			}

			l.unlock();
			while (!logEntries.empty()) {
				ProcessAnalyticsLogEntryPtr entry = logEntries.back();
				logEntries.pop_back();
				LoggerPtr logger = loggerFactory->newTransaction(entry->groupName,
					"processes", entry->key);
				logger->message(entry->data.str());
			}
		}
		
		end:
		// Sleep for about 4 seconds, aligned to seconds boundary
		// for saving power on laptops.
		ev_now_update(libev->getLoop());
		unsigned long long currentTime = SystemTime::getUsec();
		unsigned long long deadline =
			roundUp<unsigned long long>(currentTime, 1000000) + 4000000;
		timer.start((deadline - currentTime) / 1000000.0, 0.0);
	}
	
	SuperGroupPtr createSuperGroup(const Options &options) {
		SuperGroupPtr superGroup = make_shared<SuperGroup>(shared_from_this(),
			options);
		superGroup->initialize();
		superGroups.set(options.getAppGroupName(), superGroup);
		return superGroup;
	}
	
	SuperGroupPtr createSuperGroupAndAsyncGetFromIt(const Options &options,
		const GetCallback &callback)
	{
		SuperGroupPtr superGroup = createSuperGroup(options);
		SessionPtr session = superGroup->get(options, callback);
		/* Callback should now have been put on the wait list,
		 * unless something has changed and we forgot to update
		 * some code here...
		 */
		P_ASSERT(session == NULL);
		return superGroup;
	}

	// Debugging helper function, implemented in .cpp file so that GDB can access it.
	const SuperGroupPtr getSuperGroup(const char *name);
	
public:
	Pool(SafeLibev *libev, const SpawnerFactoryPtr &spawnerFactory,
		const LoggerFactoryPtr &loggerFactory = LoggerFactoryPtr(),
		const RandomGeneratorPtr &randomGenerator = RandomGeneratorPtr())
	{
		this->libev = libev;
		this->spawnerFactory = spawnerFactory;
		this->loggerFactory = loggerFactory;
		if (randomGenerator != NULL) {
			this->randomGenerator = randomGenerator;
		} else {
			this->randomGenerator = make_shared<RandomGenerator>();
		}
		
		max         = 6;
		maxIdleTime = 60 * 1000000;
		
		garbageCollectionTimer.set<Pool, &Pool::garbageCollect>(this);
		garbageCollectionTimer.set(maxIdleTime / 1000000.0, 0.0);
		libev->start(garbageCollectionTimer);
		analyticsCollectionTimer.set<Pool, &Pool::collectAnalytics>(this);
		analyticsCollectionTimer.set(3.0, 0.0);
		libev->start(analyticsCollectionTimer);

		spawnLoopIteration = 0;
	}
	
	~Pool() {
		TRACE_POINT();
		destroy();
	}

	void destroy() {
		TRACE_POINT();
		libev->stop(garbageCollectionTimer);
		libev->stop(analyticsCollectionTimer);

		UPDATE_TRACE_POINT();
		interruptableThreads.interrupt_and_join_all();
		nonInterruptableThreads.join_all();

		UPDATE_TRACE_POINT();
		ScopedLock l(syncher);
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
		l.unlock();
		runAllActions(actions);

		// detachSuperGroup() may launch additional threads, so wait for them.
		interruptableThreads.interrupt_and_join_all();
		nonInterruptableThreads.join_all();
	}

	// 'lockNow == false' may only be used during unit tests. Normally we
	// should never call the callback while holding the lock.
	void asyncGet(const Options &options, const GetCallback &callback, bool lockNow = true) {
		DynamicScopedLock lock(syncher, lockNow);
		
		verifyInvariants();
		P_TRACE(2, "asyncGet(appRoot=" << options.appRoot << ")");
		
		SuperGroup *existingSuperGroup = findMatchingSuperGroup(options);
		if (OXT_LIKELY(existingSuperGroup != NULL)) {
			/* Best case: the app super group is already in the pool. Let's use it. */
			P_TRACE(2, "Found existing SuperGroup");
			existingSuperGroup->verifyInvariants();
			SessionPtr session = existingSuperGroup->get(options, callback);
			existingSuperGroup->verifyInvariants();
			verifyInvariants();
			P_TRACE(2, "asyncGet() finished");
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
			P_TRACE(2, "Spawning new SuperGroup");
			SuperGroupPtr superGroup = createSuperGroupAndAsyncGetFromIt(options, callback);
			superGroup->verifyInvariants();
			verifyInvariants();
			P_TRACE(2, "asyncGet() finished");
			
		} else {
			vector<Callback> actions;
			
			/* Uh oh, the app super group isn't in the pool but we don't
			 * have the resources to make a new one. The sysadmin should
			 * configure the system to let something like this happen
			 * as least as possible, but let's try to handle it as well
			 * as we can.
			 *
			 * First, try to trash an idle process that's the oldest.
			 */
			P_TRACE(2, "Pool is at full capacity; trying to free a process...");
			ProcessPtr process = findOldestIdleProcess();
			if (process == NULL) {
				/* All processes are doing something. We have no choice
				 * but to trash a non-idle process.
				 */
				if (options.allowTrashingNonIdleProcesses) {
					process = findBestProcessToTrash();
				}
			} else {
				// Check invariant.
				P_ASSERT(process->getGroup()->getWaitlist.empty());
			}
			if (process == NULL) {
				/* All (super)groups are currently initializing/restarting/spawning/etc
				 * so nothing can be killed. We have no choice but to satisfy this
				 * get() action later when resources become available.
				 */
				P_TRACE(2, "Could not free a process; putting request to getWaitlist");
				getWaitlist.push_back(GetWaiter(options, callback));
			} else {
				GroupPtr group;
				SuperGroupPtr superGroup;
				
				P_TRACE(2, "Freeing process " << process->inspect());
				group = process->getGroup();
				P_ASSERT(group != NULL);
				superGroup = group->getSuperGroup();
				P_ASSERT(superGroup != NULL);
				
				group->detach(process, actions);
				if (superGroup->garbageCollectable()) {
					P_ASSERT(group->garbageCollectable());
					forceDetachSuperGroup(superGroup, actions);
					P_ASSERT(superGroup->getWaitlist.empty());
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
				P_ASSERT(session == NULL);
				superGroup->verifyInvariants();
			}
			
			P_ASSERT(atFullCapacity(false));
			verifyInvariants();
			verifyExpensiveInvariants();
			P_TRACE(2, "asyncGet() finished");
			
			if (!actions.empty()) {
				if (lockNow) {
					lock.unlock();
					runAllActions(actions);
				} else {
					// This state is not allowed. If we reach
					// here then it probably indicates a bug in
					// the test suite.
					P_ABORT();
				}
			}
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
			SessionPtr session = ticket->session;
			ticket->session.reset();
			return session;
		} else {
			rethrowException(ticket->exception);
			return SessionPtr(); // Shut up compiler warning.
		}
	}
	
	GroupPtr findOrCreateGroup(const Options &options) {
		Options options2 = options;
		options2.noop = true;
		
		Ticket ticket;
		{
			LockGuard l(syncher);
			if (superGroups.get(options.getAppGroupName()) == NULL) {
				// Forcefully create SuperGroup, don't care whether resource limits
				// actually allow it.
				createSuperGroup(options);
			}
		}
		return get(options2, &ticket)->getProcess()->getGroup();
	}
	
	void setMax(unsigned int max) {
		ScopedLock l(syncher);
		P_ASSERT(max > 0);
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

	void activateNewMaxIdleTime() {
		LockGuard l(syncher);
		garbageCollectionTimer.stop();
		garbageCollectionTimer.start(maxIdleTime / 1000000.0, 0.0);
	}

	void setMaxIdleTime(unsigned long long value) {
		{
			LockGuard l(syncher);
			maxIdleTime = value;
		}
		libev->run(boost::bind(&Pool::activateNewMaxIdleTime, this));
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

	vector<ProcessPtr> getProcesses() const {
		LockGuard l(syncher);
		vector<ProcessPtr> result;
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); OXT_LIKELY(it != end); it++) {
			const SuperGroupPtr &superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
				ProcessList::const_iterator p_it;

				for (p_it = group->processes.begin(); p_it != group->processes.end(); p_it++) {
					result.push_back(*p_it);
				}
				for (p_it = group->disabledProcesses.begin(); p_it != group->disabledProcesses.end(); p_it++) {
					result.push_back(*p_it);
				}
			}
		}
		return result;
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
		P_ASSERT(lock || postLockActions != NULL);
		DynamicScopedLock l(syncher, lock);
		
		if (OXT_LIKELY(superGroup->getPool().get() == this)) {
			if (OXT_LIKELY(superGroups.get(superGroup->name) != NULL)) {
				verifyInvariants();
				verifyExpensiveInvariants();
				
				vector<Callback> actions;
				
				forceDetachSuperGroup(superGroup, actions);
				/* If this SuperGroup had get waiters, either
				 * on itself or in one of its groups, then we must
				 * reprocess them immediately. Detaching such a
				 * SuperGroup is essentially the same as restarting it.
				 */
				migrateSuperGroupGetWaitlistToPool(superGroup);
				
				assignSessionsToGetWaiters(actions);
				possiblySpawnMoreProcessesForExistingGroups();
				
				verifyInvariants();
				verifyExpensiveInvariants();
				
				if (lock) {
					l.unlock();
					runAllActions(actions);
				} else if (postLockActions->empty()) {
					*postLockActions = actions;
				} else {
					postLockActions->reserve(postLockActions->size() +
						actions.size());
					for (unsigned int i = 0; i < actions.size(); i++) {
						postLockActions->push_back(actions[i]);
					}
				}
				
				return true;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}
	
	bool detachProcess(const ProcessPtr &process) {
		ScopedLock l(syncher);
		vector<Callback> actions;
		bool result = detachProcessUnlocked(process, actions);
		l.unlock();
		runAllActions(actions);
		return result;
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
		ScopedLock l(syncher);
		ProcessPtr process = findProcessByGupid(gupid, false);
		if (process != NULL) {
			vector<Callback> actions;
			bool result = detachProcessUnlocked(process, actions);
			l.unlock();
			runAllActions(actions);
			return result;
		} else {
			return false;
		}
	}

	string inspect(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		stringstream result;
		
		result << "----------- General information -----------" << endl;
		result << "Max pool size     : " << max << endl;
		result << "Processes         : " << getProcessCount(false) << endl;
		result << "Requests in queue : " << getWaitlist.size() << endl;
		//result << "active   = " << active << endl;
		//result << "inactive = " << inactiveApps.size() << endl;
		result << endl;
		
		result << "----------- Groups -----------" << endl;
		SuperGroupMap::const_iterator sg_it, sg_end = superGroups.end();
		for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
			const SuperGroupPtr &superGroup = sg_it->second;
			const Group *group = superGroup->defaultGroup;
			ProcessList::const_iterator p_it;
			
			if (group != NULL) {
				result << group->name << ":" << endl;
				if (group->spawning()) {
					result << "  (spawning new process...)" << endl;
				}
				result << "  Requests in queue: " << group->getWaitlist.size() << endl;
				for (p_it = group->processes.begin(); p_it != group->processes.end(); p_it++) {
					const ProcessPtr &process = *p_it;
					char buf[128];
					
					snprintf(buf, sizeof(buf),
							"PID: %-5lu   Sessions: %-2u   Processed: %-5u   Uptime: %s",
							(unsigned long) process->pid,
							process->sessions,
							process->processed,
							process->uptime().c_str());
					result << "  " << buf << endl;
				}
				result << endl;
			}
		}
		return result.str();
	}

	string toXml(bool includeSecrets = true) const {
		LockGuard l(syncher);
		stringstream result;
		SuperGroupMap::const_iterator sg_it;
		vector<GroupPtr>::const_iterator g_it;
		ProcessList::const_iterator p_it;

		result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
		result << "<info version=\"2\">";
		
		result << "<process_count>" << getProcessCount(false) << "</process_count>";
		result << "<max>" << max << "</max>";
		result << "<usage>" << usage(false) << "</usage>";
		result << "<get_wait_list_size>" << getWaitlist.size() << "</get_wait_list_size>";
		
		result << "<supergroups>";
		for (sg_it = superGroups.begin(); sg_it != superGroups.end(); sg_it++) {
			const SuperGroupPtr &superGroup = sg_it->second;

			result << "<name>" << escapeForXml(superGroup->name) << "</name>";
			result << "<state>" << superGroup->getStateName() << "</state>";
			result << "<get_wait_list_size>" << superGroup->getWaitlist.size() << "</get_wait_list_size>";
			result << "<usage>" << superGroup->usage() << "</usage>";
			if (includeSecrets) {
				result << "<secret>" << escapeForXml(superGroup->secret) << "</secret>";
			}

			for (g_it = superGroup->groups.begin(); g_it != superGroup->groups.end(); g_it++) {
				const GroupPtr &group = *g_it;

				if (group->componentInfo.isDefault) {
					result << "<group default=\"true\">";
				} else {
					result << "<group>";
				}
				group->inspectXml(result, includeSecrets);
				result << "</group>";
			}
		}
		result << "</supergroups>";

		result << "</info>";
		return result.str();
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_POOL_H_ */
