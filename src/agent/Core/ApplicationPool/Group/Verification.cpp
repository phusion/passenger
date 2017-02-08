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
 * Correctness verification functions for ApplicationPool2::Group
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


bool
Group::selfCheckingEnabled() const {
	return pool->selfchecking;
}

void
Group::verifyInvariants() const {
	// !a || b: logical equivalent of a IMPLIES b.
	#ifndef NDEBUG
	if (!selfCheckingEnabled()) {
		return;
	}

	LifeStatus lifeStatus = (LifeStatus) this->lifeStatus.load(boost::memory_order_relaxed);

	assert(enabledCount >= 0);
	assert(disablingCount >= 0);
	assert(disabledCount >= 0);
	assert(nEnabledProcessesTotallyBusy >= 0);
	assert(!( enabledCount == 0 && disablingCount > 0 ) || ( processesBeingSpawned > 0) );
	assert(!( !m_spawning ) || ( enabledCount > 0 || disablingCount == 0 ));

	assert((lifeStatus == ALIVE) == (spawner != NULL));

	// Verify getWaitlist invariants.
	assert(!( !getWaitlist.empty() ) || ( enabledProcesses.empty() || verifyNoRequestsOnGetWaitlistAreRoutable() ));
	assert(!( enabledProcesses.empty() && !m_spawning && !restarting() && !poolAtFullCapacity() ) || ( getWaitlist.empty() ));
	assert(!( !getWaitlist.empty() ) || ( !enabledProcesses.empty() || m_spawning || restarting() || poolAtFullCapacity() ));

	// Verify disableWaitlist invariants.
	assert((int) disableWaitlist.size() >= disablingCount);

	// Verify processesBeingSpawned, m_spawning and m_restarting.
	assert(!( processesBeingSpawned > 0 ) || ( m_spawning ));
	assert(!( m_restarting ) || ( processesBeingSpawned == 0 ));

	// Verify lifeStatus.
	if (lifeStatus != ALIVE) {
		assert(enabledCount == 0);
		assert(disablingCount == 0);
		assert(disabledCount == 0);
		assert(nEnabledProcessesTotallyBusy == 0);
	}

	// Verify list sizes.
	assert((int) enabledProcesses.size() == enabledCount);
	assert((int) disablingProcesses.size() == disablingCount);
	assert((int) disabledProcesses.size() == disabledCount);
	assert(nEnabledProcessesTotallyBusy <= enabledCount);
	#endif
}

void
Group::verifyExpensiveInvariants() const {
	#ifndef NDEBUG
	// !a || b: logical equivalent of a IMPLIES b.

	if (!selfCheckingEnabled()) {
		return;
	}

	ProcessList::const_iterator it, end;

	end = enabledProcesses.end();
	for (it = enabledProcesses.begin(); it != end; it++) {
		const ProcessPtr &process = *it;
		assert(process->enabled == Process::ENABLED);
		assert(process->isAlive());
		assert(process->oobwStatus == Process::OOBW_NOT_ACTIVE
			|| process->oobwStatus == Process::OOBW_REQUESTED);
	}

	end = disablingProcesses.end();
	for (it = disablingProcesses.begin(); it != end; it++) {
		const ProcessPtr &process = *it;
		assert(process->enabled == Process::DISABLING);
		assert(process->isAlive());
		assert(process->oobwStatus == Process::OOBW_NOT_ACTIVE
			|| process->oobwStatus == Process::OOBW_IN_PROGRESS);
	}

	end = disabledProcesses.end();
	for (it = disabledProcesses.begin(); it != end; it++) {
		const ProcessPtr &process = *it;
		assert(process->enabled == Process::DISABLED);
		assert(process->isAlive());
		assert(process->oobwStatus == Process::OOBW_NOT_ACTIVE
			|| process->oobwStatus == Process::OOBW_IN_PROGRESS);
	}

	foreach (const ProcessPtr &process, detachedProcesses) {
		assert(process->enabled == Process::DETACHED);
	}
	#endif
}

#ifndef NDEBUG
bool
Group::verifyNoRequestsOnGetWaitlistAreRoutable() const {
	deque<GetWaiter>::const_iterator it, end = getWaitlist.end();

	for (it = getWaitlist.begin(); it != end; it++) {
		if (route(it->options).process != NULL) {
			return false;
		}
	}
	return true;
}
#endif


} // namespace ApplicationPool2
} // namespace Passenger
