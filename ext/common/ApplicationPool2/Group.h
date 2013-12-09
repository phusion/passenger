/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2013 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL2_GROUP_H_
#define _PASSENGER_APPLICATION_POOL2_GROUP_H_

#include <string>
#include <map>
#include <queue>
#include <deque>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/macros.hpp>
#include <oxt/thread.hpp>
#include <oxt/dynamic_thread_group.hpp>
#include <cassert>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/ComponentInfo.h>
#include <ApplicationPool2/SpawnerFactory.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <Utils.h>
#include <Utils/CachedFileStat.hpp>
#include <Utils/FileChangeChecker.h>
#include <Utils/SmallVector.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * Except for otherwise documented parts, this class is not thread-safe,
 * so only access within ApplicationPool lock.
 */
class Group: public boost::enable_shared_from_this<Group> {
private:
	friend class Pool;
	friend class SuperGroup;
	
	struct GetAction {
		GetCallback callback;
		SessionPtr session;
	};
	
	struct DisableWaiter {
		ProcessPtr process;
		DisableCallback callback;
		
		DisableWaiter(const ProcessPtr &_process, const DisableCallback &_callback)
			: process(_process),
			  callback(_callback)
			{ }
	};
	
	/**
	 * Protects `m_shuttingDown`.
	 */
	mutable boost::mutex lifetimeSyncher;
	/**
	 * A back reference to the containing SuperGroup. Should never
	 * be NULL because a SuperGroup should outlive all its containing
	 * Groups.
	 * Read-only; only set during initialization.
	 */
	boost::weak_ptr<SuperGroup> superGroup;
	CachedFileStat cstat;
	FileChangeChecker fileChangeChecker;
	string restartFile;
	string alwaysRestartFile;

	/** Number of times a restart has been initiated so far. This is incremented immediately
	 * in Group::restart(), and is used to abort the restarter thread that was active at the
	 * time the restart was initiated. It's safe for the value to wrap around.
	 */
	unsigned int restartsInitiated;
	/**
	 * Whether process(es) are being spawned right now.
	 */
	bool m_spawning;
	/** Whether a non-rolling restart is in progress (i.e. whether spawnThreadRealMain()
	 * is at work). While it is in progress, it is not possible to signal the desire to
	 * spawn new process. If spawning was already in progress when the restart was initiated,
	 * then the spawning will abort as soon as possible.
	 *
	 * When rolling restarting is in progress, this flag is false.
	 *
	 * Invariant:
	 *    if m_restarting: !m_spawning
	 */
	bool m_restarting;
	/**
	 * Do not access directly, always use `isAlive()`/`getLifeStatus()` or
	 * through `lifetimeSyncher`.
	 * 
	 * Invariant:
	 *    if lifeStatus != ALIVE:
	 *       enabledCount == 0
	 *       disablingCount == 0
	 *       disabledCount == 0
	 */
	enum LifeStatus {
		/** Up and operational. */
		ALIVE,
		/** Being shut down. The containing SuperGroup has issued the shutdown()
		 * command, and this Group is now waiting for all detached processes to
		 * exit. You cannot call `get()`, `restart()` and other mutating methods
		 * anymore, and all threads created by this Group will exit as soon
		 * as possible.
		 */
		SHUTTING_DOWN,
		/**
		 * Shut down. Object no longer usable. No Processes are referenced from
		 * this Group anymore.
		 */
		SHUT_DOWN
	} lifeStatus;

	/** Contains the spawn loop thread and the restarter thread. */
	dynamic_thread_group interruptableThreads;

	/** This timer scans `detachedProcesses` periodically to see
	 * whether any of the Processes can be shut down.
	 */
	bool detachedProcessesCheckerActive;
	boost::condition_variable detachedProcessesCheckerCond;
	Callback shutdownCallback;
	GroupPtr selfPointer;
	
	
	static void _onSessionInitiateFailure(Session *session) {
		ProcessPtr process = session->getProcess();
		assert(process != NULL);
		process->getGroup()->onSessionInitiateFailure(process, session);
	}

	static void _onSessionClose(Session *session) {
		ProcessPtr process = session->getProcess();
		assert(process != NULL);
		process->getGroup()->onSessionClose(process, session);
	}
	
	static string generateSecret(const SuperGroupPtr &superGroup);
	void onSessionInitiateFailure(const ProcessPtr &process, Session *session);
	void onSessionClose(const ProcessPtr &process, Session *session);

	/** Returns whether it is allowed to perform a new OOBW in this group. */
	bool oobwAllowed() const;
	/** Returns whether a new OOBW should be initiated for this process. */
	bool shouldInitiateOobw(const ProcessPtr &process) const;
	void maybeInitiateOobw(const ProcessPtr &process);
	void lockAndMaybeInitiateOobw(const ProcessPtr &process, DisableResult result, GroupPtr self);
	void initiateOobw(const ProcessPtr &process);
	void spawnThreadOOBWRequest(GroupPtr self, ProcessPtr process);
	void initiateNextOobwRequest();

