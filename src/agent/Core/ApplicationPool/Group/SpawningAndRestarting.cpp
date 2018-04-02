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
#include <Core/ApplicationPool/Group.h>

/*************************************************************************
 *
 * Session management functions for ApplicationPool2::Group
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


// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::spawnThreadMain(GroupPtr self, SpawningKit::SpawnerPtr spawner,
	Options options, unsigned int restartsInitiated)
{
	spawnThreadRealMain(spawner, options, restartsInitiated);
}

void
Group::spawnThreadRealMain(const SpawningKit::SpawnerPtr &spawner,
	const Options &options, unsigned int restartsInitiated)
{
	TRACE_POINT();
	boost::this_thread::disable_interruption di;
	boost::this_thread::disable_syscall_interruption dsi;

	Pool *pool = getPool();
	Pool::DebugSupportPtr debug = pool->debugSupport;

	bool done = false;
	while (!done) {
		bool shouldFail = false;
		if (debug != NULL && debug->spawning) {
			UPDATE_TRACE_POINT();
			boost::this_thread::restore_interruption ri(di);
			boost::this_thread::restore_syscall_interruption rsi(dsi);
			boost::this_thread::interruption_point();
			string iteration;
			{
				LockGuard g(debug->syncher);
				debug->spawnLoopIteration++;
				iteration = toString(debug->spawnLoopIteration);
			}
			P_DEBUG("Begin spawn loop iteration " << iteration);
			debug->debugger->send("Begin spawn loop iteration " +
				iteration);

			vector<string> cases;
			cases.push_back("Proceed with spawn loop iteration " + iteration);
			cases.push_back("Fail spawn loop iteration " + iteration);
			MessagePtr message = debug->messages->recvAny(cases);
			shouldFail = message->name == "Fail spawn loop iteration " + iteration;
		}

		ProcessPtr process;
		ExceptionPtr exception;
		try {
			UPDATE_TRACE_POINT();
			boost::this_thread::restore_interruption ri(di);
			boost::this_thread::restore_syscall_interruption rsi(dsi);
			if (shouldFail) {
				SpawningKit::Journey journey(SpawningKit::SPAWN_DIRECTLY, false);
				SpawningKit::Config config;
				journey.setStepErrored(SpawningKit::SPAWNING_KIT_PREPARATION, true);
				SpawningKit::SpawnException e(SpawningKit::INTERNAL_ERROR,
					journey, &config);
				e.setSummary("Simulated failure");
				throw e.finalize();
			} else {
				process = createProcessObject(*spawner, spawner->spawn(options));
			}
		} catch (const boost::thread_interrupted &) {
			break;
		} catch (SpawningKit::SpawnException &e) {
			processAndLogNewSpawnException(e, options, pool->getContext());
			exception = copyException(e);
		} catch (const tracable_exception &e) {
			exception = copyException(e);
			// Let other (unexpected) exceptions crash the program so
			// gdb can generate a backtrace.
		}

		UPDATE_TRACE_POINT();
		ScopeGuard guard(boost::bind(Process::forceTriggerShutdownAndCleanup, process));
		boost::unique_lock<boost::mutex> lock(pool->syncher);

		if (!isAlive()) {
			if (process != NULL) {
				P_DEBUG("Group is being shut down so dropping process " <<
					process->inspect() << " which we just spawned and exiting spawn loop");
			} else {
				P_DEBUG("The group is being shut down. A process failed "
					"to be spawned anyway, so ignoring this error and exiting "
					"spawn loop");
			}
			// We stop immediately because any previously assumed invariants
			// may have been violated.
			break;
		} else if (restartsInitiated != this->restartsInitiated) {
			if (process != NULL) {
				P_DEBUG("A restart was issued for the group, so dropping process " <<
					process->inspect() << " which we just spawned and exiting spawn loop");
			} else {
				P_DEBUG("A restart was issued for the group. A process failed "
					"to be spawned anyway, so ignoring this error and exiting "
					"spawn loop");
			}
			// We stop immediately because any previously assumed invariants
			// may have been violated.
			break;
		}

		verifyInvariants();
		assert(m_spawning);
		assert(processesBeingSpawned > 0);

		processesBeingSpawned--;
		assert(processesBeingSpawned == 0);

		UPDATE_TRACE_POINT();
		boost::container::vector<Callback> actions;
		if (process != NULL) {
			AttachResult result = attach(process, actions);
			if (result == AR_OK) {
				guard.clear();
				if (getWaitlist.empty()) {
					pool->assignSessionsToGetWaiters(actions);
				} else {
					assignSessionsToGetWaiters(actions);
				}
				P_DEBUG("New process count = " << enabledCount <<
					", remaining get waiters = " << getWaitlist.size());
			} else {
				done = true;
				P_DEBUG("Unable to attach spawned process " << process->inspect());
				if (result == AR_ANOTHER_GROUP_IS_WAITING_FOR_CAPACITY) {
					pool->possiblySpawnMoreProcessesForExistingGroups();
				}
			}
		} else {
			// TODO: sure this is the best thing? if there are
			// processes currently alive we should just use them.
			if (enabledCount == 0) {
				enableAllDisablingProcesses(actions);
			}
			Pool::assignExceptionToGetWaiters(getWaitlist, exception, actions);
			pool->assignSessionsToGetWaiters(actions);
			done = true;
		}

		done = done
			|| (processLowerLimitsSatisfied() && getWaitlist.empty())
			|| processUpperLimitsReached()
			|| pool->atFullCapacityUnlocked();
		m_spawning = !done;
		if (done) {
			P_DEBUG("Spawn loop done");
		} else {
			processesBeingSpawned++;
			P_DEBUG("Continue spawning");
		}

		UPDATE_TRACE_POINT();
		pool->fullVerifyInvariants();
		lock.unlock();
		UPDATE_TRACE_POINT();
		runAllActions(actions);
		UPDATE_TRACE_POINT();
	}

	if (debug != NULL && debug->spawning) {
		debug->debugger->send("Spawn loop done");
	}
}

// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::finalizeRestart(GroupPtr self,
	Options oldOptions,
	Options newOptions, RestartMethod method,
	SpawningKit::FactoryPtr spawningKitFactory,
	unsigned int restartsInitiated,
	boost::container::vector<Callback> postLockActions)
{
	TRACE_POINT();

	Pool::runAllActions(postLockActions);
	postLockActions.clear();

	boost::this_thread::disable_interruption di;
	boost::this_thread::disable_syscall_interruption dsi;

	// Create a new spawner.
	Options spawnerOptions = oldOptions;
	resetOptions(newOptions, &spawnerOptions);
	SpawningKit::SpawnerPtr newSpawner = spawningKitFactory->create(spawnerOptions);
	SpawningKit::SpawnerPtr oldSpawner;

	UPDATE_TRACE_POINT();
	Pool *pool = getPool();

	Pool::DebugSupportPtr debug = pool->debugSupport;
	if (debug != NULL && debug->restarting) {
		boost::this_thread::restore_interruption ri(di);
		boost::this_thread::restore_syscall_interruption rsi(dsi);
		boost::this_thread::interruption_point();
		debug->debugger->send("About to end restarting");
		debug->messages->recv("Finish restarting");
	}

	ScopedLock l(pool->syncher);
	if (!isAlive()) {
		P_DEBUG("Group " << getName() << " is shutting down, so aborting restart");
		return;
	}
	if (restartsInitiated != this->restartsInitiated) {
		// Before this restart could be finalized, another restart command was given.
		// The spawner we just created might be out of date now so we abort.
		P_DEBUG("Restart of group " << getName() << " aborted because a new restart was initiated concurrently");
		if (debug != NULL && debug->restarting) {
			debug->debugger->send("Restarting aborted");
		}
		return;
	}

	// Run some sanity checks.
	pool->fullVerifyInvariants();
	assert(m_restarting);
	UPDATE_TRACE_POINT();

	// Atomically swap the new spawner with the old one.
	resetOptions(newOptions);
	oldSpawner = spawner;
	spawner    = newSpawner;

	m_restarting = false;
	if (shouldSpawn()) {
		spawn();
	} else if (isWaitingForCapacity()) {
		P_INFO("Group " << getName() << " is waiting for capacity to become available. "
			"Trying to shutdown another idle process to free capacity...");
		if (pool->forceFreeCapacity(this, postLockActions) != NULL) {
			spawn();
		} else {
			P_INFO("There are no processes right now that are eligible "
				"for shutdown. Will try again later.");
		}
	}
	verifyInvariants();

	l.unlock();
	oldSpawner.reset();
	Pool::runAllActions(postLockActions);
	P_DEBUG("Restart of group " << getName() << " done");
	if (debug != NULL && debug->restarting) {
		debug->debugger->send("Restarting done");
	}
}


/****************************
 *
 * Public methods
 *
 ****************************/


