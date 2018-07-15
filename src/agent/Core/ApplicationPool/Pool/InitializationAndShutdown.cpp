/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
 * Initialization and shutdown-related code for ApplicationPool2::Pool
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


Pool::Pool(Context *_context)
	: context(_context),
	  abortLongRunningConnectionsCallback(NULL)
{
	try {
		systemMetricsCollector.collect(systemMetrics);
	} catch (const RuntimeException &e) {
		P_WARN("Unable to collect system metrics: " << e.what());
	}

	lifeStatus   = ALIVE;
	max          = 6;
	maxIdleTime  = 60 * 1000000;
	selfchecking = true;
	palloc       = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

	// The following code only serve to instantiate certain inline methods
	// so that they can be invoked from gdb.
	(void) GroupPtr().get();
	(void) ProcessPtr().get();
	(void) SessionPtr().get();
}

Pool::~Pool() {
	if (lifeStatus != SHUT_DOWN) {
		P_BUG("You must call Pool::destroy() before actually destroying the Pool object!");
	}
	psg_destroy_pool(palloc);
}

/** Must be called right after construction. */
void
Pool::initialize() {
	LockGuard l(syncher);
	initializeAnalyticsCollection();
	initializeGarbageCollection();
}

void
Pool::initDebugging() {
	LockGuard l(syncher);
	debugSupport = boost::make_shared<DebugSupport>();
}

/**
 * Should be called right after the agent has received
 * the message to exit gracefully. This will tell processes to
 * abort any long-running connections, e.g. WebSocket connections,
 * because the Core::Controller has to wait until all connections are
 * finished before proceeding with shutdown.
 */
void
Pool::prepareForShutdown() {
	TRACE_POINT();
	ScopedLock lock(syncher);
	assert(lifeStatus == ALIVE);
	lifeStatus = PREPARED_FOR_SHUTDOWN;
	if (abortLongRunningConnectionsCallback) {
		vector<ProcessPtr> processes = getProcesses(false);
		foreach (ProcessPtr process, processes) {
			// Ensure that the process is not immediately respawned.
			process->getGroup()->options.minProcesses = 0;
			abortLongRunningConnectionsCallback(process);
		}
	}
}

/** Must be called right before destruction. */
void
Pool::destroy() {
	TRACE_POINT();
	ScopedLock lock(syncher);
	assert(lifeStatus == ALIVE || lifeStatus == PREPARED_FOR_SHUTDOWN);

	lifeStatus = SHUTTING_DOWN;

	while (!groups.empty()) {
		GroupPtr *group;
		groups.lookupRandom(NULL, &group);
		string name = group->get()->getName().toString();
		lock.unlock();
		detachGroupByName(name);
		lock.lock();
	}

	UPDATE_TRACE_POINT();
	lock.unlock();
	P_DEBUG("Shutting down ApplicationPool background threads...");
	interruptableThreads.interrupt_and_join_all();
	nonInterruptableThreads.join_all();
	lock.lock();

	lifeStatus = SHUT_DOWN;

	UPDATE_TRACE_POINT();
	verifyInvariants();
	verifyExpensiveInvariants();
}


} // namespace ApplicationPool2
} // namespace Passenger