	void spawnThreadMain(GroupPtr self, SpawnerPtr spawner, Options options,
		unsigned int restartsInitiated);
	void spawnThreadRealMain(const SpawnerPtr &spawner, const Options &options,
		unsigned int restartsInitiated);
	void finalizeRestart(GroupPtr self, Options options, SpawnerFactoryPtr spawnerFactory,
		unsigned int restartsInitiated, vector<Callback> postLockActions);
	void startCheckingDetachedProcesses(bool immediately);
	void detachedProcessesCheckerMain(GroupPtr self);
	void wakeUpGarbageCollector();
	bool poolAtFullCapacity() const;
	bool anotherGroupIsWaitingForCapacity() const;
	bool testOverflowRequestQueue() const;
	const ResourceLocator &getResourceLocator() const;

	void verifyInvariants() const {
		// !a || b: logical equivalent of a IMPLIES b.
		
		assert(enabledCount >= 0);
		assert(disablingCount >= 0);
		assert(disabledCount >= 0);
		assert(enabledProcesses.empty() == (pqueue.top() == NULL));
		assert(!( enabledCount == 0 && disablingCount > 0 ) || spawning());
		assert(!( !spawning() ) || ( enabledCount > 0 || disablingCount == 0 ));

		assert((lifeStatus == ALIVE) == (spawner != NULL));

		// Verify getWaitlist invariants.
		assert(!( !getWaitlist.empty() ) || ( enabledProcesses.empty() || pqueue.top()->atFullUtilization() ));
		assert(!( !enabledProcesses.empty() && !pqueue.top()->atFullUtilization() ) || ( getWaitlist.empty() ));
		assert(!( enabledProcesses.empty() && !spawning() && !restarting() && !poolAtFullCapacity() ) || ( getWaitlist.empty() ));
		assert(!( !getWaitlist.empty() ) || ( !enabledProcesses.empty() || spawning() || restarting() || poolAtFullCapacity() ));
		
		// Verify disableWaitlist invariants.
		assert((int) disableWaitlist.size() >= disablingCount);

		// Verify m_spawning and m_restarting.
		assert(!( m_restarting ) || !m_spawning);

		// Verify lifeStatus.
		LifeStatus lifeStatus = getLifeStatus();
		assert(!( lifeStatus != ALIVE ) || ( enabledCount == 0 ));
		assert(!( lifeStatus != ALIVE ) || ( disablingCount == 0 ));
		assert(!( lifeStatus != ALIVE ) || ( disabledCount == 0 ));
	}

	void verifyExpensiveInvariants() const {
		#ifndef NDEBUG
		// !a || b: logical equivalent of a IMPLIES b.

		assert((int) enabledProcesses.size() == enabledCount);
		assert((int) disablingProcesses.size() == disablingCount);
		assert((int) disabledProcesses.size() == disabledCount);

		ProcessList::const_iterator it, end;

		end = enabledProcesses.end();
		for (it = enabledProcesses.begin(); it != end; it++) {
			const ProcessPtr &process = *it;
			assert(process->enabled == Process::ENABLED);
			assert(process->pqHandle != NULL);
			assert(process->isAlive());
			assert(process->oobwStatus == Process::OOBW_NOT_ACTIVE
				|| process->oobwStatus == Process::OOBW_REQUESTED);
		}

		end = disablingProcesses.end();
		for (it = disablingProcesses.begin(); it != end; it++) {
			const ProcessPtr &process = *it;
			assert(process->enabled == Process::DISABLING);
			assert(process->pqHandle == NULL);
			assert(process->isAlive());
			assert(process->oobwStatus == Process::OOBW_NOT_ACTIVE
				|| process->oobwStatus == Process::OOBW_IN_PROGRESS);
		}

		end = disabledProcesses.end();
		for (it = disabledProcesses.begin(); it != end; it++) {
			const ProcessPtr &process = *it;
			assert(process->enabled == Process::DISABLED);
			assert(process->pqHandle == NULL);
			assert(process->isAlive());
			assert(process->oobwStatus == Process::OOBW_NOT_ACTIVE
				|| process->oobwStatus == Process::OOBW_IN_PROGRESS);
		}

		foreach (const ProcessPtr &process, detachedProcesses) {
			assert(process->enabled == Process::DETACHED);
			assert(process->pqHandle == NULL);
		}
		#endif
	}
	
	/**
	 * Sets options for this Group. Called at creation time and at restart time.
	 */
	void resetOptions(const Options &newOptions) {
		options = newOptions;
		options.persist(newOptions);
		options.clearPerRequestFields();
		options.groupSecret = secret;
	}
	
	/**
	 * Merges some of the new options from the latest get() request into this Group.
	 */
	void mergeOptions(const Options &other) {
		options.maxRequests      = other.maxRequests;
		options.minProcesses     = other.minProcesses;
		options.statThrottleRate = other.statThrottleRate;
		options.maxPreloaderIdleTime = other.maxPreloaderIdleTime;
	}
	
	static void runAllActions(const vector<Callback> &actions) {
		vector<Callback>::const_iterator it, end = actions.end();
		for (it = actions.begin(); it != end; it++) {
			(*it)();
		}
	}

	static void doCleanupSpawner(SpawnerPtr spawner) {
		spawner->cleanup();
	}
	