void
Group::restart(const Options &options, RestartMethod method) {
	boost::container::vector<Callback> actions;

	assert(isAlive());
	P_DEBUG("Restarting group " << getName());

	// If there is currently a restarter thread or a spawner thread active,
	// the following tells them to abort their current work as soon as possible.
	restartsInitiated++;

	processesBeingSpawned = 0;
	m_spawning   = false;
	m_restarting = true;
	uuid         = generateUuid(pool);
	this->options.groupUuid = uuid;
	detachAll(actions);
	getPool()->interruptableThreads.create_thread(
		boost::bind(&Group::finalizeRestart, this, shared_from_this(),
			this->options.copyAndPersist().clearPerRequestFields(),
			options.copyAndPersist().clearPerRequestFields(),
			method, getContext()->spawningKitFactory,
			restartsInitiated, actions),
		"Group restarter: " + getName(),
		POOL_HELPER_THREAD_STACK_SIZE
	);
}

bool
Group::restarting() const {
	return m_restarting;
}

bool
Group::needsRestart(const Options &options) {
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

/**
 * Attempts to increase the number of processes by one, while respecting the
 * resource limits. That is, this method will ensure that there are at least
 * `minProcesses` processes, but no more than `maxProcesses` processes, and no
 * more than `pool->max` processes in the entire pool.
 */
SpawnResult
Group::spawn() {
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

bool
Group::spawning() const {
	return m_spawning;
}

/** Whether a new process should be spawned for this group. */
bool
Group::shouldSpawn() const {
	return allowSpawn()
		&& (
			!processLowerLimitsSatisfied()
			|| allEnabledProcessesAreTotallyBusy()
			|| !getWaitlist.empty()
		);
}

/** Whether a new process should be spawned for this group in the
 * specific case that another get action is to be performed.
 */
bool
Group::shouldSpawnForGetAction() const {
	return enabledCount == 0 || shouldSpawn();
}

/**
 * Whether a new process is allowed to be spawned for this group,
 * i.e. whether the upper processes limits have not been reached.
 */
bool
Group::allowSpawn() const {
	return isAlive()
		&& !processUpperLimitsReached()
		&& !poolAtFullCapacity();
}


} // namespace ApplicationPool2
} // namespace Passenger
