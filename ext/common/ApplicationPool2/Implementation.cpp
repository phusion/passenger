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
	
	throw tracable_exception(*e);
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
	  componentInfo(info)
{
	name           = _superGroup->name + "#" + info.name;
	secret         = generateSecret();
	count          = 0;
	disablingCount = 0;
	disabledCount  = 0;
	spawner        = getPool()->spawnerFactory->create(options);
	m_spawning     = false;
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
	
	/* This group now has a process that's guaranteed to be not at
	 * full capacity...
	 */
	assert(!process->atFullCapacity());
	if (!getWaitlist.empty()) {
		/* ...so if there are clients waiting for a process to
		 * become available, call them now.
		 */
		UPDATE_TRACE_POINT();
		assignSessionsToGetWaitersQuickly(lock);
	} else if (!pool->getWaitlist.empty()) {
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
		 */
		UPDATE_TRACE_POINT();
		vector<Callback> actions;
		// Will take care of processing pool->getWaitlist.
		pool->detachProcess(process, false);
		pool->verifyInvariants();
		verifyInvariants();
		lock.unlock();
		runAllActions(actions);
	}
}

// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::spawnThreadMain(GroupPtr self, SpawnerPtr spawner, Options options) {
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	
	bool done = false;
	while (!done) {
		ProcessPtr process;
		ExceptionPtr exception;
		try {
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
		PoolPtr pool = getPool();
		if (pool == NULL) {
			return;
		}
		unique_lock<boost::mutex> lock(pool->syncher);
		pool = getPool();
		if (pool == NULL) {
			return;
		}
		
		verifyInvariants();
		
		vector<Callback> actions;
		if (process != NULL) {
			attach(process, actions);
			if (getWaitlist.empty()) {
				pool->assignSessionsToGetWaiters(actions);
			} else {
				assignSessionsToGetWaiters(actions);
			}
		} else {
			// TODO: sure this is the best thing? if there are
			// processes currently alive we should just use them.
			assignExceptionToGetWaiters(exception, actions);
			pool->assignSessionsToGetWaiters(actions);
		}
		
		// Temporarily mark this Group as 'not spawning' so
		// that pool->usage() doesn't take this thread's spawning
		// state into account.
		m_spawning = false;
		
		done = (unsigned long) count >= options.minProcesses
			|| pool->atFullCapacity(false);
		m_spawning = !done;
		
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
	
	secret = generateSecret();
	resetOptions(options);
	spawner = getPool()->spawnerFactory->create(options);
	while (!processes.empty()) {
		ProcessPtr process = processes.front();
		detach(process, actions);
		processes.pop_front();
	}
	
	if (!actions.empty()) {
		getPool()->nonInterruptableThreads.create_thread(
			boost::bind(Pool::runAllActionsWithCopy, actions),
			"Post lock actions runner",
			64 * 1024);
	}
}

string
Group::generateSecret() const {
	return getPool()->randomGenerator->generateAsciiString(43);
}


pid_t
Session::getPid() const {
	return process->pid;
}


} // namespace ApplicationPool2
} // namespace Passenger