	SessionPtr newSession(Process *process = NULL) {
		if (process == NULL) {
			assert(enabledCount > 0);
			process = pqueue.top();
		}
		SessionPtr session = process->newSession();
		session->onInitiateFailure = _onSessionInitiateFailure;
		session->onClose   = _onSessionClose;
		if (process->enabled == Process::ENABLED) {
			assert(process == pqueue.top());
			pqueue.pop();
			process->pqHandle = pqueue.push(process, process->utilization());
		}
		return session;
	}

	bool pushGetWaiter(const Options &newOptions, const GetCallback &callback) {
		if (OXT_LIKELY(!testOverflowRequestQueue()
			&& (newOptions.maxRequestQueueSize == 0
			    || getWaitlist.size() < newOptions.maxRequestQueueSize)))
		{
			getWaitlist.push(GetWaiter(newOptions.copyAndPersist().clearLogger(), callback));
			return true;
		} else {
			P_WARN("Request queue is full. Returning an error");
			callback(SessionPtr(), boost::make_shared<RequestQueueFullException>());
			return false;
		}
	}

	Process *findProcessWithLowestUtilization(const ProcessList &processes) const {
		Process *result = NULL;
		ProcessList::const_iterator it, end = processes.end();
		for (it = processes.begin(); it != end; it++) {
			Process *process = it->get();
			if (result == NULL || process->utilization() < result->utilization()) {
				result = process;
			}
		}
		return result;
	}

	/**
	 * Removes a process to the given list (enabledProcess, disablingProcesses, disabledProcesses).
	 * This function does not fix getWaitlist invariants or other stuff.
	 */
	void removeProcessFromList(const ProcessPtr &process, ProcessList &source) {
		ProcessPtr p = process; // Keep an extra reference count just in case.
		source.erase(process->it);
		switch (process->enabled) {
		case Process::ENABLED:
			assert(&source == &enabledProcesses);
			enabledCount--;
			pqueue.erase(process->pqHandle);
			process->pqHandle = NULL;
			break;
		case Process::DISABLING:
			assert(&source == &disablingProcesses);
			disablingCount--;
			break;
		case Process::DISABLED:
			assert(&source == &disabledProcesses);
			disabledCount--;
			break;
		case Process::DETACHED:
			assert(&source == &detachedProcesses);
			break;
		default:
			P_BUG("Unknown 'enabled' state " << (int) process->enabled);
		}
	}

	/**
	 * Adds a process to the given list (enabledProcess, disablingProcesses, disabledProcesses)
	 * and sets the process->enabled flag accordingly.
	 * The process must currently not be in any list. This function does not fix
	 * getWaitlist invariants or other stuff.
	 */
	void addProcessToList(const ProcessPtr &process, ProcessList &destination) {
		destination.push_back(process);
		process->it = destination.last_iterator();
		if (&destination == &enabledProcesses) {
			process->enabled = Process::ENABLED;
			process->pqHandle = pqueue.push(process.get(), process->utilization());
			enabledCount++;
		} else if (&destination == &disablingProcesses) {
			process->enabled = Process::DISABLING;
			disablingCount++;
		} else if (&destination == &disabledProcesses) {
			assert(process->sessions == 0);
			process->enabled = Process::DISABLED;
			disabledCount++;
		} else if (&destination == &detachedProcesses) {
			assert(process->isAlive());
			process->enabled = Process::DETACHED;
		} else {
			P_BUG("Unknown destination list");
		}
	}
	
	template<typename Lock>
	void assignSessionsToGetWaitersQuickly(Lock &lock) {
		SmallVector<GetAction, 50> actions;
		actions.reserve(getWaitlist.size());
		
		// Checkout sessions from enabled processes, or if there are none,
		// from disabling processes.
		if (enabledCount > 0) {
			while (!getWaitlist.empty() && pqueue.top() != NULL && !pqueue.top()->atFullUtilization()) {
				GetAction action;
				action.callback = getWaitlist.front().callback;
				action.session  = newSession();
				getWaitlist.pop();
				actions.push_back(action);
			}
		} else if (disablingCount > 0) {
			bool done = false;
			while (!getWaitlist.empty() && !done) {
				Process *process = findProcessWithLowestUtilization(
					disablingProcesses);
				assert(process != NULL);
				if (process->atFullUtilization()) {
					done = true;
				} else {
					GetAction action;
					action.callback = getWaitlist.front().callback;
					action.session  = newSession(process);
					getWaitlist.pop();
					actions.push_back(action);
				}
			}
		}
		
		verifyInvariants();
		lock.unlock();
		SmallVector<GetAction, 50>::const_iterator it, end = actions.end();
		for (it = actions.begin(); it != end; it++) {
			it->callback(it->session, ExceptionPtr());
		}
	}
	
	void assignSessionsToGetWaiters(vector<Callback> &postLockActions) {
		if (enabledCount > 0) {
			while (!getWaitlist.empty() && pqueue.top() != NULL && !pqueue.top()->atFullUtilization()) {
				postLockActions.push_back(boost::bind(
					getWaitlist.front().callback, newSession(),
					ExceptionPtr()));
				getWaitlist.pop();
			}
		} else if (disablingCount > 0) {
			bool done = false;
			while (!getWaitlist.empty() && !done) {
				Process *process = findProcessWithLowestUtilization(
					disablingProcesses);
				assert(process != NULL);
				if (process->atFullUtilization()) {
					done = true;
				} else {
					postLockActions.push_back(boost::bind(
						getWaitlist.front().callback, newSession(process),
						ExceptionPtr()));
					getWaitlist.pop();
				}
			}
		}
	}

