/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2014 Phusion
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
#include <algorithm>
#include <utility>
#include <sstream>
#include <iomanip>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <oxt/dynamic_thread_group.hpp>
#include <oxt/backtrace.hpp>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Group.h>
#include <ApplicationPool2/SuperGroup.h>
#include <ApplicationPool2/Session.h>
#include <ApplicationPool2/SpawnerFactory.h>
#include <ApplicationPool2/Options.h>
#include <Logging.h>
#include <Exceptions.h>
#include <Hooks.h>
#include <Utils/Lock.h>
#include <Utils/AnsiColorConstants.h>
#include <Utils/SystemTime.h>
#include <Utils/MessagePassing.h>
#include <Utils/VariantMap.h>
#include <Utils/ProcessMetricsCollector.h>
#include <Utils/SystemMetricsCollector.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class Pool: public boost::enable_shared_from_this<Pool> {
public:
	struct InspectOptions {
		bool colorize;
		bool verbose;

		InspectOptions()
			: colorize(false),
			  verbose(false)
			{ }

		InspectOptions(const VariantMap &options)
			: colorize(options.getBool("colorize", false, false)),
			  verbose(options.getBool("verbose", false, false))
			{ }
	};

// Actually private, but marked public so that unit tests can access the fields.
public:
	friend class SuperGroup;
	friend class Group;

	struct DebugSupport {
		/** Mailbox for the unit tests to receive messages on. */
		MessageBoxPtr debugger;
		/** Mailbox for the ApplicationPool code to receive messages on. */
		MessageBoxPtr messages;

		// Choose aspects to debug.
		bool restarting;
		bool spawning;
		bool superGroup;
		bool oobw;
		bool testOverflowRequestQueue;
		bool detachedProcessesChecker;

		// The following fields may only be accessed by Pool.
		boost::mutex syncher;
		unsigned int spawnLoopIteration;

		DebugSupport() {
			debugger = boost::make_shared<MessageBox>();
			messages = boost::make_shared<MessageBox>();
			restarting = true;
			spawning   = true;
			superGroup = false;
			oobw       = false;
			detachedProcessesChecker = false;
			testOverflowRequestQueue = false;
			spawnLoopIteration = 0;
		}
	};

	typedef boost::shared_ptr<DebugSupport> DebugSupportPtr;

	SpawnerFactoryPtr spawnerFactory;
	SystemMetricsCollector systemMetricsCollector;
	SystemMetrics systemMetrics;

	mutable boost::mutex syncher;
	unsigned int max;
	unsigned long long maxIdleTime;

	boost::condition_variable garbageCollectionCond;

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

	enum LifeStatus {
		ALIVE,
		PREPARED_FOR_SHUTDOWN,
		SHUTTING_DOWN,
		SHUT_DOWN
	} lifeStatus;

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
	 *   free capacity will be used to spawn a process for this
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

	const VariantMap *agentsOptions;
	DebugSupportPtr debugSupport;

	static void runAllActions(const vector<Callback> &actions) {
		vector<Callback>::const_iterator it, end = actions.end();
		for (it = actions.begin(); it != end; it++) {
			(*it)();
		}
	}

	static void runAllActionsWithCopy(vector<Callback> actions) {
		runAllActions(actions);
	}

	static const char *maybeColorize(const InspectOptions &options, const char *color) {
		if (options.colorize) {
			return color;
		} else {
			return "";
		}
	}

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

	void fullVerifyInvariants() const {
		TRACE_POINT();
		verifyInvariants();
		UPDATE_TRACE_POINT();
		verifyExpensiveInvariants();
		UPDATE_TRACE_POINT();
		StringMap<SuperGroupPtr>::const_iterator sg_it, sg_end = superGroups.end();
		for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
			pair<StaticString, SuperGroupPtr> p = *sg_it;
			p.second->verifyInvariants();
			foreach (GroupPtr group, p.second->groups) {
				group->verifyInvariants();
				group->verifyExpensiveInvariants();
			}
		}
	}

	bool runHookScripts(const char *name,
		const boost::function<void (HookScriptOptions &)> &setup) const
	{
		if (agentsOptions != NULL) {
			string hookName = string("hook_") + name;
			string spec = agentsOptions->get(hookName, false);
			if (!spec.empty()) {
				HookScriptOptions options;
				options.agentsOptions = agentsOptions;
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

	static const char *maybePluralize(unsigned int count, const char *singular, const char *plural) {
		if (count == 1) {
			return singular;
		} else {
			return plural;
		}
	}

	const SpawnerConfigPtr &getSpawnerConfig() const {
		return spawnerFactory->getConfig();
	}

	const UnionStation::CorePtr &getUnionStationCore() const {
		return getSpawnerConfig()->unionStationCore;
	}

	const RandomGeneratorPtr &getRandomGenerator() const {
		return getSpawnerConfig()->randomGenerator;
	}

	ProcessPtr findOldestIdleProcess(const Group *exclude = NULL) const {
		ProcessPtr oldestIdleProcess;

		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); it != end; it++) {
			const SuperGroupPtr &superGroup = it->second;
			const vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
				if (group.get() == exclude) {
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
				const ProcessList &processes = group->enabledProcesses;
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
				SessionPtr session = superGroup->get(waiter.options, waiter.callback,
					postLockActions);
				if (session != NULL) {
					postLockActions.push_back(boost::bind(
						waiter.callback, session, ExceptionPtr()));
				}
				/* else: the callback has now been put in
				 *       the group's get wait list.
				 */
			} else if (!atFullCapacity(false)) {
				createSuperGroupAndAsyncGetFromIt(waiter.options, waiter.callback,
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
	static void assignExceptionToGetWaiters(Queue &getWaitlist,
		const ExceptionPtr &exception,
		vector<Callback> &postLockActions)
	{
		while (!getWaitlist.empty()) {
			postLockActions.push_back(boost::bind(
				getWaitlist.front().callback, SessionPtr(),
				exception));
			getWaitlist.pop_front();
		}
	}

	void possiblySpawnMoreProcessesForExistingGroups() {
		StringMap<SuperGroupPtr>::const_iterator sg_it, sg_end = superGroups.end();
		/* Looks for Groups that are waiting for capacity to become available,
		 * and spawn processes in those groups.
		 */
		for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
			pair<StaticString, SuperGroupPtr> p = *sg_it;
			foreach (GroupPtr group, p.second->groups) {
				if (group->isWaitingForCapacity()) {
					P_DEBUG("Group " << group->name << " is waiting for capacity");
					group->spawn();
					if (atFullCapacity(false)) {
						return;
					}
				}
			}
		}
		/* Now look for Groups that haven't maximized their allowed capacity
		 * yet, and spawn processes in those groups.
		 */
		for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
			pair<StaticString, SuperGroupPtr> p = *sg_it;
			foreach (GroupPtr group, p.second->groups) {
				if (group->shouldSpawn()) {
					P_DEBUG("Group " << group->name << " requests more processes to be spawned");
					group->spawn();
					if (atFullCapacity(false)) {
						return;
					}
				}
			}
		}
	}

	void migrateSuperGroupGetWaitlistToPool(const SuperGroupPtr &superGroup) {
		getWaitlist.reserve(getWaitlist.size() + superGroup->getWaitlist.size());
		while (!superGroup->getWaitlist.empty()) {
			getWaitlist.push_back(superGroup->getWaitlist.front());
			superGroup->getWaitlist.pop_front();
		}
	}

	/**
	 * Calls Group::detach() so be sure to fix up the invariants afterwards.
	 * See the comments for Group::detach() and the code for detachProcessUnlocked().
	 */
	ProcessPtr forceFreeCapacity(const Group *exclude,
		vector<Callback> &postLockActions)
	{
		ProcessPtr process = findOldestIdleProcess(exclude);
		if (process != NULL) {
			P_DEBUG("Forcefully detaching process " << process->inspect() <<
				" in order to free capacity in the pool");

			const GroupPtr group = process->getGroup();
			assert(group != NULL);
			assert(group->getWaitlist.empty());

			const SuperGroupPtr superGroup = group->getSuperGroup();
			assert(superGroup != NULL);

			group->detach(process, postLockActions);
		}
		return process;
	}

	/**
	 * Forcefully destroys and detaches the given SuperGroup. After detaching
	 * the SuperGroup may have a non-empty getWaitlist so be sure to do
	 * something with it.
	 *
	 * Also, one of the post lock actions can potentially perform a long-running
	 * operation, so running them in a thread is advised.
	 */
	void forceDetachSuperGroup(const SuperGroupPtr &superGroup,
		vector<Callback> &postLockActions,
		const SuperGroup::ShutdownCallback &callback)
	{
		const SuperGroupPtr sp = superGroup; // Prevent premature destruction.
		bool removed = superGroups.remove(superGroup->name);
		assert(removed);
		(void) removed; // Shut up compiler warning.
		superGroup->destroy(false, postLockActions, callback);
	}

	bool detachProcessUnlocked(const ProcessPtr &process, vector<Callback> &postLockActions) {
		if (OXT_LIKELY(process->isAlive())) {
			verifyInvariants();

			const GroupPtr group = process->getGroup();
			const SuperGroupPtr superGroup = group->getSuperGroup();
			assert(superGroup->state != SuperGroup::INITIALIZING);
			assert(superGroup->getWaitlist.empty());

			group->detach(process, postLockActions);
			// 'process' may now be a stale pointer so don't use it anymore.
			assignSessionsToGetWaiters(postLockActions);
			possiblySpawnMoreProcessesForExistingGroups();

			group->verifyInvariants();
			superGroup->verifyInvariants();
			verifyInvariants();
			verifyExpensiveInvariants();

			return true;
		} else {
			return false;
		}
	}

	void inspectProcessList(const InspectOptions &options, stringstream &result,
		const Group *group, const ProcessList &processes) const
	{
		ProcessList::const_iterator p_it;
		for (p_it = processes.begin(); p_it != processes.end(); p_it++) {
			const ProcessPtr &process = *p_it;
			char buf[128];
			char cpubuf[10];
			char membuf[10];

			snprintf(cpubuf, sizeof(cpubuf), "%d%%", (int) process->metrics.cpu);
			snprintf(membuf, sizeof(membuf), "%ldM",
				(unsigned long) (process->metrics.realMemory() / 1024));
			snprintf(buf, sizeof(buf),
					"  * PID: %-5lu   Sessions: %-2u      Processed: %-5u   Uptime: %s\n"
					"    CPU: %-5s   Memory  : %-5s   Last used: %s ago",
					(unsigned long) process->pid,
					process->sessions,
					process->processed,
					process->uptime().c_str(),
					cpubuf,
					membuf,
					distanceOfTimeInWords(process->lastUsed / 1000000).c_str());
			result << buf << endl;

			if (process->enabled == Process::DISABLING) {
				result << "    Disabling..." << endl;
			} else if (process->enabled == Process::DISABLED) {
				result << "    DISABLED" << endl;
			} else if (process->enabled == Process::DETACHED) {
				result << "    Shutting down..." << endl;
			}

			const Socket *socket;
			if (options.verbose && (socket = process->sockets->findSocketWithName("http")) != NULL) {
				result << "    URL     : http://" << replaceString(socket->address, "tcp://", "") << endl;
				result << "    Password: " << process->connectPassword << endl;
			}
		}
	}

	struct DetachSuperGroupWaitTicket {
		boost::mutex syncher;
		boost::condition_variable cond;
		SuperGroup::ShutdownResult result;
		bool done;

		DetachSuperGroupWaitTicket() {
			done = false;
		}
	};

	struct DisableWaitTicket {
		boost::mutex syncher;
		boost::condition_variable cond;
		DisableResult result;
		bool done;

		DisableWaitTicket() {
			done = false;
		}
	};

	static void syncDetachSuperGroupCallback(SuperGroup::ShutdownResult result,
		boost::shared_ptr<DetachSuperGroupWaitTicket> ticket)
	{
		LockGuard l(ticket->syncher);
		ticket->done = true;
		ticket->result = result;
		ticket->cond.notify_one();
	}

	static void waitDetachSuperGroupCallback(boost::shared_ptr<DetachSuperGroupWaitTicket> ticket) {
		ScopedLock l(ticket->syncher);
		while (!ticket->done) {
			ticket->cond.wait(l);
		}
	}

	static void syncDisableProcessCallback(const ProcessPtr &process, DisableResult result,
		boost::shared_ptr<DisableWaitTicket> ticket)
	{
		LockGuard l(ticket->syncher);
		ticket->done = true;
		ticket->result = result;
		ticket->cond.notify_one();
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

	struct GarbageCollectorState {
		unsigned long long now;
		unsigned long long nextGcRunTime;
		vector<Callback> actions;
	};

	static void garbageCollect(PoolPtr self) {
		TRACE_POINT();
		{
			ScopedLock lock(self->syncher);
			self->garbageCollectionCond.timed_wait(lock,
				posix_time::seconds(5));
		}
		while (!this_thread::interruption_requested()) {
			try {
				UPDATE_TRACE_POINT();
				unsigned long long sleepTime = self->realGarbageCollect();
				UPDATE_TRACE_POINT();
				ScopedLock lock(self->syncher);
				self->garbageCollectionCond.timed_wait(lock,
					posix_time::microseconds(sleepTime));
			} catch (const thread_interrupted &) {
				break;
			} catch (const tracable_exception &e) {
				P_WARN("ERROR: " << e.what() << "\n  Backtrace:\n" << e.backtrace());
			}
		}
	}

	void maybeUpdateNextGcRuntime(GarbageCollectorState &state, unsigned long candidate) {
		if (state.nextGcRunTime == 0 || candidate < state.nextGcRunTime) {
			state.nextGcRunTime = candidate;
		}
	}

	void maybeDetachIdleProcess(GarbageCollectorState &state, const GroupPtr &group,
		const ProcessPtr &process, ProcessList::iterator &p_it)
	{
		assert(maxIdleTime > 0);
		unsigned long long processGcTime =
			process->lastUsed + maxIdleTime;
		if (process->sessions == 0
		 && state.now >= processGcTime
		 && (unsigned long) group->getProcessCount() > group->options.minProcesses)
		{
			ProcessList::iterator prev = p_it;
			prev--;
			P_DEBUG("Garbage collect idle process: " << process->inspect() <<
				", group=" << group->name);
			group->detach(process, state.actions);
			p_it = prev;
		} else {
			maybeUpdateNextGcRuntime(state, processGcTime);
		}
	}

	void maybeCleanPreloader(GarbageCollectorState &state, const GroupPtr &group) {
		if (group->spawner->cleanable() && group->options.getMaxPreloaderIdleTime() != 0) {
			unsigned long long spawnerGcTime =
				group->spawner->lastUsed() +
				group->options.getMaxPreloaderIdleTime() * 1000000;
			if (state.now >= spawnerGcTime) {
				P_DEBUG("Garbage collect idle spawner: group=" << group->name);
				group->cleanupSpawner(state.actions);
			} else {
				maybeUpdateNextGcRuntime(state, spawnerGcTime);
			}
		}
	}

	unsigned long long realGarbageCollect() {
		TRACE_POINT();
		ScopedLock lock(syncher);
		SuperGroupMap::iterator it, end = superGroups.end();
		GarbageCollectorState state;
		state.now = SystemTime::getUsec();
		state.nextGcRunTime = 0;

		P_DEBUG("Garbage collection time...");
		verifyInvariants();

		// For all supergroups and groups...
		for (it = superGroups.begin(); it != end; it++) {
			SuperGroupPtr superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::iterator g_it, g_end = groups.end();

			superGroup->verifyInvariants();

			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				GroupPtr group = *g_it;

				if (maxIdleTime > 0) {
					ProcessList &processes = group->enabledProcesses;
					ProcessList::iterator p_it, p_end = processes.end();

					for (p_it = processes.begin(); p_it != p_end; p_it++) {
						ProcessPtr process = *p_it;
						// ...detach processes that have been idle for more than maxIdleTime.
						maybeDetachIdleProcess(state, group, process, p_it);
					}
				}

				group->verifyInvariants();

				// ...cleanup the spawner if it's been idle for more than preloaderIdleTime.
				maybeCleanPreloader(state, group);
			}

			superGroup->verifyInvariants();
		}

		verifyInvariants();
		lock.unlock();

		// Schedule next garbage collection run.
		unsigned long long sleepTime;
		if (state.nextGcRunTime == 0 || state.nextGcRunTime <= state.now) {
			if (maxIdleTime == 0) {
				sleepTime = 10 * 60 * 1000000;
			} else {
				sleepTime = maxIdleTime;
			}
		} else {
			sleepTime = state.nextGcRunTime - state.now;
		}
		P_DEBUG("Garbage collection done; next garbage collect in " <<
			std::fixed << std::setprecision(3) << (sleepTime / 1000000.0) << " sec");

		UPDATE_TRACE_POINT();
		runAllActions(state.actions);
		UPDATE_TRACE_POINT();
		state.actions.clear();
		return sleepTime;
	}

	struct UnionStationLogEntry {
		string groupName;
		const char *category;
		string key;
		string data;
	};

	static void collectAnalytics(PoolPtr self) {
		TRACE_POINT();
		syscalls::usleep(3000000);
		while (!this_thread::interruption_requested()) {
			try {
				UPDATE_TRACE_POINT();
				unsigned long long sleepTime = self->realCollectAnalytics();
				syscalls::usleep(sleepTime);
			} catch (const thread_interrupted &) {
				break;
			} catch (const tracable_exception &e) {
				P_WARN("ERROR: " << e.what() << "\n  Backtrace:\n" << e.backtrace());
			}
		}
	}

	static void collectPids(const ProcessList &processes, vector<pid_t> &pids) {
		foreach (const ProcessPtr &process, processes) {
			pids.push_back(process->pid);
		}
	}

	static void updateProcessMetrics(const ProcessList &processes,
		const ProcessMetricMap &allMetrics,
		vector<ProcessPtr> &processesToDetach)
	{
		foreach (const ProcessPtr &process, processes) {
			ProcessMetricMap::const_iterator metrics_it =
				allMetrics.find(process->pid);
			if (metrics_it != allMetrics.end()) {
				process->metrics = metrics_it->second;
			// If the process is missing from 'allMetrics' then either 'ps'
			// failed or the process really is gone. We double check by sending
			// it a signal.
			} else if (!process->dummy && !process->osProcessExists()) {
				P_WARN("Process " << process->inspect() << " no longer exists! "
					"Detaching it from the pool.");
				processesToDetach.push_back(process);
			}
		}
	}

	void prepareUnionStationProcessStateLogs(vector<UnionStationLogEntry> &logEntries,
		const GroupPtr &group) const
	{
		const UnionStation::CorePtr &unionStationCore = getUnionStationCore();
		if (group->options.analytics && unionStationCore != NULL) {
			logEntries.push_back(UnionStationLogEntry());
			UnionStationLogEntry &entry = logEntries.back();
			stringstream stream;

			stream << "Group: <group>";
			group->inspectXml(stream, false);
			stream << "</group>";

			entry.groupName = group->options.getAppGroupName();
			entry.category  = "processes";
			entry.key       = group->options.unionStationKey;
			entry.data      = stream.str();
		}
	}

	void prepareUnionStationSystemMetricsLogs(vector<UnionStationLogEntry> &logEntries,
		const GroupPtr &group) const
	{
		const UnionStation::CorePtr &unionStationCore = getUnionStationCore();
		if (group->options.analytics && unionStationCore != NULL) {
			logEntries.push_back(UnionStationLogEntry());
			UnionStationLogEntry &entry = logEntries.back();
			stringstream stream;

			stream << "System metrics: ";
			systemMetrics.toXml(stream);

			entry.groupName = group->options.getAppGroupName();
			entry.category  = "system_metrics";
			entry.key       = group->options.unionStationKey;
			entry.data      = stream.str();
		}
	}

	unsigned long long realCollectAnalytics() {
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		vector<pid_t> pids;
		unsigned int max;

		P_DEBUG("Analytics collection time...");
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
					collectPids(group->enabledProcesses, pids);
					collectPids(group->disablingProcesses, pids);
					collectPids(group->disabledProcesses, pids);
				}
			}
		}

		// Collect process metrics and system and store them in the
		// data structures. Later, we log them to Union Station.
		ProcessMetricMap processMetrics;
		try {
			UPDATE_TRACE_POINT();
			processMetrics = ProcessMetricsCollector().collect(pids);
		} catch (const ParseException &) {
			P_WARN("Unable to collect process metrics: cannot parse 'ps' output.");
			goto end;
		}
		try {
			UPDATE_TRACE_POINT();
			systemMetricsCollector.collect(systemMetrics);
		} catch (const RuntimeException &e) {
			P_WARN("Unable to collect system metrics: " << e.what());
			goto end;
		}

		{
			UPDATE_TRACE_POINT();
			vector<UnionStationLogEntry> logEntries;
			vector<ProcessPtr> processesToDetach;
			vector<Callback> actions;
			ScopedLock l(syncher);
			SuperGroupMap::iterator sg_it, sg_end = superGroups.end();

			UPDATE_TRACE_POINT();
			for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
				const SuperGroupPtr &superGroup = sg_it->second;
				vector<GroupPtr>::iterator g_it, g_end = superGroup->groups.end();

				for (g_it = superGroup->groups.begin(); g_it != g_end; g_it++) {
					const GroupPtr &group = *g_it;

					updateProcessMetrics(group->enabledProcesses, processMetrics, processesToDetach);
					updateProcessMetrics(group->disablingProcesses, processMetrics, processesToDetach);
					updateProcessMetrics(group->disabledProcesses, processMetrics, processesToDetach);
					prepareUnionStationProcessStateLogs(logEntries, group);
					prepareUnionStationSystemMetricsLogs(logEntries, group);
				}
			}

			UPDATE_TRACE_POINT();
			foreach (const ProcessPtr process, processesToDetach) {
				detachProcessUnlocked(process, actions);
			}
			UPDATE_TRACE_POINT();
			processesToDetach.clear();

			l.unlock();
			UPDATE_TRACE_POINT();
			if (!logEntries.empty()) {
				const UnionStation::CorePtr &unionStationCore = getUnionStationCore();
				while (!logEntries.empty()) {
					UnionStationLogEntry &entry = logEntries.back();
					UnionStation::TransactionPtr transaction =
						unionStationCore->newTransaction(
							entry.groupName,
							entry.category,
							entry.key);
					transaction->message(entry.data);
					logEntries.pop_back();
				}
			}

			UPDATE_TRACE_POINT();
			runAllActions(actions);
			UPDATE_TRACE_POINT();
			// Run destructors with updated trace point.
			actions.clear();
		}

		end:
		// Sleep for about 4 seconds, aligned to seconds boundary
		// for saving power on laptops.
		unsigned long long currentTime = SystemTime::getUsec();
		unsigned long long deadline =
			roundUp<unsigned long long>(currentTime, 1000000) + 4000000;
		P_DEBUG("Analytics collection done; next analytics collection in " <<
			std::fixed << std::setprecision(3) << ((deadline - currentTime) / 1000000.0) <<
			" sec");
		return deadline - currentTime;
	}

	SuperGroupPtr createSuperGroup(const Options &options) {
		SuperGroupPtr superGroup = boost::make_shared<SuperGroup>(shared_from_this(),
			options);
		superGroup->initialize();
		superGroups.set(options.getAppGroupName(), superGroup);
		garbageCollectionCond.notify_all();
		return superGroup;
	}

	SuperGroupPtr createSuperGroupAndAsyncGetFromIt(const Options &options,
		const GetCallback &callback, vector<Callback> &postLockActions)
	{
		SuperGroupPtr superGroup = createSuperGroup(options);
		SessionPtr session = superGroup->get(options, callback,
			postLockActions);
		/* Callback should now have been put on the wait list,
		 * unless something has changed and we forgot to update
		 * some code here...
		 */
		assert(session == NULL);
		return superGroup;
	}

	// Debugging helper function, implemented in .cpp file so that GDB can access it.
	const SuperGroupPtr getSuperGroup(const char *name);

