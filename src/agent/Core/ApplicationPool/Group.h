/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#include <deque>
#include <boost/thread.hpp>
#include <boost/bind/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/container/vector.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/atomic.hpp>
#include <oxt/macros.hpp>
#include <oxt/thread.hpp>
#include <oxt/dynamic_thread_group.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <MemoryKit/palloc.h>
#include <WrapperRegistry/Registry.h>
#include <Hooks.h>
#include <Utils.h>
#include <Core/ApplicationPool/Common.h>
#include <Core/ApplicationPool/Context.h>
#include <Core/ApplicationPool/BasicGroupInfo.h>
#include <Core/ApplicationPool/Process.h>
#include <Core/ApplicationPool/Options.h>
#include <Core/SpawningKit/Factory.h>
#include <Core/SpawningKit/Result.h>
#include <Core/SpawningKit/UserSwitchingRules.h>
#include <Shared/ApplicationPoolApiKey.h>

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
		/** The Process to route the request to, or nullptr if no process can be routed to. */
		Process * const process;
		/**
		 * If `process` is nullptr, then `finished` indicates whether another `Group::route()`
		 * call on a different request *could* succeed, meaning that the caller should continue
		 * calling `Group::route()` if there are more queued requests that need to be processed.
		 *
		 * Usually `finished` is false because all processes are totally busy. But in some cases,
		 * for example when using sticky sessions, it could be true because other requests can
		 * potentially be routed to other processes.
		 */
		const bool finished;

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

	/****** Initialization and shutdown ******/

	static ApiKey generateApiKey(const Pool *pool);
	static string generateUuid(const Pool *pool);

	bool shutdownCanFinish() const;
	void finishShutdown(boost::container::vector<Callback> &postLockActions);

	/****** Session management ******/

	RouteResult route(const Options &options) const;
	SessionPtr newSession(Process *process, unsigned long long now = 0);
	static void _onSessionInitiateFailure(Session *session);
	static void _onSessionClose(Session *session);
	OXT_FORCE_INLINE void onSessionInitiateFailure(Process *process, Session *session);
	OXT_FORCE_INLINE void onSessionClose(Process *process, Session *session);

	/****** Spawning and restarting ******/

	void spawnThreadMain(GroupPtr self, SpawningKit::SpawnerPtr spawner, Options options,
		unsigned int restartsInitiated);
	void spawnThreadRealMain(const SpawningKit::SpawnerPtr &spawner, const Options &options,
		unsigned int restartsInitiated);
	void finalizeRestart(GroupPtr self, Options oldOptions, Options newOptions,
		RestartMethod method, SpawningKit::FactoryPtr spawningKitFactory,
		unsigned int restartsInitiated, boost::container::vector<Callback> postLockActions);

	/****** Process list management ******/

	Process *findProcessWithStickySessionId(unsigned int id) const;
	Process *findProcessWithStickySessionIdOrLowestBusyness(unsigned int id) const;
	Process *findProcessWithLowestBusyness(const ProcessList &processes) const;
	Process *findEnabledProcessWithLowestBusyness() const;
	Process *findBestProcessPreferringStickySessionId(unsigned int id) const;
	Process *findBestProcess(const ProcessList &processes) const;
	Process *findBestEnabledProcess() const;
	bool useNewRouting() const;

	void addProcessToList(const ProcessPtr &process, ProcessList &destination);
	void removeProcessFromList(const ProcessPtr &process, ProcessList &source);
	void removeFromDisableWaitlist(const ProcessPtr &p, DisableResult result,
		boost::container::vector<Callback> &postLockActions);
	void clearDisableWaitlist(DisableResult result,
		boost::container::vector<Callback> &postLockActions);
	void enableAllDisablingProcesses(boost::container::vector<Callback> &postLockActions);

	void startCheckingDetachedProcesses(bool immediately);
	void detachedProcessesCheckerMain(GroupPtr self);

	/****** Out-of-band work ******/

	bool oobwAllowed() const;
	bool shouldInitiateOobw(Process *process) const;
	void maybeInitiateOobw(Process *process);
	void lockAndMaybeInitiateOobw(const ProcessPtr &process, DisableResult result, GroupPtr self);
	void initiateOobw(const ProcessPtr &process);
	void spawnThreadOOBWRequest(GroupPtr self, ProcessPtr process);
	void initiateNextOobwRequest();

	/****** Internal utilities ******/

	static void runAllActions(const boost::container::vector<Callback> &actions);
	static void interruptAndJoinAllThreads(GroupPtr self);
	static void doCleanupSpawner(SpawningKit::SpawnerPtr spawner);

	void resetOptions(const Options &newOptions, Options *destination = NULL);
	void mergeOptions(const Options &other);

	bool prepareHookScriptOptions(HookScriptOptions &hsOptions, const char *name);
	void runAttachHooks(const ProcessPtr process) const;
	void runDetachHooks(const ProcessPtr process) const;
	void setupAttachOrDetachHook(const ProcessPtr process, HookScriptOptions &options) const;

	unsigned int generateStickySessionId();
	ProcessPtr createNullProcessObject();
	ProcessPtr createProcessObject(const SpawningKit::Spawner &spawner, const SpawningKit::Result &spawnResult);
	bool poolAtFullCapacity() const;
	ProcessPtr poolForceFreeCapacity(const Group *exclude, boost::container::vector<Callback> &postLockActions);
	void wakeUpGarbageCollector();
	bool anotherGroupIsWaitingForCapacity() const;
	Group *findOtherGroupWaitingForCapacity() const;
	bool pushGetWaiter(const Options &newOptions, const GetCallback &callback,
		boost::container::vector<Callback> &postLockActions);
	template<typename Lock> void assignSessionsToGetWaitersQuickly(Lock &lock);
	void assignSessionsToGetWaiters(boost::container::vector<Callback> &postLockActions);
	bool testOverflowRequestQueue() const;
	void callAbortLongRunningConnectionsCallback(const ProcessPtr &process);

	/****** Correctness verification ******/

	bool selfCheckingEnabled() const;
	void verifyInvariants() const;
	void verifyExpensiveInvariants() const;
	#ifndef NDEBUG
		bool verifyNoRequestsOnGetWaitlistAreRoutable() const;
	#endif

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


	/****** Initialization and shutdown ******/

	Group(Pool *pool, const Options &options);
	~Group();
	bool initialize();
	void shutdown(const Callback &callback,
		boost::container::vector<Callback> &postLockActions);

	/****** Life time, basic info, backreferences and related objects ******/

	bool isAlive() const;
	OXT_FORCE_INLINE LifeStatus getLifeStatus() const;

	StaticString getName() const;
	const BasicGroupInfo &getInfo();
	const ApiKey &getApiKey() const;

	OXT_FORCE_INLINE Pool *getPool() const;
	Context *getContext() const;
	psg_pool_t *getPallocPool() const;
	const ResourceLocator &getResourceLocator() const;
	const WrapperRegistry::Registry &getWrapperRegistry() const;

	/****** Session management ******/

	SessionPtr get(const Options &newOptions, const GetCallback &callback,
		boost::container::vector<Callback> &postLockActions);

	/****** Spawning and restarting ******/

	void restart(const Options &options, RestartMethod method = RM_DEFAULT);
	bool restarting() const;
	bool needsRestart(const Options &options);

	SpawnResult spawn();
	bool spawning() const;
	bool shouldSpawn() const;
	bool shouldSpawnForGetAction() const;
	bool allowSpawn() const;

	/****** Process list management ******/

	AttachResult attach(const ProcessPtr &process,
		boost::container::vector<Callback> &postLockActions);
	void detach(const ProcessPtr &process,
		boost::container::vector<Callback> &postLockActions);
	void detachAll(boost::container::vector<Callback> &postLockActions);

	void enable(const ProcessPtr &process,
		boost::container::vector<Callback> &postLockActions);
	DisableResult disable(const ProcessPtr &process, const DisableCallback &callback);

	/****** State inspection ******/

	unsigned int getProcessCount() const;
	bool processLowerLimitsSatisfied() const;
	bool processUpperLimitsReached() const;
	bool allEnabledProcessesAreTotallyBusy() const;

	unsigned int capacityUsed() const;
	bool isWaitingForCapacity() const;
	bool garbageCollectable(unsigned long long now = 0) const;

	void inspectXml(std::ostream &stream, bool includeSecrets = true) const;
	void inspectPropertiesInAdminPanelFormat(Json::Value &result) const;
	void inspectConfigInAdminPanelFormat(Json::Value &result) const;

	/****** Out-of-band work ******/

	void requestOOBW(const ProcessPtr &process);

	/****** Miscellaneous ******/

	void cleanupSpawner(boost::container::vector<Callback> &postLockActions);
	bool authorizeByUid(uid_t uid) const;
	bool authorizeByApiKey(const ApiKey &key) const;
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_GROUP_H_ */