	void enableAllDisablingProcesses(vector<Callback> &postLockActions) {
		P_DEBUG("Enabling all DISABLING processes with result DR_ERROR");
		deque<DisableWaiter>::iterator it, end = disableWaitlist.end();
		for (it = disableWaitlist.begin(); it != end; it++) {
			const DisableWaiter &waiter = *it;
			const ProcessPtr process = waiter.process;
			// A process can appear multiple times in disableWaitlist.
			assert(process->enabled == Process::DISABLING
				|| process->enabled == Process::ENABLED);
			if (process->enabled == Process::DISABLING) {
				removeProcessFromList(process, disablingProcesses);
				addProcessToList(process, enabledProcesses);
				P_DEBUG("Enabled process " << process->inspect());
			}
		}
		clearDisableWaitlist(DR_ERROR, postLockActions);
	}
	
	void removeFromDisableWaitlist(const ProcessPtr &p, DisableResult result,
		vector<Callback> &postLockActions)
	{
		deque<DisableWaiter>::const_iterator it, end = disableWaitlist.end();
		deque<DisableWaiter> newList;
		for (it = disableWaitlist.begin(); it != end; it++) {
			const DisableWaiter &waiter = *it;
			const ProcessPtr process = waiter.process;
			if (process == p) {
				postLockActions.push_back(boost::bind(waiter.callback, p, result));
			} else {
				newList.push_back(waiter);
			}
		}
		disableWaitlist = newList;
	}

	void clearDisableWaitlist(DisableResult result, vector<Callback> &postLockActions) {
		// This function may be called after processes in the disableWaitlist
		// have been disabled or enabled, so do not assume any value for
		// waiter.process->enabled in this function.
		postLockActions.reserve(postLockActions.size() + disableWaitlist.size());
		while (!disableWaitlist.empty()) {
			const DisableWaiter &waiter = disableWaitlist.front();
			postLockActions.push_back(boost::bind(waiter.callback, waiter.process, result));
			disableWaitlist.pop_front();
		}
	}

	bool shutdownCanFinish() const {
		return getLifeStatus() == SHUTTING_DOWN
			&& enabledCount == 0
			&& disablingCount == 0
	 		&& disabledCount == 0
	 		&& detachedProcesses.empty();
	}

	static void interruptAndJoinAllThreads(GroupPtr self) {
		self->interruptableThreads.interrupt_and_join_all();
	}

	/** One of the post lock actions can potentially perform a long-running
	 * operation, so running them in a thread is advised.
	 */
	void finishShutdown(vector<Callback> &postLockActions) {
		TRACE_POINT();
		assert(getLifeStatus() == SHUTTING_DOWN);
		P_DEBUG("Finishing shutdown of group " << name);
		if (shutdownCallback) {
			postLockActions.push_back(shutdownCallback);
			shutdownCallback = Callback();
		}
		postLockActions.push_back(boost::bind(interruptAndJoinAllThreads,
			shared_from_this()));
		{
			boost::lock_guard<boost::mutex> l(lifetimeSyncher);
			lifeStatus = SHUT_DOWN;
		}
		selfPointer.reset();
	}
	
public:
	Options options;
	/** This name uniquely identifies this Group within its Pool. It can also be used as the display name. */
	const string name;
	/** A secret token that may be known among all processes in this Group. Used for securing
	 * intra-group process communication.
	 */
	const string secret;
	ComponentInfo componentInfo;
	
