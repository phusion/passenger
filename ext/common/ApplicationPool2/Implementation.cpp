/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2011, 2012 Phusion
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
#include <typeinfo>
#include <boost/make_shared.hpp>
#include <oxt/backtrace.hpp>
#include <ApplicationPool2/Pool.h>
#include <ApplicationPool2/SuperGroup.h>
#include <ApplicationPool2/Group.h>
#include <Exceptions.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


template<typename T>
bool
exceptionIsInstanceOf(const tracable_exception &e) {
	try {
		(void) dynamic_cast<T>(e);
		return true;
	} catch (const bad_cast &) {
		return false;
	}
}

#define TRY_COPY_EXCEPTION(klass) \
	do { \
		if (exceptionIsInstanceOf<const klass &>(e)) { \
			return make_shared<klass>( (const klass &) e ); \
		} \
	} while (false)

ExceptionPtr
copyException(const tracable_exception &e) {
	TRY_COPY_EXCEPTION(FileSystemException);
	TRY_COPY_EXCEPTION(TimeRetrievalException);
	TRY_COPY_EXCEPTION(SystemException);
	
	TRY_COPY_EXCEPTION(FileNotFoundException);
	TRY_COPY_EXCEPTION(EOFException);
	TRY_COPY_EXCEPTION(IOException);
	
	TRY_COPY_EXCEPTION(ConfigurationException);
	
	TRY_COPY_EXCEPTION(SpawnException);
	
	TRY_COPY_EXCEPTION(InvalidModeStringException);
	TRY_COPY_EXCEPTION(ArgumentException);
	
	TRY_COPY_EXCEPTION(RuntimeException);
	
	TRY_COPY_EXCEPTION(TimeoutException);
	
	TRY_COPY_EXCEPTION(NonExistentUserException);
	TRY_COPY_EXCEPTION(NonExistentGroupException);
	TRY_COPY_EXCEPTION(SecurityException);
	
	TRY_COPY_EXCEPTION(SyntaxError);
	
	TRY_COPY_EXCEPTION(boost::lock_error);
	TRY_COPY_EXCEPTION(boost::thread_resource_error);
	TRY_COPY_EXCEPTION(boost::unsupported_thread_option);
	TRY_COPY_EXCEPTION(boost::invalid_thread_argument);
	TRY_COPY_EXCEPTION(boost::thread_permission_error);

	TRY_COPY_EXCEPTION(boost::thread_interrupted);
	TRY_COPY_EXCEPTION(boost::thread_exception);
	TRY_COPY_EXCEPTION(boost::condition_error);

	return make_shared<tracable_exception>(e);
}

#define TRY_RETHROW_EXCEPTION(klass) \
	do { \
		if (exceptionIsInstanceOf<const klass &>(*e)) { \
			throw klass((const klass &) *e); \
		} \
	} while (false)

void
rethrowException(const ExceptionPtr &e) {
	TRY_RETHROW_EXCEPTION(FileSystemException);
	TRY_RETHROW_EXCEPTION(TimeRetrievalException);
	TRY_RETHROW_EXCEPTION(SystemException);
	
	TRY_RETHROW_EXCEPTION(FileNotFoundException);
	TRY_RETHROW_EXCEPTION(EOFException);
	TRY_RETHROW_EXCEPTION(IOException);
	
	TRY_RETHROW_EXCEPTION(ConfigurationException);
	
	TRY_RETHROW_EXCEPTION(SpawnException);
	
	TRY_RETHROW_EXCEPTION(InvalidModeStringException);
	TRY_RETHROW_EXCEPTION(ArgumentException);
	
	TRY_RETHROW_EXCEPTION(RuntimeException);
	
	TRY_RETHROW_EXCEPTION(TimeoutException);
	
	TRY_RETHROW_EXCEPTION(NonExistentUserException);
	TRY_RETHROW_EXCEPTION(NonExistentGroupException);
	TRY_RETHROW_EXCEPTION(SecurityException);
	
	TRY_RETHROW_EXCEPTION(SyntaxError);

	TRY_RETHROW_EXCEPTION(boost::lock_error);
	TRY_RETHROW_EXCEPTION(boost::thread_resource_error);
	TRY_RETHROW_EXCEPTION(boost::unsupported_thread_option);
	TRY_RETHROW_EXCEPTION(boost::invalid_thread_argument);
	TRY_RETHROW_EXCEPTION(boost::thread_permission_error);

	TRY_RETHROW_EXCEPTION(boost::thread_interrupted);
	TRY_RETHROW_EXCEPTION(boost::thread_exception);
	TRY_RETHROW_EXCEPTION(boost::condition_error);
	
	throw tracable_exception(*e);
}


