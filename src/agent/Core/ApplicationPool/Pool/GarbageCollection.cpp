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
#include <Core/ApplicationPool/Pool.h>

/*************************************************************************
 *
 * Garbage collection functions for ApplicationPool2::Pool
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


void
Pool::initializeGarbageCollection() {
	interruptableThreads.create_thread(
		boost::bind(garbageCollect, shared_from_this()),
		"Pool garbage collector",
		POOL_HELPER_THREAD_STACK_SIZE
	);
}

void
Pool::garbageCollect(PoolPtr self) {
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

void
Pool::maybeUpdateNextGcRuntime(GarbageCollectorState &state, unsigned long candidate) {
	if (state.nextGcRunTime == 0 || candidate < state.nextGcRunTime) {
		state.nextGcRunTime = candidate;
	}
}

void
Pool::checkWhetherProcessCanBeGarbageCollected(GarbageCollectorState &state,
	const GroupPtr &group, const ProcessPtr &process, ProcessList &output)
{
	assert(maxIdleTime > 0);
	unsigned long long processGcTime = process->lastUsed + maxIdleTime;
	if (process->sessions == 0
	 && state.now >= processGcTime)
	{
		if (output.capacity() == 0) {
			output.reserve(group->enabledCount);
		}
		output.push_back(process);
	} else {
		maybeUpdateNextGcRuntime(state, processGcTime);
	}
}

void
Pool::garbageCollectProcessesInGroup(GarbageCollectorState &state,
	const GroupPtr &group)
{
	ProcessList &processes = group->enabledProcesses;
	ProcessList processesToGc;
	ProcessList::iterator p_it, p_end = processes.end();

	for (p_it = processes.begin(); p_it != p_end; p_it++) {
		const ProcessPtr &process = *p_it;
		checkWhetherProcessCanBeGarbageCollected(state, group, process,
			processesToGc);
	}

	p_it  = processesToGc.begin();
	p_end = processesToGc.end();
	while (p_it != p_end
	 && (unsigned long) group->getProcessCount() > group->options.minProcesses)
	{
		ProcessPtr process = *p_it;
		P_DEBUG("Garbage collect idle process: " << process->inspect() <<
			", group=" << group->getName());
		group->detach(process, state.actions);
		p_it++;
	}
}

void
Pool::maybeCleanPreloader(GarbageCollectorState &state, const GroupPtr &group) {
	if (group->spawner->cleanable() && group->options.getMaxPreloaderIdleTime() != 0) {
		unsigned long long spawnerGcTime =
			group->spawner->lastUsed() +
			group->options.getMaxPreloaderIdleTime() * 1000000;
		if (state.now >= spawnerGcTime) {
			P_DEBUG("Garbage collect idle spawner: group=" << group->getName());
			group->cleanupSpawner(state.actions);
		} else {
			maybeUpdateNextGcRuntime(state, spawnerGcTime);
		}
	}
}

unsigned long long
Pool::realGarbageCollect() {
	TRACE_POINT();
	ScopedLock lock(syncher);
	GroupMap::ConstIterator g_it(groups);
	GarbageCollectorState state;
	state.now = SystemTime::getUsec();
	state.nextGcRunTime = 0;

	P_DEBUG("Garbage collection time...");
	verifyInvariants();

	// For all groups...
	while (*g_it != NULL) {
		const GroupPtr group = g_it.getValue();

		if (maxIdleTime > 0) {
			// ...detach processes that have been idle for more than maxIdleTime.
			garbageCollectProcessesInGroup(state, group);
		}

		group->verifyInvariants();

		// ...cleanup the spawner if it's been idle for more than preloaderIdleTime.
		maybeCleanPreloader(state, group);

		g_it.next();
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

void
Pool::wakeupGarbageCollector() {
	garbageCollectionCond.notify_all();
}


} // namespace ApplicationPool2
} // namespace Passenger