	/**
	 * Processes are categorized as enabled, disabling or disabled.
	 *
	 * - get() requests should go to enabled processes.
	 * - Disabling processes are allowed to finish their current requests,
	 *   but they generally will not receive any new requests. The only
	 *   exception is when there are no enabled processes. In this case,
	 *   a new process will be spawned while in the mean time all requests
	 *   go to one of the disabling processes. Disabling processes become
	 *   disabled as soon as they finish all their requests and there are
	 *   enabled processes.
	 * - Disabled processes never handle requests.
	 *
	 * 'enabledProcesses', 'disablingProcesses' and 'disabledProcesses' contain
	 * all enabled, disabling and disabling processes in this group, respectively.
	 * 'enabledCount', 'disablingCount' and 'disabledCount' are used to maintain
	 * their numbers.
	 * These lists do not intersect. A process is in exactly 1 list.
	 *
	 * 'pqueue' orders all enabled processes according to utilization() values,
	 * from small to large.
	 *
	 * Invariants:
	 *    enabledCount >= 0
	 *    disablingCount >= 0
	 *    disabledCount >= 0
	 *    enabledProcesses.size() == enabledCount
	 *    disablingProcesses.size() == disabingCount
	 *    disabledProcesses.size() == disabledCount
     *
	 *    enabledProcesses.empty() == (pqueue.top() == NULL)
	 *
	 *    if (enabledCount == 0):
	 *       spawning() || restarting() || poolAtFullCapacity()
	 *    if (enabledCount == 0) and (disablingCount > 0):
	 *       spawning()
	 *    if !spawning():
	 *       (enabledCount > 0) or (disablingCount == 0)
	 *
	 *    if pqueue.top().atFullUtilization():
	 *       All enabled processes are at full utilization.
	 *
	 *    for all process in enabledProcesses:
	 *       process.enabled == Process::ENABLED
	 *       process.pqHandle != NULL
	 *       process.isAlive()
	 *       process.oobwStatus == Process::OOBW_NOT_ACTIVE || process.oobwStatus == Process::OOBW_REQUESTED
	 *    for all processes in disablingProcesses:
	 *       process.enabled == Process::DISABLING
	 *       process.pqHandle == NULL
	 *       process.isAlive()
	 *       process.oobwStatus == Process::OOBW_NOT_ACTIVE || process.oobwStatus == Process::OOBW_IN_PROGRESS
	 *    for all process in disabledProcesses:
	 *       process.enabled == Process::DISABLED
	 *       process.pqHandle == NULL
	 *       process.isAlive()
	 *       process.oobwStatus == Process::OOBW_NOT_ACTIVE || process.oobwStatus == Process::OOBW_IN_PROGRESS
	 */
	int enabledCount;
	int disablingCount;
	int disabledCount;
	PriorityQueue<Process> pqueue;
	ProcessList enabledProcesses;
	ProcessList disablingProcesses;
	ProcessList disabledProcesses;

	/**
	 * When a process is detached, it is stored here until we've confirmed
	 * that the OS process has exited.
	 *
	 * for all process in detachedProcesses:
	 *    process.enabled == Process::DETACHED
	 *    process.pqHandle == NULL
	 */
	ProcessList detachedProcesses;
	
	/**
	 * get() requests for this group that cannot be immediately satisfied are
	 * put on this wait list, which must be processed as soon as the necessary
	 * resources have become free.
	 * 
	 * 'std::' is required because Solaris in its infinite wisdom has a C
	 * struct in its system headers called 'queue'.
	 * http://code.google.com/p/phusion-passenger/issues/detail?id=840
	 *
	 * Invariant 1 (safety):
	 * If actions are queued in the getWaitlist, then that's because there are
	 * no processes that can serve them.
	 *    if getWaitlist is non-empty:
	 *       enabledProcesses.empty() || (all enabled processes are at full utilization)
	 * Equivalently:
	 *    if !enabledProcesses.empty() && (an enabled process is not at full utilization):
	 *        getWaitlist is empty.
	 *
	 * Invariant 2:
	 *    if enabledProcesses.empty() && !spawning() && !restarting() && !poolAtFullCapacity():
	 *       getWaitlist is empty
	 * Equivalently:
	 *    if getWaitlist is non-empty:
	 *       !enabledProcesses.empty() || spawning() || restarting() || poolAtFullCapacity()
	 */
	std::queue<GetWaiter> getWaitlist;
	/**
	 * Disable() commands that couldn't finish immediately will put their callbacks
	 * in this queue. Note that there may be multiple DisableWaiters pointing to the
	 * same Process.
	 *
	 * Invariant:
	 *    disableWaitlist.size() >= disablingCount
	 */
	deque<DisableWaiter> disableWaitlist;
	
	/**
	 * Invariant:
	 *    (lifeStatus == ALIVE) == (spawner != NULL)
	 */
	SpawnerPtr spawner;
	
	Group(const SuperGroupPtr &superGroup, const Options &options, const ComponentInfo &info);
	~Group();

	/**
	 * Must be called before destroying a Group. You can optionally provide a
	 * callback so that you are notified when shutdown has finished.
	 *
	 * The caller is responsible for migrating waiters on the getWaitlist.
	 *
	 * One of the post lock actions can potentially perform a long-running
	 * operation, so running them in a thread is advised.
	 */
	void shutdown(const Callback &callback, vector<Callback> &postLockActions) {
		assert(isAlive());

		P_DEBUG("Begin shutting down group " << name);
		shutdownCallback = callback;
		detachAll(postLockActions);
		startCheckingDetachedProcesses(true);
		interruptableThreads.interrupt_all();
		postLockActions.push_back(boost::bind(doCleanupSpawner, spawner));
		spawner.reset();
		selfPointer = shared_from_this();
		assert(disableWaitlist.empty());
		{
			boost::lock_guard<boost::mutex> l(lifetimeSyncher);
			lifeStatus = SHUTTING_DOWN;
		}
	}