const SuperGroupPtr
Pool::getSuperGroup(const char *name) {
	return superGroups.get(name);
}


boost::mutex &
SuperGroup::getPoolSyncher(const PoolPtr &pool) {
	return pool->syncher;
}

void
SuperGroup::runAllActions(const vector<Callback> &actions) {
	Pool::runAllActions(actions);
}

string
SuperGroup::generateSecret() const {
	return getPool()->randomGenerator->generateAsciiString(43);
}

void
SuperGroup::createInterruptableThread(const function<void ()> &func, const string &name,
	unsigned int stackSize)
{
	getPool()->interruptableThreads.create_thread(func, name, stackSize);
}

void
SuperGroup::createNonInterruptableThread(const function<void ()> &func, const string &name,
	unsigned int stackSize)
{
	getPool()->nonInterruptableThreads.create_thread(func, name, stackSize);
}


Group::Group(const SuperGroupPtr &_superGroup, const Options &options, const ComponentInfo &info)
	: superGroup(_superGroup),
	  name(_superGroup->name + "#" + info.name),
	  secret(generateSecret(_superGroup)),
	  componentInfo(info)
{
	count          = 0;
	disablingCount = 0;
	disabledCount  = 0;
	spawner        = getPool()->spawnerFactory->create(options);
	m_spawning     = false;
	m_restarting   = false;
	if (options.restartDir.empty()) {
		restartFile = options.appRoot + "/tmp/restart.txt";
		alwaysRestartFile = options.appRoot + "/always_restart.txt";
	} else {
		restartFile = options.restartDir + "/restart.txt";
		alwaysRestartFile = options.restartDir + "/always_restart.txt";
	}
	resetOptions(options);
}

PoolPtr
Group::getPool() const {
	SuperGroupPtr superGroup = getSuperGroup();
	if (superGroup != NULL) {
		return superGroup->getPool();
	} else {
		return PoolPtr();
	}
}

void
Group::createInterruptableThread(const function<void ()> &func, const string &name,
	unsigned int stackSize)
{
	getPool()->interruptableThreads.create_thread(func, name, stackSize);
}

void
Group::onSessionInitiateFailure(const ProcessPtr &process, Session *session) {
	vector<Callback> actions;

	TRACE_POINT();
	// Standard resource management boilerplate stuff...
	PoolPtr pool = getPool();
	if (OXT_UNLIKELY(pool == NULL)) {
		return;
	}
	unique_lock<boost::mutex> lock(pool->syncher);
	pool = getPool();
	if (OXT_UNLIKELY(pool == NULL)) {
		return;
	}

	UPDATE_TRACE_POINT();
	if (pool->detachProcessUnlocked(process, actions)) {
		P_DEBUG("Could not initiate a session with process " <<
			process->inspect() << ", detached from pool");
	}
	lock.unlock();
	runAllActions(actions);
}

