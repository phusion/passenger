#include <TestSupport.h>
#include <ApplicationPool2/Pool.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct ApplicationPool2_PoolTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		BackgroundEventLoop bg;
		PoolPtr pool;
		GetCallback callback;
		SessionPtr currentSession;
		ExceptionPtr currentException;
		AtomicInt number;
		
		ApplicationPool2_PoolTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			pool = make_shared<Pool>(bg.libev,
				make_shared<SpawnerFactory>(bg.libev, *resourceLocator, generation));
			bg.start();
			callback = boost::bind(&ApplicationPool2_PoolTest::_callback, this, _1, _2);
		}
		
		~ApplicationPool2_PoolTest() {
			// Explicitly destroy these here because they can run
			// additional code that depend on other fields in this
			// class.
			currentSession.reset();
			pool.reset();
		}
		
		Options createOptions() {
			Options options;
			options.spawnMethod = "dummy";
			options.appRoot = "stub/rack";
			options.startCommand = "ruby\1" "start.rb";
			options.startupFile  = "stub/rack/start.rb";
			return options;
		}
		
		void _callback(const SessionPtr &session, const ExceptionPtr &e) {
			currentSession = session;
			currentException = e;
			number++;
		}
	};
	
	DEFINE_TEST_GROUP_WITH_LIMIT(ApplicationPool2_PoolTest, 100);
	
	TEST_METHOD(1) {
		// Test initial state.
		ensure(!pool->atFullCapacity());
	}
	
	
	/*********** Test asyncGet() behavior on a single SuperGroup and Group ***********/
	
	TEST_METHOD(2) {
		// asyncGet() actions on empty pools cannot be immediately satisfied.
		// Instead a new process will be spawned. In the mean time get()
		// actions are put on a wait list which will be processed as soon
		// as the new process is done spawning.
		Options options = createOptions();
		
		ScopedLock l(pool->syncher);
		pool->asyncGet(options, callback, false);
		ensure_equals(number, 0);
		ensure(pool->getWaitlist.empty());
		ensure(!pool->superGroups.empty());
		l.unlock();
		
		EVENTUALLY(5,
			result = pool->getProcessCount() == 1;
		);
		ensure_equals(number, 1);
		ensure(currentSession != NULL);
		ensure(currentException == NULL);
	}
	
	TEST_METHOD(3) {
		// If one matching process already exists and it's not at full
		// capacity then asyncGet() will immediately use it.
		Options options = createOptions();
		
		// Spawn a process and opens a session with it.
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		
		// Close the session so that the process is now idle.
		ProcessPtr process = currentSession->getProcess();
		currentSession.reset();
		ensure_equals(process->usage(), 0);
		ensure(!process->atFullCapacity());
		
		// Verify test assertion.
		ScopedLock l(pool->syncher);
		pool->asyncGet(options, callback, false);
		ensure_equals("callback is immediately called", number, 2);
	}
	
	TEST_METHOD(4) {
		// If one matching process already exists but it's at full capacity,
		// and the limits prevent spawning of a new process,
		// then asyncGet() will put the get action on the group's wait
		// queue. When the process is no longer at full capacity it will
		// process the request.
		
		// Spawn a process and verify that it's at full capacity.
		// Keep its session open.
		Options options = createOptions();
		options.appGroupName = "test";
		pool->setMax(1);
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		SessionPtr session1 = currentSession;
		ProcessPtr process = session1->getProcess();
		currentSession.reset();
		ensure_equals(process->sessions, 1);
		ensure(process->atFullCapacity());
		
		// Now call asyncGet() again.
		pool->asyncGet(options, callback);
		ensure_equals("callback is not yet called", number, 1);
		ensure_equals("the get action has been put on the wait list",
			pool->superGroups.get("test")->defaultGroup->getWaitlist.size(), 1u);
		
		session1.reset();
		ensure_equals("callback is called after the process becomes idle",
			number, 2);
		ensure_equals("the get wait list has been processed",
			pool->superGroups.get("test")->defaultGroup->getWaitlist.size(), 0u);
		ensure_equals(process->sessions, 1);
	}
	
	TEST_METHOD(5) {
		// If one matching process already exists but it's at full capacity,
		// and the limits and pool capacity allow spawning of a new process,
		// then get() will put the get action on the group's wait
		// queue while spawning a process in the background.
		// Either the existing process or the newly spawned process
		// will process the action, whichever becomes first available.
		
		// Here we test the case in which the existing process becomes
		// available first.
		
		// Spawn a regular process and keep its session open.
		Options options = createOptions();
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		SessionPtr session1 = currentSession;
		ProcessPtr process1 = currentSession->getProcess();
		currentSession.reset();
		
		// Now spawn a process that never finishes.
		options.spawnMethod  = "direct";
		options.startCommand = "sleep\1" "60";
		pool->asyncGet(options, callback);
		
		// Release the session on the first process.
		session1.reset();
		
		ensure_equals("The callback should have been called twice now", number, 2);
		ensure_equals("The first process handled the second asyncGet() request",
			currentSession->getProcess(), process1);
	}
	
	TEST_METHOD(6) {
		// Here we test the case in which the new process becomes
		// available first.
		
		// Spawn a regular process.
		Options options = createOptions();
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		SessionPtr session1 = currentSession;
		ProcessPtr process1 = currentSession->getProcess();
		currentSession.reset();
		
		// As long as we don't release process1 the following get
		// action will be processed by the newly spawned process.
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		ensure_equals(number, 2);
		ensure(currentSession->getProcess() != process1);
	}
	
	TEST_METHOD(7) {
		// If multiple matching processes exist, and one of them is idle,
		// then asyncGet() will use that.
		
		// Spawn 3 processes and keep a session open with 1 of them.
		Options options = createOptions();
		options.minProcesses = 3;
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 3;
		);
		SessionPtr session1 = currentSession;
		ProcessPtr process1 = currentSession->getProcess();
		currentSession.reset();
		
		// Now open another session. It should complete immediately
		// and should not use the first process.
		ScopedLock l(pool->syncher);
		pool->asyncGet(options, callback, false);
		ensure_equals("asyncGet() completed immediately", number, 2);
		SessionPtr session2 = currentSession;
		ProcessPtr process2 = currentSession->getProcess();
		l.unlock();
		currentSession.reset();
		ensure(process2 != process1);
		
		// Now open yet another session. It should also complete immediately
		// and should not use the first or the second process.
		l.lock();
		pool->asyncGet(options, callback, false);
		ensure_equals("asyncGet() completed immediately", number, 3);
		SessionPtr session3 = currentSession;
		ProcessPtr process3 = currentSession->getProcess();
		l.unlock();
		currentSession.reset();
		ensure(process3 != process1);
		ensure(process3 != process2);
	}
	
	TEST_METHOD(8) {
		// If multiple matching processes exist, and all of them are at
		// full capacity except one, then asyncGet() will use that.
	}
	
	TEST_METHOD(9) {
		// If multiple matching processes exist, and all of them are at full capacity,
		// and no more processes may be spawned,
		// then asyncGet() will put the action on the group's wait queue.
		// The process that first becomes not at full capacity will process the action.
	}
	
	TEST_METHOD(10) {
		// If multiple matching processes exist, and all of them are at full capacity,
		// and a new process may be spawned,
		// then asyncGet() will put the action on the group's wait queue and spawn the
		// new process.
		// The process that first becomes not at full capacity
		// or the newly spawned process
		// will process the action, whichever is earlier.
	}
	
	
	/*********** Test asyncGet() behavior on multiple SuperGroups,
	             each with a single Group ***********/
	
	TEST_METHOD(20) {
		// If the pool is full, and one tries to asyncGet() from a nonexistant group,
		// then it will kill the oldest idle process and spawn a new process.
	}
	
	TEST_METHOD(21) {
		// If the pool is full, and one tries to asyncGet() from a nonexistant group,
		// and all existing processes are non-idle, then it will
		// kill the oldest process and spawn a new process.
	}
	
	
	/*********** Test detachProcess() ***********/
	
	TEST_METHOD(30) {
		// detachProcess() detaches the process from the group.
	}
	
	TEST_METHOD(31) {
		// If the containing group had waiters on it, and detachProcess()
		// detaches the only process in the group, then a new process
		// is automatically spawned to handle the waiters.
	}
	
	TEST_METHOD(32) {
		// If the pool had waiters on it then detachProcess() will
		// automatically create the SuperGroups that were requested
		// by the waiters.
	}
	
	TEST_METHOD(33) {
		// If the containing SuperGroup becomes garbage collectable after
		// detaching the process, then detachProcess() also detaches the
		// containing SuperGroup.
	}
	
	TEST_METHOD(34) {
		// If the containing SuperGroup becomes garbage collectable after
		// detaching the process, and the pool had waiters on it, then
		// detachProcess() will automatically create the SuperGroups that
		// were requested by the waiters.
	}
	
	
	/*********** Other tests ***********/
	
	TEST_METHOD(40) {
		// The pool is considered to be at full capacity if and only
		// if all SuperGroups are at full capacity.
	}
	
	TEST_METHOD(41) {
		// If the pool is at full capacity, then increasing max will cause
		// new processes to be spawned. Any queued get requests are processed
		// as those new processes become available or as existing processes
		// become available.
	}
	
	TEST_METHOD(42) {
		// getProcessCount() returns the number of processes in the pool.
	}
	
	TEST_METHOD(43) {
		// Each spawned process has a GUPID, which can be looked up
		// through findProcessByGupid().
	}
	
	TEST_METHOD(44) {
		// findProcessByGupid() returns a NULL pointer if there is
		// no matching process.
	}
	
	// Process idle cleaning.
	// Spawner idle cleaning.
	// Process metrics collection.
}