	SessionPtr get(const Options &newOptions, const GetCallback &callback) {
		assert(isAlive());

		if (OXT_LIKELY(!restarting())) {
			if (OXT_UNLIKELY(needsRestart(newOptions))) {
				restart(newOptions);
			} else {
				mergeOptions(newOptions);
			}
			if (OXT_UNLIKELY(!newOptions.noop && shouldSpawnForGetAction())) {
				spawn();
			}
		}
		
		if (OXT_UNLIKELY(newOptions.noop)) {
			ProcessPtr process = boost::make_shared<Process>(SafeLibevPtr(),
				0, string(), string(),
				FileDescriptor(), FileDescriptor(),
				SocketListPtr(), 0, 0);
			process->dummy = true;
			process->requiresShutdown = false;
			process->setGroup(shared_from_this());
			return boost::make_shared<Session>(process, (Socket *) NULL);
		}
		
		if (OXT_UNLIKELY(enabledCount == 0)) {
			/* We don't have any processes yet, but they're on the way.
			 *
			 * We have some choices here. If there are disabling processes
			 * then we generally want to use them, except:
			 * - When non-rolling restarting because those disabling processes
			 *   are from the old version.
			 * - When all disabling processes are at full utilization.
			 *
			 * Whenever a disabling process cannot be used, call the callback
			 * after a process has been spawned or has failed to spawn, or
			 * when a disabling process becomes available.
			 */
			assert(spawning() || restarting() || poolAtFullCapacity());

			if (disablingCount > 0 && !restarting()) {
				Process *process = findProcessWithLowestUtilization(
					disablingProcesses);
				assert(process != NULL);
				if (!process->atFullUtilization()) {
					return newSession(process);
				}
			}

			if (pushGetWaiter(newOptions, callback)) {
				P_DEBUG("No session checked out yet: group is spawning or restarting");
			}
			return SessionPtr();
		} else {
			Process *process = pqueue.top();
			assert(process != NULL);
			if (process->atFullUtilization()) {
				/* Looks like all processes are at full utilization.
				 * Wait until a new one has been spawned or until
				 * resources have become free.
				 */
				if (pushGetWaiter(newOptions, callback)) {
					P_DEBUG("No session checked out yet: all processes are at full capacity");
				}
				return SessionPtr();
			} else {
				P_DEBUG("Session checked out from process " << process->inspect());
				return newSession();
			}
		}
	}
	
	/**
	 * Thread-safe.
	 * @pre getLifeState() != SHUT_DOWN
	 * @post result != NULL
	 */
	SuperGroupPtr getSuperGroup() const {
		return superGroup.lock();
	}
	
	void setSuperGroup(const SuperGroupPtr &superGroup) {
		assert(this->superGroup.lock() == NULL);
		this->superGroup = superGroup;
	}
	
	/**
	 * Thread-safe.
	 * @pre getLifeState() != SHUT_DOWN
	 * @post result != NULL
	 */
	PoolPtr getPool() const;
	
	// Thread-safe.
	bool isAlive() const {
		boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
		return lifeStatus == ALIVE;
	}

	// Thread-safe.
	LifeStatus getLifeStatus() const {
		boost::lock_guard<boost::mutex> lock(lifetimeSyncher);
		return lifeStatus;
	}
	
	// Thread-safe, but only call outside the pool lock!
	void requestOOBW(const ProcessPtr &process);
	
	/**
	 * Attaches the given process to this Group and mark it as enabled. This
	 * function doesn't touch getWaitlist so be sure to fix its invariants
	 * afterwards if necessary.
	 */
	void attach(const ProcessPtr &process, vector<Callback> &postLockActions) {
		assert(process->getGroup() == NULL || process->getGroup().get() == this);
		assert(process->isAlive());
		assert(isAlive());

		process->setGroup(shared_from_this());
		P_DEBUG("Attaching process " << process->inspect());
		addProcessToList(process, enabledProcesses);

		/* Now that there are enough resources, relevant processes in
		 * 'disableWaitlist' can be disabled.
		 */
		deque<DisableWaiter>::const_iterator it, end = disableWaitlist.end();
		deque<DisableWaiter> newDisableWaitlist;
		for (it = disableWaitlist.begin(); it != end; it++) {
			const DisableWaiter &waiter = *it;
			const ProcessPtr process2 = waiter.process;
			// The same process can appear multiple times in disableWaitlist.
			assert(process2->enabled == Process::DISABLING
				|| process2->enabled == Process::DISABLED);
			if (process2->sessions == 0) {
				if (process2->enabled == Process::DISABLING) {
					P_DEBUG("Disabling DISABLING process " << process2->inspect() <<
						"; disable command succeeded immediately");
					removeProcessFromList(process2, disablingProcesses);
					addProcessToList(process2, disabledProcesses);
				} else {
					P_DEBUG("Disabling (already disabled) DISABLING process " <<
						process2->inspect() << "; disable command succeeded immediately");
				}
				postLockActions.push_back(boost::bind(waiter.callback, process2, DR_SUCCESS));
			} else {
				newDisableWaitlist.push_back(waiter);
			}
		}
		disableWaitlist = newDisableWaitlist;

		// Update GC sleep timer.
		wakeUpGarbageCollector();
	}

	/**
	 * Detaches the given process from this Group. This function doesn't touch
	 * getWaitlist so be sure to fix its invariants afterwards if necessary.
	 * `pool->detachProcessUnlocked()` does that so you should usually use
	 * that method over this one.
	 */
	void detach(const ProcessPtr &process, vector<Callback> &postLockActions) {
		assert(process->getGroup().get() == this);
		assert(process->isAlive());
		assert(isAlive());

		const ProcessPtr p = process; // Keep an extra reference just in case.
		P_DEBUG("Detaching process " << process->inspect());

		if (process->enabled == Process::ENABLED || process->enabled == Process::DISABLING) {
			assert(enabledCount > 0 || disablingCount > 0);
			if (process->enabled == Process::ENABLED) {
				removeProcessFromList(process, enabledProcesses);
			} else {
				removeProcessFromList(process, disablingProcesses);
				removeFromDisableWaitlist(process, DR_NOOP, postLockActions);
			}
		} else {
			assert(!disabledProcesses.empty());
			removeProcessFromList(process, disabledProcesses);
		}

		addProcessToList(process, detachedProcesses);
		startCheckingDetachedProcesses(false);
	}
	
