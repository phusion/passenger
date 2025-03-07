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
#ifndef _PASSENGER_APPLICATION_POOL_GROUP_SESSION_MANAGEMENT_CPP_
#define _PASSENGER_APPLICATION_POOL_GROUP_SESSION_MANAGEMENT_CPP_

#ifdef INTELLISENSE
	#include <Core/ApplicationPool/Pool.h>
#endif
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


/* Determines which process to route a get() action to. The returned process
 * is guaranteed to be `canBeRoutedTo()`, i.e. not totally busy.
 *
 * A request is routed to an enabled processes, or if there are none,
 * from a disabling process. The rationale is as follows:
 * If there are no enabled process, then waiting for one to spawn is too
 * expensive. The next best thing is to route to disabling processes
 * until more processes have been spawned.
 */
Group::RouteResult
Group::route(const Options &options) const {
	Process *process = nullptr;
	if (OXT_LIKELY(enabledCount > 0)) {
		if (options.stickySessionId == 0) {
			if (OXT_LIKELY(useNewRouting())) {
				process = findBestProcess(enabledProcesses);
				if (process != nullptr) {
					assert(process->canBeRoutedTo());
					return RouteResult(process);
				} else {
					return RouteResult(NULL, true);
				}
            } else {
				process = findEnabledProcessWithLowestBusyness();
				if (process && process->canBeRoutedTo()) {
					return RouteResult(process);
				} else {
					return RouteResult(NULL, process == nullptr);
				}
			}
		} else {
			if (OXT_LIKELY(useNewRouting())) {
				process = findBestProcessPreferringStickySessionId(options.stickySessionId);
				if (process != nullptr) {
					if (process->canBeRoutedTo()) {
						return RouteResult(process);
					} else {
						return RouteResult(NULL, false);
					}
				} else {
					return RouteResult(NULL, true);
				}
			}else{
				process = findProcessWithStickySessionIdOrLowestBusyness(options.stickySessionId);
				if (process && process->canBeRoutedTo()) {
					return RouteResult(process);
				} else {
					return RouteResult(NULL, process == nullptr);
				}
			}
		}
	} else {
		if (OXT_LIKELY(useNewRouting())) {
			process = findBestProcess(disablingProcesses);
			if (process != nullptr) {
				assert(process->canBeRoutedTo());
				return RouteResult(process);
			} else {
				return RouteResult(NULL, true);
			}
		} else {
			process = findProcessWithLowestBusyness(disablingProcesses);
			if (process && process->canBeRoutedTo()) {
				return RouteResult(process);
			} else {
				return RouteResult(NULL, process == nullptr);
			}
		}
	}
}

