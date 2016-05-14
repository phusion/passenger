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
 * Initialization and shutdown functions for ApplicationPool2::Group
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


ApiKey
Group::generateApiKey(const Pool *pool) {
	char value[ApiKey::SIZE];
	pool->getRandomGenerator()->generateAsciiString(value, ApiKey::SIZE);
	return ApiKey(StaticString(value, ApiKey::SIZE));
}

string
Group::generateUuid(const Pool *pool) {
	return pool->getRandomGenerator()->generateAsciiString(20);
}

bool
Group::shutdownCanFinish() const {
	LifeStatus lifeStatus = (LifeStatus) this->lifeStatus.load(boost::memory_order_seq_cst);
	return lifeStatus == SHUTTING_DOWN
		&& enabledCount == 0
		&& disablingCount == 0
 		&& disabledCount == 0
 		&& detachedProcesses.empty();
}

/** One of the post lock actions can potentially perform a long-running
 * operation, so running them in a thread is advised.
 */
void
Group::finishShutdown(boost::container::vector<Callback> &postLockActions) {
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
	this->lifeStatus.store(SHUT_DOWN, boost::memory_order_seq_cst);
	selfPointer.reset();
}


/****************************
 *
 * Public methods
 *
 ****************************/


Group::Group(Pool *_pool, const Options &_options)
	: pool(_pool),
	  uuid(generateUuid(_pool))
{
	info.context = _pool->getContext();
	info.group   = this;
	info.name    = _options.getAppGroupName().toString();
	info.apiKey  = generateApiKey(_pool);
	resetOptions(_options);
	enabledCount   = 0;
	disablingCount = 0;
	disabledCount  = 0;
	nEnabledProcessesTotallyBusy = 0;
	spawner        = getContext()->spawningKitFactory->create(options);
	restartsInitiated = 0;
	processesBeingSpawned = 0;
	m_spawning     = false;
	m_restarting   = false;
	lifeStatus.store(ALIVE, boost::memory_order_relaxed);
	lastRestartFileMtime = 0;
	lastRestartFileCheckTime = 0;
	alwaysRestartFileExists = false;
	if (options.restartDir.empty()) {
		restartFile = options.appRoot + "/tmp/restart.txt";
		alwaysRestartFile = options.appRoot + "/tmp/always_restart.txt";
	} else if (options.restartDir[0] == '/') {
		restartFile = options.restartDir + "/restart.txt";
		alwaysRestartFile = options.restartDir + "/always_restart.txt";
	} else {
		restartFile = options.appRoot + "/" + options.restartDir + "/restart.txt";
		alwaysRestartFile = options.appRoot + "/" + options.restartDir + "/always_restart.txt";
	}

	detachedProcessesCheckerActive = false;
}

Group::~Group() {
	LifeStatus lifeStatus = getLifeStatus();
	if (OXT_UNLIKELY(lifeStatus == ALIVE)) {
		P_BUG("You must call Group::shutdown() before destroying a Group.");
	}
	assert(lifeStatus == SHUT_DOWN);
	assert(!detachedProcessesCheckerActive);
	assert(getWaitlist.empty());
}

bool
Group::initialize() {
	nullProcess = createNullProcessObject();
	return true;
}

/**
 * Must be called before destroying a Group. You can optionally provide a
 * callback so that you are notified when shutdown has finished.
 *
 * The caller is responsible for migrating waiters on the getWaitlist.
 *
 * One of the post lock actions can potentially perform a long-running
 * operation, so running them in a thread is advised.
 */
void
Group::shutdown(const Callback &callback,
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
	lifeStatus.store(SHUTTING_DOWN, boost::memory_order_seq_cst);
}


} // namespace ApplicationPool2
} // namespace Passenger