	/**
	 * Detaches all processes from this Group. This function doesn't touch
	 * getWaitlist so be sure to fix its invariants afterwards if necessary.
	 */
	void detachAll(vector<Callback> &postLockActions) {
		assert(isAlive());
		P_DEBUG("Detaching all processes in group " << name);

		foreach (ProcessPtr process, enabledProcesses) {
			addProcessToList(process, detachedProcesses);
			process->pqHandle = NULL;
		}
		foreach (ProcessPtr process, disablingProcesses) {
			addProcessToList(process, detachedProcesses);
		}
		foreach (ProcessPtr process, disabledProcesses) {
			addProcessToList(process, detachedProcesses);
		}
		
		enabledProcesses.clear();
		disablingProcesses.clear();
		disabledProcesses.clear();
		pqueue.clear();
		enabledCount = 0;
		disablingCount = 0;
		disabledCount = 0;
		clearDisableWaitlist(DR_NOOP, postLockActions);
		startCheckingDetachedProcesses(false);
	}
	
	/**
	 * Marks the given process as enabled. This function doesn't touch getWaitlist
	 * so be sure to fix its invariants afterwards if necessary.
	 */
	void enable(const ProcessPtr &process, vector<Callback> &postLockActions) {
		assert(process->getGroup().get() == this);
		assert(process->isAlive());
		assert(isAlive());

		if (process->enabled == Process::DISABLING) {
			P_DEBUG("Enabling DISABLING process " << process->inspect());
			removeProcessFromList(process, disablingProcesses);
			addProcessToList(process, enabledProcesses);
			removeFromDisableWaitlist(process, DR_CANCELED, postLockActions);
		} else if (process->enabled == Process::DISABLED) {
			P_DEBUG("Enabling DISABLED process " << process->inspect());
			removeProcessFromList(process, disabledProcesses);
			addProcessToList(process, enabledProcesses);
		} else {
			P_DEBUG("Enabling ENABLED process " << process->inspect());
		}
	}
	
	/**
	 * Marks the given process as disabled. Returns DR_SUCCESS, DR_DEFERRED
	 * or DR_NOOP. If the result is DR_DEFERRED, then the callback will be
	 * called later with the result of this action.
	 */
	DisableResult disable(const ProcessPtr &process, const DisableCallback &callback) {
		assert(process->getGroup().get() == this);
		assert(process->isAlive());
		assert(isAlive());

		if (process->enabled == Process::ENABLED) {
			P_DEBUG("Disabling ENABLED process " << process->inspect() <<
				"; enabledCount=" << enabledCount << ", process.sessions=" << process->sessions);
			assert(enabledCount >= 0);
			if (enabledCount == 1 && !allowSpawn()) {
				P_WARN("Cannot disable sole enabled process in group " << name <<
					" because spawning is not allowed according to the current" <<
					" configuration options");
				return DR_ERROR;
			} else if (enabledCount <= 1 || process->sessions > 0) {
				removeProcessFromList(process, enabledProcesses);
				addProcessToList(process, disablingProcesses);
				disableWaitlist.push_back(DisableWaiter(process, callback));
				if (enabledCount == 0) {
					/* All processes are going to be disabled, so in order
					 * to avoid blocking requests we first spawn a new process
					 * and disable this process after the other one is done
					 * spawning. We do this irregardless of resource limits
					 * because this is an exceptional situation.
					 */
					P_DEBUG("Spawning a new process to avoid the disable action from blocking requests");
					spawn();
				}
				P_DEBUG("Deferring disable command completion");
				return DR_DEFERRED;
			} else {
				removeProcessFromList(process, enabledProcesses);
				addProcessToList(process, disabledProcesses);
				P_DEBUG("Disable command succeeded immediately");
				return DR_SUCCESS;
			}
		} else if (process->enabled == Process::DISABLING) {
			assert(disablingCount > 0);
			disableWaitlist.push_back(DisableWaiter(process, callback));
			P_DEBUG("Disabling DISABLING process " << process->inspect() <<
				name << "; command queued, deferring disable command completion");
			return DR_DEFERRED;
		} else {
			assert(disabledCount > 0);
			P_DEBUG("Disabling DISABLED process " << process->inspect() <<
				name << "; disable command succeeded immediately");
			return DR_NOOP;
		}
	}

	void cleanupSpawner(vector<Callback> &postLockActions) {
		assert(isAlive());
		postLockActions.push_back(boost::bind(doCleanupSpawner, spawner));
	}

	unsigned int utilization() const {
		int result = enabledCount;
		if (spawning()) {
			result++;
		}
		return result;
	}
	