void
Group::onSessionClose(const ProcessPtr &process, Session *session) {
	TRACE_POINT();
	// Standard resource management boilerplate stuff...
	PoolPtr pool = getPool();
	if (OXT_UNLIKELY(pool == NULL)) {
		return;
	}
	unique_lock<boost::mutex> lock(pool->syncher);
	pool = getPool();
	if (OXT_UNLIKELY(pool == NULL)) {
		return;
	}
	
	verifyInvariants();
	UPDATE_TRACE_POINT();
	
	/* Update statistics. */
	process->sessionClosed(session);
	pqueue.decrease(process->pqHandle, process->usage());

	bool maxRequestsReached = options.maxRequests > 0
		&& process->processed >= options.maxRequests;

	/* This group now has a process that's guaranteed to be not at
	 * full capacity...
	 */
	assert(!process->atFullCapacity());
	if (!getWaitlist.empty() && !maxRequestsReached) {
		/* ...so if there are clients waiting for a process to
		 * become available, call them now.
		 */
		UPDATE_TRACE_POINT();
		assignSessionsToGetWaitersQuickly(lock);
	} else if (!pool->getWaitlist.empty() || maxRequestsReached) {
		/* Someone might be trying to get() a session for a different
		 * group that couldn't be spawned because of lack of pool capacity.
		 * If this group isn't under sufficiently load (as apparent by the
		 * emptiness of the get wait list) then now's a good time to detach
		 * this process or group in order to free capacity.
		 *
		 * TODO: this strategy can cause starvation if all groups are
		 * continuously busy. Yeah if the system is under that much load
		 * it is probably fsck'ed anyway but it may be a good idea in the
		 * future to detach this group even if the wait list is non-empty
		 * in case there's a waiter on the pool's get wait list that's older
		 * than X seconds.
		 *
		 * ---- OR ----
		 *
		 * This process has processed its maximum number of requests,
		 * so we detach it.
		 */
		UPDATE_TRACE_POINT();
		if (!pool->getWaitlist.empty()) {
			P_DEBUG("Process " << process->inspect() << " is no longer at "
				"full capacity; detaching it in order to make room in the pool");
		} else {
			P_DEBUG("Process " << process->inspect() <<
				" has reached its maximum number of requests (" <<
				options.maxRequests << "); detaching it");
		}

		vector<Callback> actions;
		// Will take care of processing pool->getWaitlist.
		pool->detachProcessUnlocked(process, actions);
		pool->verifyInvariants();
		verifyInvariants();
		lock.unlock();
		runAllActions(actions);
	}
}

// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::spawnThreadMain(GroupPtr self, SpawnerPtr spawner, Options options) {
	try {
		spawnThreadRealMain(spawner, options);
	} catch (const thread_interrupted &) {
		// Return.
	}
}

void
Group::spawnThreadRealMain(const SpawnerPtr &spawner, const Options &options) {
	TRACE_POINT();
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	
	bool done = false;
	while (!done) {
		ProcessPtr process;
		ExceptionPtr exception;
		try {
			UPDATE_TRACE_POINT();
			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			process = spawner->spawn(options);
		} catch (const thread_interrupted &) {
			return;
		} catch (const tracable_exception &e) {
			exception = copyException(e);
			// Let other (unexpected) exceptions crash the program so
			// gdb can generate a backtrace.
		}
		UPDATE_TRACE_POINT();
		PoolPtr pool = getPool();
		if (pool == NULL) {
			return;
		}
		{
			LockGuard l(pool->debugSyncher);
			pool->spawnLoopIteration++;
			P_TRACE(2, "Entering spawn loop iteration " << pool->spawnLoopIteration);
		}
		unique_lock<boost::mutex> lock(pool->syncher);
		pool = getPool();
		if (pool == NULL) {
			return;
		}
		
		verifyInvariants();
		P_ASSERT(m_spawning || m_restarting);
		
		UPDATE_TRACE_POINT();
		vector<Callback> actions;
		if (process != NULL) {
			attach(process, actions);
			if (getWaitlist.empty()) {
				pool->assignSessionsToGetWaiters(actions);
			} else {
				assignSessionsToGetWaiters(actions);
			}
			P_DEBUG("Attached process " << process->inspect() <<
				" to group " << name <<
				": new process count = " << count <<
				", remaining get waiters = " << getWaitlist.size());
		} else {
			// TODO: sure this is the best thing? if there are
			// processes currently alive we should just use them.
			P_DEBUG("Could not spawn process appRoot=" << name <<
				": " << exception->what());
			assignExceptionToGetWaiters(exception, actions);
			pool->assignSessionsToGetWaiters(actions);
			done = true;
		}
		
		// Temporarily mark this Group as 'not spawning' so
		// that pool->usage() doesn't take this thread's spawning
		// state into account.
		m_spawning = false;
		
		done = done
			|| ((unsigned long) count >= options.minProcesses && getWaitlist.empty())
			|| pool->atFullCapacity(false)
			|| m_restarting;
		m_spawning = !done;
		if (done) {
			if (m_restarting) {
				P_DEBUG("Spawn loop aborted because the group is being restarted");
			} else {
				P_DEBUG("Spawn loop done");
			}
		} else {
			P_DEBUG("Continue spawning");
		}
		
		UPDATE_TRACE_POINT();
		verifyInvariants();
		lock.unlock();
		runAllActions(actions);
	}
}

