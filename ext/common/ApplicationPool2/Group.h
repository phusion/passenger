/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion
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
#include <boost/container/vector.hpp>
#include <boost/atomic.hpp>
#include <oxt/macros.hpp>
#include <oxt/thread.hpp>
#include <oxt/dynamic_thread_group.hpp>
#include <sys/stat.h>
#include <cstdlib>
#include <cassert>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Context.h>
#include <ApplicationPool2/BasicGroupInfo.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <SpawningKit/Factory.h>
#include <MemoryKit/palloc.h>
#include <Hooks.h>
#include <Utils.h>
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
// Actually private, but marked public so that unit tests can access the fields.
public:
	friend class Pool;

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

	struct RouteResult {
		Process *process;
		bool finished;

		RouteResult(Process *p, bool _finished = false)
			: process(p),
			  finished(_finished)
			{ }
	};

	enum LifeStatus {
		/** Up and operational. */
		ALIVE,
		/** Being shut down. The containing Pool has issued the shutdown()
		 * command, and this Group is now waiting for all detached processes to
		 * exit. You cannot call `get()`, `restart()` and other mutating methods
		 * anymore, and all threads created by this Group will exit as soon
		 * as possible.
		 */
		SHUTTING_DOWN,
		/**
		 * Shut down complete. Object no longer usable. No Processes are referenced
		 * from this Group anymore.
		 */
		SHUT_DOWN
	};

	BasicGroupInfo info;

	/**
	 * A back reference to the containing Pool. Should never
	 * be NULL because a Pool should outlive all its containing
	 * Groups.
	 * Read-only; only set during initialization.
	 */
	Pool *pool;
	time_t lastRestartFileMtime;
	time_t lastRestartFileCheckTime;

	/** Number of times a restart has been initiated so far. This is incremented immediately
	 * in Group::restart(), and is used to abort the restarter thread that was active at the
	 * time the restart was initiated. It's safe for the value to wrap around.
	 */
	unsigned int restartsInitiated;
	/**
	 * The number of processes that are being spawned right now.
	 *
	 * Invariant:
	 *     if processesBeingSpawned > 0: m_spawning
	 */
	short processesBeingSpawned;
	/**
	 * A Group object progresses through a life.
	 *
	 * You should not access this directly. You should use `isAlive()`/`getLifeStatus()`.
	 *
	 * Invariant:
	 *    if lifeStatus != ALIVE:
	 *       enabledCount == 0
	 *       disablingCount == 0
	 *       disabledCount == 0
	 *       nEnabledProcessesTotallyBusy == 0
	 */
	boost::atomic<boost::uint8_t> lifeStatus;
	/**
	 * Whether the spawner thread is currently working. Note that even
	 * if it's working, it doesn't necessarily mean that processes are
	 * being spawned (i.e. that processesBeingSpawned > 0). After the
	 * thread is done spawning a process, it will attempt to attach
	 * the newly-spawned process to the group. During that time it's not
	 * technically spawning anything.
	 */
	bool m_spawning: 1;
	/** Whether a non-rolling restart is in progress (i.e. whether spawnThreadRealMain()
	 * is at work). While it is in progress, it is not possible to signal the desire to
	 * spawn new process. If spawning was already in progress when the restart was initiated,
	 * then the spawning will abort as soon as possible.
	 *
	 * When rolling restarting is in progress, this flag is false.
	 *
	 * Invariant:
	 *    if m_restarting: processesBeingSpawned == 0
	 */
	bool m_restarting: 1;
	bool alwaysRestartFileExists: 1;

	/** Contains the spawn loop thread and the restarter thread. */
	dynamic_thread_group interruptableThreads;

	string restartFile;
	string alwaysRestartFile;
	ProcessPtr nullProcess;

	/** This timer scans `detachedProcesses` periodically to see
	 * whether any of the Processes can be shut down.
	 */
	bool detachedProcessesCheckerActive;
	boost::condition_variable detachedProcessesCheckerCond;
	Callback shutdownCallback;
	GroupPtr selfPointer;


	static void generateSecret(const Pool *pool, char *secret);
	static string generateUuid(const Pool *pool);
	static void _onSessionInitiateFailure(Session *session);
	static void _onSessionClose(Session *session);
	OXT_FORCE_INLINE void onSessionInitiateFailure(Process *process, Session *session);
	OXT_FORCE_INLINE void onSessionClose(Process *process, Session *session);

	/** Returns whether it is allowed to perform a new OOBW in this group. */
	bool oobwAllowed() const;
	/** Returns whether a new OOBW should be initiated for this process. */
	bool shouldInitiateOobw(Process *process) const;
	void maybeInitiateOobw(Process *process);
	void lockAndMaybeInitiateOobw(const ProcessPtr &process, DisableResult result, GroupPtr self);
	void initiateOobw(const ProcessPtr &process);
	void spawnThreadOOBWRequest(GroupPtr self, ProcessPtr process);
	void initiateNextOobwRequest();

	void spawnThreadMain(GroupPtr self, SpawningKit::SpawnerPtr spawner, Options options,
		unsigned int restartsInitiated);
	void spawnThreadRealMain(const SpawningKit::SpawnerPtr &spawner, const Options &options,
		unsigned int restartsInitiated);
	void finalizeRestart(GroupPtr self, Options oldOptions, Options newOptions,
		RestartMethod method, SpawningKit::FactoryPtr spawningKitFactory,
		unsigned int restartsInitiated, boost::container::vector<Callback> postLockActions);
	void startCheckingDetachedProcesses(bool immediately);
	void detachedProcessesCheckerMain(GroupPtr self);
	void wakeUpGarbageCollector();
	bool selfCheckingEnabled() const;
	bool poolAtFullCapacity() const;
	bool anotherGroupIsWaitingForCapacity() const;
	Group *findOtherGroupWaitingForCapacity() const;
	ProcessPtr poolForceFreeCapacity(const Group *exclude, boost::container::vector<Callback> &postLockActions);
	bool testOverflowRequestQueue() const;
	void callAbortLongRunningConnectionsCallback(const ProcessPtr &process);
	psg_pool_t *getPallocPool() const;
	const ResourceLocator &getResourceLocator() const;
	bool prepareHookScriptOptions(HookScriptOptions &hsOptions, const char *name);
	void runAttachHooks(const ProcessPtr process) const;
	void runDetachHooks(const ProcessPtr process) const;
	void setupAttachOrDetachHook(const ProcessPtr process, HookScriptOptions &options) const;

	#include <ApplicationPool2/Group/ProcessListManagement.cpp>
	#include <ApplicationPool2/Group/Utils.cpp>
	#include <ApplicationPool2/Group/Inspection.cpp>
	#include <ApplicationPool2/Group/Verification.cpp>

	ProcessPtr createProcessObject(const Json::Value &json) {
		struct Guard {
			Context *context;
			Process *process;

			Guard(Context *c, Process *s)
				: context(c),
				  process(s)
				{ }

			~Guard() {
				if (process != NULL) {
					context->getProcessObjectPool().free(process);
				}
			}

			void clear() {
				process = NULL;
			}
		};

		Context *context = getContext();
		LockGuard l(context->getMmSyncher());
		Process *process = context->getProcessObjectPool().malloc();
		Guard guard(context, process);
		process = new (process) Process(&info, json);
		guard.clear();
		return ProcessPtr(process, false);
	}

	/* Determines which process to route a get() action to. The returned process
	 * is guaranteed to be `canBeRoutedTo()`, i.e. not totally busy.
	 *
	 * A request is routed to an enabled processes, or if there are none,
	 * from a disabling process. The rationale is as follows:
	 * If there are no enabled process, then waiting for one to spawn is too
	 * expensive. The next best thing is to route to disabling processes
	 * until more processes have been spawned.
	 */
	RouteResult route(const Options &options) const {
		if (OXT_LIKELY(enabledCount > 0)) {
			if (options.stickySessionId == 0) {
				Process *process = findProcessWithLowestBusyness(enabledProcesses);
				if (process->canBeRoutedTo()) {
					return RouteResult(process);
				} else {
					return RouteResult(NULL, true);
				}
			} else {
				Process *process = findProcessWithStickySessionIdOrLowestBusyness(
					options.stickySessionId);
				if (process != NULL) {
					if (process->canBeRoutedTo()) {
						return RouteResult(process);
					} else {
						return RouteResult(NULL, false);
					}
				} else {
					return RouteResult(NULL, true);
				}
			}
		} else {
			Process *process = findProcessWithLowestBusyness(disablingProcesses);
			if (process->canBeRoutedTo()) {
				return RouteResult(process);
			} else {
				return RouteResult(NULL, true);
			}
		}
	}

	SessionPtr newSession(Process *process, unsigned long long now = 0) {
		bool wasTotallyBusy = process->isTotallyBusy();
		SessionPtr session = process->newSession(now);
		session->onInitiateFailure = _onSessionInitiateFailure;
		session->onClose   = _onSessionClose;
		if (process->enabled == Process::ENABLED) {
			enabledProcessBusynessLevels[process->getIndex()] = process->busyness();
			if (!wasTotallyBusy && process->isTotallyBusy()) {
				nEnabledProcessesTotallyBusy++;
			}
		}
		return session;
	}

	bool pushGetWaiter(const Options &newOptions, const GetCallback &callback,
		boost::container::vector<Callback> &postLockActions)
	{
		if (OXT_LIKELY(!testOverflowRequestQueue()
			&& (newOptions.maxRequestQueueSize == 0
			    || getWaitlist.size() < newOptions.maxRequestQueueSize)))
		{
			getWaitlist.push_back(GetWaiter(
				newOptions.copyAndPersist().detachFromUnionStationTransaction(),
				callback));
			return true;
		} else {
			postLockActions.push_back(boost::bind(GetCallback::call,
				callback, SessionPtr(), boost::make_shared<RequestQueueFullException>(newOptions.maxRequestQueueSize)));

			HookScriptOptions hsOptions;
			if (prepareHookScriptOptions(hsOptions, "queue_full_error")) {
				// TODO <Feb 17, 2015] DK> should probably rate limit this, since we are already at heavy load
				postLockActions.push_back(boost::bind(runHookScripts, hsOptions));
			}

			return false;
		}
	}

	template<typename Lock>
	void assignSessionsToGetWaitersQuickly(Lock &lock) {
		if (getWaitlist.empty()) {
			verifyInvariants();
			lock.unlock();
			return;
		}

		SmallVector<GetAction, 8> actions;
		unsigned int i = 0;
		bool done = false;

		actions.reserve(getWaitlist.size());

		while (!done && i < getWaitlist.size()) {
			const GetWaiter &waiter = getWaitlist[i];
			RouteResult result = route(waiter.options);
			if (result.process != NULL) {
				GetAction action;
				action.callback = waiter.callback;
				action.session  = newSession(result.process);
				getWaitlist.erase(getWaitlist.begin() + i);
				actions.push_back(action);
			} else {
				done = result.finished;
				if (!result.finished) {
					i++;
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

	void assignSessionsToGetWaiters(boost::container::vector<Callback> &postLockActions) {
		unsigned int i = 0;
		bool done = false;

		while (!done && i < getWaitlist.size()) {
			const GetWaiter &waiter = getWaitlist[i];
			RouteResult result = route(waiter.options);
			if (result.process != NULL) {
				postLockActions.push_back(boost::bind(
					GetCallback::call,
					waiter.callback,
					newSession(result.process),
					ExceptionPtr()));
				getWaitlist.erase(getWaitlist.begin() + i);
			} else {
				done = result.finished;
				if (!result.finished) {
					i++;
				}
			}
		}
	}

	bool shutdownCanFinish() const {
		LifeStatus lifeStatus = (LifeStatus) this->lifeStatus.load(boost::memory_order_relaxed);
		return lifeStatus == SHUTTING_DOWN
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
	void finishShutdown(boost::container::vector<Callback> &postLockActions) {
		TRACE_POINT();
		#ifndef NDEBUG
			LifeStatus lifeStatus = (LifeStatus) this->lifeStatus.load(boost::memory_order_relaxed);
			P_ASSERT_EQ(lifeStatus, SHUTTING_DOWN);
		#endif
		P_DEBUG("Finishing shutdown of group " << info.name);
		if (shutdownCallback) {
			postLockActions.push_back(shutdownCallback);
			shutdownCallback = Callback();
		}
		postLockActions.push_back(boost::bind(interruptAndJoinAllThreads,
			shared_from_this()));
		this->lifeStatus.store(SHUT_DOWN, boost::memory_order_release);
		selfPointer.reset();
	}

public:
	Options options;
	/** A UUID that's generated on Group initialization, and changes every time
	 * the Group receives a restart command. Allows Union Station to track app
	 * restarts. This information is public.
	 */
	string uuid;

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
	 * `nEnabledProcessesTotallyBusy` counts the number of enabled processes for which
	 * `isTotallyBusy()` is true.
	 *
	 * Invariants:
	 *    enabledCount >= 0
	 *    disablingCount >= 0
	 *    disabledCount >= 0
	 *    enabledProcesses.size() == enabledCount
	 *    disablingProcesses.size() == disabingCount
	 *    disabledProcesses.size() == disabledCount
	 *    nEnabledProcessesTotallyBusy <= enabledCount
     *
	 *    if (enabledCount == 0):
	 *       processesBeingSpawned > 0 || restarting() || poolAtFullCapacity()
	 *    if (enabledCount == 0) and (disablingCount > 0):
	 *       processesBeingSpawned > 0
	 *    if !m_spawning:
	 *       (enabledCount > 0) || (disablingCount == 0)
	 *
	 *    for all process in enabledProcesses:
	 *       process.enabled == Process::ENABLED
	 *       process.isAlive()
	 *       process.oobwStatus == Process::OOBW_NOT_ACTIVE || process.oobwStatus == Process::OOBW_REQUESTED
	 *    for all processes in disablingProcesses:
	 *       process.enabled == Process::DISABLING
	 *       process.isAlive()
	 *       process.oobwStatus == Process::OOBW_NOT_ACTIVE || process.oobwStatus == Process::OOBW_IN_PROGRESS
	 *    for all process in disabledProcesses:
	 *       process.enabled == Process::DISABLED
	 *       process.isAlive()
	 *       process.oobwStatus == Process::OOBW_NOT_ACTIVE || process.oobwStatus == Process::OOBW_IN_PROGRESS
	 */
	int enabledCount;
	int disablingCount;
	int disabledCount;
	int nEnabledProcessesTotallyBusy;
	ProcessList enabledProcesses;
	ProcessList disablingProcesses;
	ProcessList disabledProcesses;

	/**
	 * When a process is detached, it is stored here until we've confirmed
	 * that the OS process has exited.
	 *
	 * for all process in detachedProcesses:
	 *    process.enabled == Process::DETACHED
	 */
	ProcessList detachedProcesses;

	/**
	 * A cache of the processes' busyness. It's in a compact structure
	 * so that `findProcessWithLowestBusyness()` can work very quickly
	 * when there are a large number of processes.
	 */
	boost::container::vector<int> enabledProcessBusynessLevels;

	/**
	 * get() requests for this group that cannot be immediately satisfied are
	 * put on this wait list, which must be processed as soon as the necessary
	 * resources have become free.
	 *
	 * ### Invariant 1 (safety)
	 *
	 * If requests are queued in the getWaitlist, then that's because there are
	 * no processes that can serve them.
	 *
	 *    if getWaitlist is non-empty:
	 *       enabledProcesses.empty() || (no request in getWaitlist is routeable)
	 *
	 * Here, "routeable" is defined as `route(options).process != NULL`.
	 *
	 * ### Invariant 2 (progress)
	 *
	 * The only reason why there are no enabled processes, while at the same time we're
	 * not spawning or waiting for pool capacity, is because there is nothing to do.
	 *
	 *    if enabledProcesses.empty() && !m_spawning && !restarting() && !poolAtFullCapacity():
	 *       getWaitlist is empty
	 *
	 * Equivalently:
	 * If requests are queued in the getWaitlist, then either we have processes that can process
	 * them (some time in the future), or we're actively trying to spawn processes, unless we're
	 * unable to do that because of resource limits.
	 *
	 *    if getWaitlist is non-empty:
	 *       !enabledProcesses.empty() || m_spawning || restarting() || poolAtFullCapacity()
	 */
	deque<GetWaiter> getWaitlist;
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
	SpawningKit::SpawnerPtr spawner;


	/********************************************
	 * Constructors and destructors
	 ********************************************/

	Group(Pool *pool, const Options &options);
	~Group();
	bool initialize();

	/**
	 * Must be called before destroying a Group. You can optionally provide a
	 * callback so that you are notified when shutdown has finished.
	 *
	 * The caller is responsible for migrating waiters on the getWaitlist.
	 *
	 * One of the post lock actions can potentially perform a long-running
	 * operation, so running them in a thread is advised.
	 */
	void shutdown(const Callback &callback,
		boost::container::vector<Callback> &postLockActions)
	{
		assert(isAlive());
		assert(getWaitlist.empty());

		P_DEBUG("Begin shutting down group " << info.name);
		shutdownCallback = callback;
		detachAll(postLockActions);
		startCheckingDetachedProcesses(true);
		interruptableThreads.interrupt_all();
		postLockActions.push_back(boost::bind(doCleanupSpawner, spawner));
		spawner.reset();
		selfPointer = shared_from_this();
		assert(disableWaitlist.empty());
		lifeStatus.store(SHUTTING_DOWN, boost::memory_order_release);
	}


	/********************************************
	 * Life time and back-reference methods
	 ********************************************/

	/**
	 * Thread-safe.
	 * @pre getLifeState() != SHUT_DOWN
	 * @post result != NULL
	 */
	OXT_FORCE_INLINE Pool *getPool() const;

	// Thread-safe.
	bool isAlive() const {
		return getLifeStatus() == ALIVE;
	}

	// Thread-safe.
	OXT_FORCE_INLINE
	LifeStatus getLifeStatus() const {
		return (LifeStatus) lifeStatus.load(boost::memory_order_acquire);
	}


	/********************************************
	 * Core methods
	 ********************************************/

	SessionPtr get(const Options &newOptions, const GetCallback &callback,
		boost::container::vector<Callback> &postLockActions)
	{
		assert(isAlive());

		if (OXT_LIKELY(!restarting())) {
			if (OXT_UNLIKELY(needsRestart(newOptions))) {
				restart(newOptions);
			} else {
				mergeOptions(newOptions);
			}
			if (OXT_UNLIKELY(!newOptions.noop && shouldSpawnForGetAction())) {
				// If we're trying to spawn the first process for this group, and
				// spawning failed because the pool is at full capacity, then we
				// try to kill some random idle process in the pool and try again.
				if (spawn() == SR_ERR_POOL_AT_FULL_CAPACITY && enabledCount == 0) {
					P_INFO("Unable to spawn the the sole process for group " << info.name <<
						" because the max pool size has been reached. Trying " <<
						"to shutdown another idle process to free capacity...");
					if (poolForceFreeCapacity(this, postLockActions) != NULL) {
						SpawnResult result = spawn();
						assert(result == SR_OK);
						(void) result;
					} else {
						P_INFO("There are no processes right now that are eligible "
							"for shutdown. Will try again later.");
					}
				}
			}
		}

		if (OXT_UNLIKELY(newOptions.noop)) {
			return nullProcess->createSessionObject((Socket *) NULL);
		}

		if (OXT_UNLIKELY(enabledCount == 0)) {
			/* We don't have any processes yet, but they're on the way.
			 *
			 * We have some choices here. If there are disabling processes
			 * then we generally want to use them, except:
			 * - When non-rolling restarting because those disabling processes
			 *   are from the old version.
			 * - When all disabling processes are totally busy.
			 *
			 * Whenever a disabling process cannot be used, call the callback
			 * after a process has been spawned or has failed to spawn, or
			 * when a disabling process becomes available.
			 */
			assert(m_spawning || restarting() || poolAtFullCapacity());

			if (disablingCount > 0 && !restarting()) {
				Process *process = findProcessWithLowestBusyness(
					disablingProcesses);
				assert(process != NULL);
				if (!process->isTotallyBusy()) {
					return newSession(process, newOptions.currentTime);
				}
			}

			if (pushGetWaiter(newOptions, callback, postLockActions)) {
				P_DEBUG("No session checked out yet: group is spawning or restarting");
			}
			return SessionPtr();
		} else {
			RouteResult result = route(newOptions);
			if (result.process == NULL) {
				/* Looks like all processes are totally busy.
				 * Wait until a new one has been spawned or until
				 * resources have become free.
				 */
				if (pushGetWaiter(newOptions, callback, postLockActions)) {
					P_DEBUG("No session checked out yet: all processes are at full capacity");
				}
				return SessionPtr();
			} else {
				P_DEBUG("Session checked out from process " << result.process->inspect());
				return newSession(result.process, newOptions.currentTime);
			}
		}
	}


	/********************************************
	 * State mutation methods
	 ********************************************/

	// Thread-safe, but only call outside the pool lock!
	void requestOOBW(const ProcessPtr &process);

	/**
	 * Attempts to increase the number of processes by one, while respecting the
	 * resource limits. That is, this method will ensure that there are at least
	 * `minProcesses` processes, but no more than `maxProcesses` processes, and no
	 * more than `pool->max` processes in the entire pool.
	 */
	SpawnResult spawn() {
		assert(isAlive());
		if (m_spawning) {
			return SR_IN_PROGRESS;
		} else if (restarting()) {
			return SR_ERR_RESTARTING;
		} else if (processUpperLimitsReached()) {
			return SR_ERR_GROUP_UPPER_LIMITS_REACHED;
		} else if (poolAtFullCapacity()) {
			return SR_ERR_POOL_AT_FULL_CAPACITY;
		} else {
			P_DEBUG("Requested spawning of new process for group " << info.name);
			interruptableThreads.create_thread(
				boost::bind(&Group::spawnThreadMain,
					this, shared_from_this(), spawner,
					options.copyAndPersist().clearPerRequestFields(),
					restartsInitiated),
				"Group process spawner: " + info.name,
				POOL_HELPER_THREAD_STACK_SIZE);
			m_spawning = true;
			processesBeingSpawned++;
			return SR_OK;
		}
	}

	void cleanupSpawner(boost::container::vector<Callback> &postLockActions) {
		assert(isAlive());
		postLockActions.push_back(boost::bind(doCleanupSpawner, spawner));
	}

	void restart(const Options &options, RestartMethod method = RM_DEFAULT);


	/********************************************
	 * Queries
	 ********************************************/

	 Context *getContext() const {
		return info.context;
	}

	const BasicGroupInfo &getInfo() {
		return info;
	}

	StaticString getName() const {
		return info.name;
	}

	StaticString getSecret() const {
		return info.getSecret();
	}

	unsigned int getProcessCount() const {
		return enabledCount + disablingCount + disabledCount;
	}

	/**
	 * Returns the number of processes in this group that should be part of the
	 * ApplicationPool process limits calculations.
	 */
	unsigned int capacityUsed() const {
		return enabledCount + disablingCount + disabledCount + processesBeingSpawned;
	}

	/**
	 * Returns whether the lower bound of the group-specific process limits
	 * have been satisfied. Note that even if the result is false, the pool limits
	 * may not allow spawning, so you should check `pool->atFullCapacity()` too.
	 */
	bool processLowerLimitsSatisfied() const {
		return capacityUsed() >= options.minProcesses;
	}

	/**
	 * Returns whether the upper bound of the group-specific process limits have
	 * been reached, or surpassed. Does not check whether pool limits have been
	 * reached. Use `pool->atFullCapacity()` to check for that.
	 */
	bool processUpperLimitsReached() const {
		return options.maxProcesses != 0 && capacityUsed() >= options.maxProcesses;
	}

	/**
	 * Returns whether all enabled processes are totally busy. If so, another
	 * process should be spawned, if allowed by the process limits.
	 * Returns false if there are no enabled processes.
	 */
	bool allEnabledProcessesAreTotallyBusy() const {
		return nEnabledProcessesTotallyBusy == enabledCount;
	}

	/**
	 * Checks whether this group is waiting for capacity on the pool to
	 * become available before it can continue processing requests.
	 */
	bool isWaitingForCapacity() const {
		return enabledProcesses.empty()
			&& processesBeingSpawned == 0
			&& !m_restarting
			&& !getWaitlist.empty();
	}

	bool garbageCollectable(unsigned long long now = 0) const {
		/* if (now == 0) {
			now = SystemTime::getUsec();
		}
		return busyness() == 0
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

	/**
	 * Whether a new process is allowed to be spawned for this group,
	 * i.e. whether the upper processes limits have not been reached.
	 */
	bool allowSpawn() const {
		return isAlive()
			&& !processUpperLimitsReached()
			&& !poolAtFullCapacity();
	}

	bool needsRestart(const Options &options) {
		if (m_restarting) {
			return false;
		} else {
			time_t now;
			struct stat buf;

			if (options.currentTime != 0) {
				now = options.currentTime / 1000000;
			} else {
				now = SystemTime::get();
			}

			if (lastRestartFileCheckTime == 0) {
				// First time we call needsRestart() for this group.
				if (syscalls::stat(restartFile.c_str(), &buf) == 0) {
					lastRestartFileMtime = buf.st_mtime;
				} else {
					lastRestartFileMtime = 0;
				}
				lastRestartFileCheckTime = now;
				return false;

			} else if (lastRestartFileCheckTime <= now - (time_t) options.statThrottleRate) {
				// Not first time we call needsRestart() for this group.
				// Stat throttle time has passed.
				bool restart;

				lastRestartFileCheckTime = now;

				if (lastRestartFileMtime > 0) {
					// restart.txt existed before
					if (syscalls::stat(restartFile.c_str(), &buf) == -1) {
						// restart.txt no longer exists
						lastRestartFileMtime = buf.st_mtime;
						restart = false;
					} else if (buf.st_mtime != lastRestartFileMtime) {
						// restart.txt's mtime has changed
						lastRestartFileMtime = buf.st_mtime;
						restart = true;
					} else {
						restart = false;
					}
				} else {
					// restart.txt didn't exist before
					if (syscalls::stat(restartFile.c_str(), &buf) == 0) {
						// restart.txt now exists
						lastRestartFileMtime = buf.st_mtime;
						restart = true;
					} else {
						// restart.txt still doesn't exist
						lastRestartFileMtime = 0;
						restart = false;
					}
				}

				if (!restart) {
					alwaysRestartFileExists = restart =
						syscalls::stat(alwaysRestartFile.c_str(), &buf) == 0;
				}

				return restart;

			} else {
				// Not first time we call needsRestart() for this group.
				// Still within stat throttling window.
				if (alwaysRestartFileExists) {
					// always_restart.txt existed before
					alwaysRestartFileExists = syscalls::stat(
						alwaysRestartFile.c_str(), &buf) == 0;
					return alwaysRestartFileExists;
				} else {
					// Don't check until stat throttling window is over
					return false;
				}
			}
		}
	}

	bool spawning() const {
		return m_spawning;
	}

	bool restarting() const {
		return m_restarting;
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_GROUP_H_ */