	bool garbageCollectable(unsigned long long now = 0) const {
		/* if (now == 0) {
			now = SystemTime::getUsec();
		}
		return utilization() == 0
			&& getWaitlist.empty()
			&& disabledProcesses.empty()
			&& options.getMaxPreloaderIdleTime() != 0
			&& now - spawner->lastUsed() >
				(unsigned long long) options.getMaxPreloaderIdleTime() * 1000000; */
		return false;
	}
	
	/** Whether a new process should be spawned for this group. */
	bool shouldSpawn() const;
	/** Whether a new process should be spawned for this group in the
	 * specific case that another get action is to be performed.
	 */
	bool shouldSpawnForGetAction() const;
	/** Whether a new process is allowed to be spawned for this group. */
	bool allowSpawn() const;
	
	/** Start spawning a new process in the background, in case this
	 * isn't already happening and the group isn't being restarted.
	 * Will ensure that at least options.minProcesses processes are spawned.
	 */
	void spawn() {
		assert(isAlive());
		if (!spawning() && !restarting()) {
			P_DEBUG("Requested spawning of new process for group " << name);
			interruptableThreads.create_thread(
				boost::bind(&Group::spawnThreadMain,
					this, shared_from_this(), spawner,
					options.copyAndPersist().clearPerRequestFields(),
					restartsInitiated),
				"Group process spawner: " + name,
				POOL_HELPER_THREAD_STACK_SIZE);
			m_spawning = true;
		}
	}
	
	bool needsRestart(const Options &options) {
		if (m_restarting) {
			return false;
		} else {
			struct stat buf;
			return cstat.stat(alwaysRestartFile, &buf, options.statThrottleRate) == 0 ||
			       fileChangeChecker.changed(restartFile, options.statThrottleRate);
		}
	}
	
	void restart(const Options &options);
	
	bool spawning() const {
		return m_spawning;
	}

	bool restarting() const {
		return m_restarting;
	}

	/**
	 * Returns the number of processes in this group that should be part for the
	 * MaxPoolSize constraint calculation.
	 */
	unsigned int getProcessCount() const {
		return enabledCount + disablingCount + disabledCount;
	}

	/**
	 * Checks whether this group is waiting for capacity on the pool to
	 * become available before it can continue processing requests.
	 */
	bool isWaitingForCapacity() const {
		return enabledProcesses.empty()
			&& !m_spawning
			&& !m_restarting
			&& !getWaitlist.empty();
	}

	template<typename Stream>
	void inspectXml(Stream &stream, bool includeSecrets = true) const {
		ProcessList::const_iterator it;

		stream << "<name>" << escapeForXml(name) << "</name>";
		stream << "<component_name>" << escapeForXml(componentInfo.name) << "</component_name>";
		stream << "<app_root>" << escapeForXml(options.appRoot) << "</app_root>";
		stream << "<app_type>" << escapeForXml(options.appType) << "</app_type>";
		stream << "<environment>" << escapeForXml(options.environment) << "</environment>";
		stream << "<enabled_process_count>" << enabledCount << "</enabled_process_count>";
		stream << "<disabling_process_count>" << disablingCount << "</disabling_process_count>";
		stream << "<disabled_process_count>" << disabledCount << "</disabled_process_count>";
		stream << "<utilization>" << utilization() << "</utilization>";
		stream << "<get_wait_list_size>" << getWaitlist.size() << "</get_wait_list_size>";
		stream << "<disable_wait_list_size>" << disableWaitlist.size() << "</disable_wait_list_size>";
		if (spawning()) {
			stream << "<spawning/>";
		}
		if (restarting()) {
			stream << "<restarting/>";
		}
		if (includeSecrets) {
			stream << "<secret>" << escapeForXml(secret) << "</secret>";
		}
		switch (lifeStatus) {
		case ALIVE:
			stream << "<life_status>ALIVE</life_status>";
			break;
		case SHUTTING_DOWN:
			stream << "<life_status>SHUTTING_DOWN</life_status>";
			break;
		case SHUT_DOWN:
			stream << "<life_status>SHUT_DOWN</life_status>";
			break;
		default:
			P_BUG("Unknown 'lifeStatus' state " << (int) lifeStatus);
		}

		stream << "<options>";
		options.toXml(stream, getResourceLocator());
		stream << "</options>";

		stream << "<processes>";
		
		for (it = enabledProcesses.begin(); it != enabledProcesses.end(); it++) {
			stream << "<process>";
			(*it)->inspectXml(stream, includeSecrets);
			stream << "</process>";
		}
		for (it = disablingProcesses.begin(); it != disablingProcesses.end(); it++) {
			stream << "<process>";
			(*it)->inspectXml(stream, includeSecrets);
			stream << "</process>";
		}
		for (it = disabledProcesses.begin(); it != disabledProcesses.end(); it++) {
			stream << "<process>";
			(*it)->inspectXml(stream, includeSecrets);
			stream << "</process>";
		}
		for (it = detachedProcesses.begin(); it != detachedProcesses.end(); it++) {
			stream << "<process>";
			(*it)->inspectXml(stream, includeSecrets);
			stream << "</process>";
		}

		stream << "</processes>";
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_GROUP_H_ */