bool
Group::shouldSpawn() const {
	return !spawning()
		&& (count == 0 || pqueue.top()->atFullCapacity())
		&& !getPool()->atFullCapacity(false);
}

void
Group::restart(const Options &options) {
	vector<Callback> actions;

	assert(!m_restarting);
	P_DEBUG("Restarting group " << name);
	m_spawning = false;
	m_restarting = true;
	detachAll(actions);
	getPool()->nonInterruptableThreads.create_thread(
		boost::bind(&Group::finalizeRestart, this, shared_from_this(), options.copyAndPersist(),
			getPool()->spawnerFactory, actions),
		"Group restarter: " + name,
		POOL_HELPER_THREAD_STACK_SIZE
	);
}

// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::finalizeRestart(GroupPtr self, Options options, SpawnerFactoryPtr spawnerFactory,
	vector<Callback> postLockActions)
{
	TRACE_POINT();

	Pool::runAllActions(postLockActions);
	postLockActions.clear();

	// Create a new spawner.
	SpawnerPtr newSpawner = spawnerFactory->create(options);
	SpawnerPtr oldSpawner;

	// Standard resource management boilerplate stuff...
	UPDATE_TRACE_POINT();
	PoolPtr pool = getPool();
	if (OXT_UNLIKELY(pool == NULL)) {
		return;
	}
	LockGuard l(pool->syncher);
	pool = getPool();
	if (OXT_UNLIKELY(pool == NULL)) {
		return;
	}

	// Run some sanity checks.
	verifyInvariants();
	assert(m_restarting);
	UPDATE_TRACE_POINT();
	
	// Atomically swap the new spawner with the old one.
	resetOptions(options);
	oldSpawner = spawner;
	spawner    = newSpawner;

	m_restarting = false;
	if (!getWaitlist.empty()) {
		spawn();
	}
	P_DEBUG("Restart of group " << name << " done");
	verifyInvariants();
	Pool::runAllActions(postLockActions);
	// oldSpawner will now be destroyed, outside the lock.
}

string
Group::generateSecret(const SuperGroupPtr &superGroup) {
	return superGroup->getPool()->randomGenerator->generateAsciiString(43);
}


string
Process::inspect() const {
	stringstream result;
	result << "(pid=";
	result << pid;
	GroupPtr group = getGroup();
	if (group != NULL) {
		result << ", group=";
		result << group->name;
	}
	result << ")";
	return result.str();
}


const string &
Session::getConnectPassword() const {
	return process->connectPassword;
}

pid_t
Session::getPid() const {
	return process->pid;
}

const string &
Session::getGupid() const {
	return process->gupid;
}


SmartSpawner::PipeWatcher::PipeWatcher(
	const SafeLibevPtr &_libev,
	const FileDescriptor &_fd,
	bool _forward)
	: libev(_libev),
	  fd(_fd),
	  forward(_forward)
{
	watcher.set(fd, ev::READ);
	watcher.set<PipeWatcher, &PipeWatcher::onReadable>(this);
	libev->start(watcher);
}

SmartSpawner::PipeWatcher::~PipeWatcher() {
	libev->stop(watcher);
}

void
SmartSpawner::PipeWatcher::start() {
	selfPointer = shared_from_this();
}

void
SmartSpawner::PipeWatcher::onReadable(ev::io &io, int revents) {
	char buf[1024 * 8];
	ssize_t ret;
	
	ret = syscalls::read(fd, buf, sizeof(buf));
	if (ret <= 0) {
		libev->stop(watcher);
		selfPointer.reset();
	} else if (forward) {
		write(STDERR_FILENO, buf, ret);
	}
}


} // namespace ApplicationPool2
} // namespace Passenger