SessionPtr
Group::newSession(Process *process, unsigned long long now) {
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

void
Group::_onSessionInitiateFailure(Session *session) {
	Process *process = session->getProcess();
	assert(process != NULL);
	process->getGroup()->onSessionInitiateFailure(process, session);
}

void
Group::_onSessionClose(Session *session) {
	Process *process = session->getProcess();
	assert(process != NULL);
	process->getGroup()->onSessionClose(process, session);
}

OXT_FORCE_INLINE void
Group::onSessionInitiateFailure(Process *process, Session *session) {
	boost::container::vector<Callback> actions;

	TRACE_POINT();
	// Standard resource management boilerplate stuff...
	Pool *pool = getPool();
	boost::unique_lock<boost::mutex> lock(pool->syncher);
	assert(process->isAlive());
	assert(isAlive() || getLifeStatus() == SHUTTING_DOWN);

	UPDATE_TRACE_POINT();
	P_DEBUG("Could not initiate a session with process " <<
		process->inspect() << ", detaching from pool if possible");
	if (!pool->detachProcessUnlocked(process->shared_from_this(), actions)) {
		P_DEBUG("Process was already detached");
	}
	pool->fullVerifyInvariants();
	lock.unlock();
	runAllActions(actions);
}

OXT_FORCE_INLINE void
Group::onSessionClose(Process *process, Session *session) {
	TRACE_POINT();
	// Standard resource management boilerplate stuff...
	Pool *pool = getPool();
	boost::unique_lock<boost::mutex> lock(pool->syncher);
	assert(process->isAlive());
	assert(isAlive() || getLifeStatus() == SHUTTING_DOWN);

	P_TRACE(2, "Session closed for process " << process->inspect());
	verifyInvariants();
	UPDATE_TRACE_POINT();

	/* Update statistics. */
	bool wasTotallyBusy = process->isTotallyBusy();
	process->sessionClosed(session);
	assert(process->getLifeStatus() == Process::ALIVE);
	assert(process->enabled == Process::ENABLED
		|| process->enabled == Process::DISABLING
		|| process->enabled == Process::DETACHED);
	if (process->enabled == Process::ENABLED) {
		enabledProcessBusynessLevels[process->getIndex()] = process->busyness();
		if (wasTotallyBusy) {
			assert(nEnabledProcessesTotallyBusy >= 1);
			nEnabledProcessesTotallyBusy--;
		}
	}

	/* This group now has a process that's guaranteed to be not
	 * totally busy.
	 */
	assert(!process->isTotallyBusy());

	bool detachingBecauseOfMaxRequests = false;
	bool detachingBecauseCapacityNeeded = false;
	bool shouldDetach =
		( detachingBecauseOfMaxRequests = (
			options.maxRequests > 0
			&& process->processed >= options.maxRequests
		)) || (
			detachingBecauseCapacityNeeded = (
				process->sessions == 0
				&& getWaitlist.empty()
				&& (
					!pool->getWaitlist.empty()
					|| anotherGroupIsWaitingForCapacity()
				)
			)
		);
	bool shouldDisable =
		process->enabled == Process::DISABLING
		&& process->sessions == 0
		&& enabledCount > 0;

	if (shouldDetach || shouldDisable) {
		UPDATE_TRACE_POINT();
		boost::container::vector<Callback> actions;

		if (shouldDetach) {
			if (detachingBecauseCapacityNeeded) {
				/* Someone might be trying to get() a session for a different
				 * group that couldn't be spawned because of lack of pool capacity.
				 * If this group isn't under sufficiently load (as apparent by the
				 * checked conditions) then now's a good time to detach
				 * this process or group in order to free capacity.
				 */
				P_DEBUG("Process " << process->inspect() << " is no longer totally "
					"busy; detaching it in order to make room in the pool");
			} else {
				/* This process has processed its maximum number of requests,
				 * so we detach it.
				 */
				P_DEBUG("Process " << process->inspect() <<
					" has reached its maximum number of requests (" <<
					options.maxRequests << "); detaching it");
			}
			pool->detachProcessUnlocked(process->shared_from_this(), actions);
		} else {
			ProcessPtr processPtr = process->shared_from_this();
			removeProcessFromList(processPtr, disablingProcesses);
			addProcessToList(processPtr, disabledProcesses);
			removeFromDisableWaitlist(processPtr, DR_SUCCESS, actions);
			maybeInitiateOobw(process);
		}

		pool->fullVerifyInvariants();
		lock.unlock();
		runAllActions(actions);

	} else {
		UPDATE_TRACE_POINT();

		// This could change process->enabled.
		maybeInitiateOobw(process);

		if (!getWaitlist.empty() && process->enabled == Process::ENABLED) {
			/* If there are clients on this group waiting for a process to
			 * become available then call them now.
			 */
			UPDATE_TRACE_POINT();
			// Already calls verifyInvariants().
			assignSessionsToGetWaitersQuickly(lock);
		}
	}
}


/****************************
 *
 * Public methods
 *
 ****************************/


SessionPtr
Group::get(const Options &newOptions, const GetCallback &callback,
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
			Process *process = nullptr;
			if (OXT_LIKELY(useNewRouting())) {
				process = findBestProcess(disablingProcesses);
			} else {
				process = findProcessWithLowestBusyness(disablingProcesses);
			}
			if (process != nullptr && !process->isTotallyBusy()) {
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

bool
Group::useNewRouting() const {
	return !pool->context->oldRouting;
}

} // namespace ApplicationPool2
} // namespace Passenger

#endif // _PASSENGER_APPLICATION_POOL_GROUP_SESSION_MANAGEMENT_CPP_