public:
	Pool(const SpawnerFactoryPtr &spawnerFactory,
		const VariantMap *agentsOptions = NULL)
	{
		this->spawnerFactory = spawnerFactory;
		this->agentsOptions = agentsOptions;

		try {
			systemMetricsCollector.collect(systemMetrics);
		} catch (const RuntimeException &e) {
			P_WARN("Unable to collect system metrics: " << e.what());
		}

		lifeStatus  = ALIVE;
		max         = 6;
		maxIdleTime = 60 * 1000000;

		// The following code only serve to instantiate certain inline methods
		// so that they can be invoked from gdb.
		(void) SuperGroupPtr().get();
		(void) GroupPtr().get();
		(void) ProcessPtr().get();
		(void) SessionPtr().get();
	}

	~Pool() {
		if (lifeStatus != SHUT_DOWN) {
			P_BUG("You must call Pool::destroy() before actually destroying the Pool object!");
		}
	}

	/** Must be called right after construction. */
	void initialize() {
		LockGuard l(syncher);
		interruptableThreads.create_thread(
			boost::bind(collectAnalytics, shared_from_this()),
			"Pool analytics collector",
			POOL_HELPER_THREAD_STACK_SIZE
		);
		interruptableThreads.create_thread(
			boost::bind(garbageCollect, shared_from_this()),
			"Pool garbage collector",
			POOL_HELPER_THREAD_STACK_SIZE
		);
	}

	void initDebugging() {
		LockGuard l(syncher);
		debugSupport = boost::make_shared<DebugSupport>();
	}

	/** Should be called right after the HelperAgent has received
	 * the message to exit gracefully. This will tell processes to
	 * abort any long-running connections, e.g. WebSocket connections,
	 * because the RequestHandler has to wait until all connections are
	 * finished before proceeding with shutdown.
	 */
	void prepareForShutdown() {
		TRACE_POINT();
		ScopedLock lock(syncher);
		assert(lifeStatus == ALIVE);
		lifeStatus = PREPARED_FOR_SHUTDOWN;
		vector<ProcessPtr> processes = getProcesses(false);
		foreach (ProcessPtr process, processes) {
			if (process->abortLongRunningConnections()) {
				// Ensure that the process is not immediately respawned.
				process->getGroup()->options.minProcesses = 0;
			}
		}
	}

	/** Must be called right before destruction. */
	void destroy() {
		TRACE_POINT();
		ScopedLock lock(syncher);
		assert(lifeStatus == ALIVE || lifeStatus == PREPARED_FOR_SHUTDOWN);

		lifeStatus = SHUTTING_DOWN;

		while (!superGroups.empty()) {
			string name = superGroups.begin()->second->name;
			lock.unlock();
			detachSuperGroupByName(name);
			lock.lock();
		}

		UPDATE_TRACE_POINT();
		lock.unlock();
		interruptableThreads.interrupt_and_join_all();
		nonInterruptableThreads.join_all();
		lock.lock();

		lifeStatus = SHUT_DOWN;

		UPDATE_TRACE_POINT();
		verifyInvariants();
		verifyExpensiveInvariants();
	}

	// 'lockNow == false' may only be used during unit tests. Normally we
	// should never call the callback while holding the lock.
	void asyncGet(const Options &options, const GetCallback &callback, bool lockNow = true) {
		DynamicScopedLock lock(syncher, lockNow);

		assert(lifeStatus == ALIVE || lifeStatus == PREPARED_FOR_SHUTDOWN);
		verifyInvariants();
		P_TRACE(2, "asyncGet(appGroupName=" << options.getAppGroupName() << ")");
		vector<Callback> actions;

		SuperGroup *existingSuperGroup = findMatchingSuperGroup(options);
		if (OXT_LIKELY(existingSuperGroup != NULL)) {
			/* Best case: the app super group is already in the pool. Let's use it. */
			P_TRACE(2, "Found existing SuperGroup");
			existingSuperGroup->verifyInvariants();
			SessionPtr session = existingSuperGroup->get(options, callback, actions);
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
			P_DEBUG("Spawning new SuperGroup");
			SuperGroupPtr superGroup = createSuperGroupAndAsyncGetFromIt(options,
				callback, actions);
			superGroup->verifyInvariants();
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
					options.copyAndPersist().detachFromUnionStationTransaction(),
					callback));
			} else {
				/* Now that a process has been trashed we can create
				 * the missing SuperGroup.
				 */
				P_DEBUG("Creating new SuperGroup");
				SuperGroupPtr superGroup;
				superGroup = boost::make_shared<SuperGroup>(shared_from_this(), options);
				superGroup->initialize();
				superGroups.set(options.getAppGroupName(), superGroup);
				garbageCollectionCond.notify_all();
				SessionPtr session = superGroup->get(options, callback,
					actions);
				/* The SuperGroup is still initializing so the callback
				 * should now have been put on the wait list,
				 * unless something has changed and we forgot to update
				 * some code here...
				 */
				assert(session == NULL);
				freedProcess->getGroup()->verifyInvariants();
				superGroup->verifyInvariants();
			}

			assert(atFullCapacity(false));
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
		return get(options2, &ticket)->getGroup();
	}

	void setMax(unsigned int max) {
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
			vector<Callback> actions;
			assignSessionsToGetWaiters(actions);
			possiblySpawnMoreProcessesForExistingGroups();

			fullVerifyInvariants();
			l.unlock();
			runAllActions(actions);
		} else {
			fullVerifyInvariants();
		}
	}

	void setMaxIdleTime(unsigned long long value) {
		LockGuard l(syncher);
		maxIdleTime = value;
		garbageCollectionCond.notify_all();
	}

	unsigned int capacityUsed(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		SuperGroupMap::const_iterator it, end = superGroups.end();
		int result = 0;
		for (it = superGroups.begin(); it != end; it++) {
			const SuperGroupPtr &superGroup = it->second;
			result += superGroup->capacityUsed();
		}
		return result;
	}

	bool atFullCapacity(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		return capacityUsed(false) >= max;
	}

	vector<ProcessPtr> getProcesses(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		vector<ProcessPtr> result;
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); OXT_LIKELY(it != end); it++) {
			const SuperGroupPtr &superGroup = it->second;
			vector<GroupPtr> &groups = superGroup->groups;
			vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
			for (g_it = groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
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
			}
		}
		return result;
	}

	/**
	 * Returns the total number of processes in the pool, including all disabling and
	 * disabled processes, but excluding processes that are shutting down and excluding
	 * processes that are being spawned.
	 */
	unsigned int getProcessCount(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		unsigned int result = 0;
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); OXT_LIKELY(it != end); it++) {
			const SuperGroupPtr &superGroup = it->second;
			result += superGroup->getProcessCount();
		}
		return result;
	}

	unsigned int getSuperGroupCount() const {
		LockGuard l(syncher);
		return superGroups.size();
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
		vector<ProcessPtr> processes = getProcesses(lock);
		vector<ProcessPtr>::const_iterator it, end = processes.end();
		for (it = processes.begin(); it != end; it++) {
			const ProcessPtr &process = *it;
			if (process->gupid == gupid) {
				return process;
			}
		}
		return ProcessPtr();
	}

	ProcessPtr findProcessByPid(pid_t pid, bool lock = true) const {
		vector<ProcessPtr> processes = getProcesses(lock);
		vector<ProcessPtr>::const_iterator it, end = processes.end();
		for (it = processes.begin(); it != end; it++) {
			const ProcessPtr &process = *it;
			if (process->pid == pid) {
				return process;
			}
		}
		return ProcessPtr();
	}

	bool detachSuperGroupByName(const string &name) {
		TRACE_POINT();
		ScopedLock l(syncher);

		SuperGroupPtr superGroup = superGroups.get(name);
		if (OXT_LIKELY(superGroup != NULL)) {
			if (OXT_LIKELY(superGroups.get(superGroup->name) != NULL)) {
				UPDATE_TRACE_POINT();
				verifyInvariants();
				verifyExpensiveInvariants();

				vector<Callback> actions;
				boost::shared_ptr<DetachSuperGroupWaitTicket> ticket =
					boost::make_shared<DetachSuperGroupWaitTicket>();
				ExceptionPtr exception = copyException(
					GetAbortedException("The containing SuperGroup was detached."));

				forceDetachSuperGroup(superGroup, actions,
					boost::bind(syncDetachSuperGroupCallback, _1, ticket));
				assignExceptionToGetWaiters(superGroup->getWaitlist,
					exception, actions);
				#if 0
				/* If this SuperGroup had get waiters, either
				 * on itself or in one of its groups, then we must
				 * reprocess them immediately. Detaching such a
				 * SuperGroup is essentially the same as restarting it.
				 */
				migrateSuperGroupGetWaitlistToPool(superGroup);

				UPDATE_TRACE_POINT();
				assignSessionsToGetWaiters(actions);
				#endif
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
				return ticket->result == SuperGroup::SUCCESS;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}

	bool detachSuperGroupBySecret(const string &superGroupSecret) {
		ScopedLock l(syncher);
		SuperGroupPtr superGroup = findSuperGroupBySecret(superGroupSecret, false);
		if (superGroup != NULL) {
			string name = superGroup->name;
			superGroup.reset();
			l.unlock();
			return detachSuperGroupByName(name);
		} else {
			return false;
		}
	}

	bool detachProcess(const ProcessPtr &process) {
		ScopedLock l(syncher);
		vector<Callback> actions;
		bool result = detachProcessUnlocked(process, actions);
		fullVerifyInvariants();
		l.unlock();
		runAllActions(actions);
		return result;
	}

	bool detachProcess(pid_t pid) {
		ScopedLock l(syncher);
		ProcessPtr process = findProcessByPid(pid, false);
		if (process != NULL) {
			vector<Callback> actions;
			bool result = detachProcessUnlocked(process, actions);
			fullVerifyInvariants();
			l.unlock();
			runAllActions(actions);
			return result;
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
			fullVerifyInvariants();
			l.unlock();
			runAllActions(actions);
			return result;
		} else {
			return false;
		}
	}

	DisableResult disableProcess(const string &gupid) {
		ScopedLock l(syncher);
		ProcessPtr process = findProcessByGupid(gupid, false);
		if (process != NULL) {
			GroupPtr group = process->getGroup();
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

	bool restartGroupByName(const StaticString &name, RestartMethod method = RM_DEFAULT) {
		ScopedLock l(syncher);
		SuperGroupMap::iterator sg_it, sg_end = superGroups.end();

		for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
			const SuperGroupPtr &superGroup = sg_it->second;
			foreach (const GroupPtr &group, superGroup->groups) {
				if (name == group->name) {
					if (!group->restarting()) {
						group->restart(group->options, method);
					}
					return true;
				}
			}
		}

		return false;
	}

	unsigned int restartSuperGroupsByAppRoot(const StaticString &appRoot) {
		ScopedLock l(syncher);
		SuperGroupMap::iterator sg_it, sg_end = superGroups.end();
		unsigned int result = 0;

		for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
			const SuperGroupPtr &superGroup = sg_it->second;
			if (appRoot == superGroup->options.appRoot) {
				result++;
				superGroup->restart(superGroup->options);
			}
		}

		return result;
	}

	/**
	 * Checks whether at least one process is being spawned.
	 */
	bool isSpawning(bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		SuperGroupMap::const_iterator it, end = superGroups.end();
		for (it = superGroups.begin(); it != end; it++) {
			foreach (GroupPtr group, it->second->groups) {
				if (group->spawning()) {
					return true;
				}
			}
		}
		return false;
	}

	string inspect(const InspectOptions &options = InspectOptions(), bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		stringstream result;
		const char *headerColor = maybeColorize(options, ANSI_COLOR_YELLOW ANSI_COLOR_BLUE_BG ANSI_COLOR_BOLD);
		const char *resetColor  = maybeColorize(options, ANSI_COLOR_RESET);

		result << headerColor << "----------- General information -----------" << resetColor << endl;
		result << "Max pool size : " << max << endl;
		result << "Processes     : " << getProcessCount(false) << endl;
		result << "Requests in top-level queue : " << getWaitlist.size() << endl;
		if (options.verbose) {
			unsigned int i = 0;
			foreach (const GetWaiter &waiter, getWaitlist) {
				result << "  " << i << ": " << waiter.options.getAppGroupName() << endl;
				i++;
			}
		}
		result << endl;

		result << headerColor << "----------- Application groups -----------" << resetColor << endl;
		SuperGroupMap::const_iterator sg_it, sg_end = superGroups.end();
		for (sg_it = superGroups.begin(); sg_it != sg_end; sg_it++) {
			const SuperGroupPtr &superGroup = sg_it->second;
			const Group *group = superGroup->defaultGroup;
			ProcessList::const_iterator p_it;

			if (group != NULL) {
				result << group->name << ":" << endl;
				result << "  App root: " << group->options.appRoot << endl;
				if (group->restarting()) {
					result << "  (restarting...)" << endl;
				}
				if (group->spawning()) {
					if (group->processesBeingSpawned == 0) {
						result << "  (spawning...)" << endl;
					} else {
						result << "  (spawning " << group->processesBeingSpawned << " new " <<
							maybePluralize(group->processesBeingSpawned, "process", "processes") <<
							"...)" << endl;
					}
				}
				result << "  Requests in queue: " << group->getWaitlist.size() << endl;
				inspectProcessList(options, result, group, group->enabledProcesses);
				inspectProcessList(options, result, group, group->disablingProcesses);
				inspectProcessList(options, result, group, group->disabledProcesses);
				inspectProcessList(options, result, group, group->detachedProcesses);
				result << endl;
			}
		}
		return result.str();
	}

	string toXml(bool includeSecrets = true, bool lock = true) const {
		DynamicScopedLock l(syncher, lock);
		stringstream result;
		SuperGroupMap::const_iterator sg_it;
		vector<GroupPtr>::const_iterator g_it;
		ProcessList::const_iterator p_it;

		result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
		result << "<info version=\"3\">";

		result << "<passenger_version>" << PASSENGER_VERSION << "</passenger_version>";
		result << "<process_count>" << getProcessCount(false) << "</process_count>";
		result << "<max>" << max << "</max>";
		result << "<capacity_used>" << capacityUsed(false) << "</capacity_used>";
		result << "<get_wait_list_size>" << getWaitlist.size() << "</get_wait_list_size>";

		if (includeSecrets) {
			vector<GetWaiter>::const_iterator w_it, w_end = getWaitlist.end();

			result << "<get_wait_list>";
			for (w_it = getWaitlist.begin(); w_it != w_end; w_it++) {
				const GetWaiter &waiter = *w_it;
				result << "<item>";
				result << "<app_group_name>" << escapeForXml(waiter.options.getAppGroupName()) << "</app_group_name>";
				result << "</item>";
			}
			result << "</get_wait_list>";
		}

		result << "<supergroups>";
		for (sg_it = superGroups.begin(); sg_it != superGroups.end(); sg_it++) {
			const SuperGroupPtr &superGroup = sg_it->second;

			result << "<supergroup>";
			result << "<name>" << escapeForXml(superGroup->name) << "</name>";
			result << "<state>" << superGroup->getStateName() << "</state>";
			result << "<get_wait_list_size>" << superGroup->getWaitlist.size() << "</get_wait_list_size>";
			result << "<capacity_used>" << superGroup->capacityUsed() << "</capacity_used>";
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
			result << "</supergroup>";
		}
		result << "</supergroups>";

		result << "</info>";
		return result.str();
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_POOL_H_ */
