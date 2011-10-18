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
		SpawnerPtr spawner = process1->getGroup()->spawner;
		dynamic_pointer_cast<DummySpawner>(spawner)->spawnTime = 5000000;
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
		// If multiple matching processes exist, then asyncGet() will use
		// the one with the smallest usage number.
		
		// Spawn 2 processes, each with a concurrency of 2.
		Options options = createOptions();
		options.minProcesses = 2;
		pool->setMax(2);
		GroupPtr group = pool->findOrCreateGroup(options);
		dynamic_pointer_cast<DummySpawner>(group->spawner)->concurrency = 2;
		{
			LockGuard l(pool->syncher);
			group->spawn();
		}
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		
		// asyncGet() selects some process.
		pool->asyncGet(options, callback);
		ensure_equals(number, 1);
		SessionPtr session1 = currentSession;
		ProcessPtr process1 = currentSession->getProcess();
		currentSession.reset();
		
		// The first process now has 1 session, so next asyncGet() should
		// select the other process.
		pool->asyncGet(options, callback);
		ensure_equals(number, 2);
		SessionPtr session2 = currentSession;
		ProcessPtr process2 = currentSession->getProcess();
		currentSession.reset();
		ensure("(1)", process1 != process2);
		
		// Both processes now have an equal number of sessions. Next asyncGet()
		// can select either.
		pool->asyncGet(options, callback);
		ensure_equals(number, 3);
		SessionPtr session3 = currentSession;
		ProcessPtr process3 = currentSession->getProcess();
		currentSession.reset();
		
		// One process now has the lowest number of sessions. Next
		// asyncGet() should select that one.
		pool->asyncGet(options, callback);
		ensure_equals(number, 4);
		SessionPtr session4 = currentSession;
		ProcessPtr process4 = currentSession->getProcess();
		currentSession.reset();
		ensure(process3 != process4);
	}
	
	TEST_METHOD(9) {
		// If multiple matching processes exist, and all of them are at full capacity,
		// and no more processes may be spawned,
		// then asyncGet() will put the action on the group's wait queue.
		// The process that first becomes not at full capacity will process the action.
		
		// Spawn 2 processes and open 4 sessions.
		Options options = createOptions();
		options.appGroupName = "test";
		options.minProcesses = 2;
		pool->setMax(2);
		GroupPtr group = pool->findOrCreateGroup(options);
		dynamic_pointer_cast<DummySpawner>(group->spawner)->concurrency = 2;
		
		vector<SessionPtr> sessions;
		int expectedNumber = 1;
		for (int i = 0; i < 4; i++) {
			pool->asyncGet(options, callback);
			EVENTUALLY(5,
				result = number == expectedNumber;
			);
			expectedNumber++;
			sessions.push_back(currentSession);
			currentSession.reset();
		}
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		
		SuperGroupPtr superGroup = pool->superGroups.get("test");
		ensure_equals(superGroup->groups[0]->getWaitlist.size(), 0u);
		ensure(pool->atFullCapacity());
		
		// Now try to open another session.
		pool->asyncGet(options, callback);
		ensure_equals("The get request has been put on the wait list",
			pool->superGroups.get("test")->groups[0]->getWaitlist.size(), 1u);
		
		// Close an existing session so that one process is no
		// longer at full capacity.
		sessions[0].reset();
		ensure_equals("The get request has been removed from the wait list",
			pool->superGroups.get("test")->groups[0]->getWaitlist.size(), 0u);
		ensure(pool->atFullCapacity());
	}
	
	TEST_METHOD(10) {
		// If multiple matching processes exist, and all of them are at full capacity,
		// and a new process may be spawned,
		// then asyncGet() will put the action on the group's wait queue and spawn the
		// new process.
		// The process that first becomes not at full capacity
		// or the newly spawned process
		// will process the action, whichever is earlier.
		// Here we test the case where an existing process is earlier.
		
		// Spawn 2 processes and open 4 sessions.
		Options options = createOptions();
		options.minProcesses = 2;
		pool->setMax(3);
		GroupPtr group = pool->findOrCreateGroup(options);
		dynamic_pointer_cast<DummySpawner>(group->spawner)->concurrency = 2;
		
		vector<SessionPtr> sessions;
		int expectedNumber = 1;
		for (int i = 0; i < 4; i++) {
			pool->asyncGet(options, callback);
			EVENTUALLY(5,
				result = number == expectedNumber;
			);
			expectedNumber++;
			sessions.push_back(currentSession);
			currentSession.reset();
		}
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		
		// The next asyncGet() should spawn a new process and the action should be queued.
		ScopedLock l(pool->syncher);
		dynamic_pointer_cast<DummySpawner>(group->spawner)->spawnTime = 5000000;
		pool->asyncGet(options, callback, false);
		ensure(group->spawning());
		ensure_equals(group->getWaitlist.size(), 1u);
		l.unlock();
		
		// Close one of the sessions. Now it will process the action.
		ProcessPtr process = sessions[0]->getProcess();
		sessions[0].reset();
		ensure_equals(number, 5);
		ensure_equals(currentSession->getProcess(), process);
		ensure_equals(group->getWaitlist.size(), 0u);
		ensure_equals(pool->getProcessCount(), 2u);
	}
	
	TEST_METHOD(11) {
		// Here we test the case where the newly spawned process is earlier.
		
		// Spawn 2 processes and open 4 sessions.
		Options options = createOptions();
		options.minProcesses = 2;
		pool->setMax(3);
		GroupPtr group = pool->findOrCreateGroup(options);
		dynamic_pointer_cast<DummySpawner>(group->spawner)->concurrency = 2;
		
		vector<SessionPtr> sessions;
		int expectedNumber = 1;
		for (int i = 0; i < 4; i++) {
			pool->asyncGet(options, callback);
			EVENTUALLY(5,
				result = number == expectedNumber;
			);
			expectedNumber++;
			sessions.push_back(currentSession);
			currentSession.reset();
		}
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		
		// The next asyncGet() should spawn a new process. After it's done
		// spawning it will process the action.
		pool->asyncGet(options, callback, false);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 3;
		);
		EVENTUALLY(5,
			result = number == 5;
		);
		ensure_equals(currentSession->getProcess()->pid, 3);
		ensure_equals(group->getWaitlist.size(), 0u);
	}
	
	
	/*********** Test asyncGet() behavior on multiple SuperGroups,
	             each with a single Group ***********/
	
	TEST_METHOD(20) {
		// If the pool is full, and one tries to asyncGet() from a nonexistant group,
		// then it will kill the oldest idle process and spawn a new process.
		Options options = createOptions();
		pool->setMax(2);
		
		// Get from /foo and close its session immediately.
		options.appRoot = "/foo";
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		ProcessPtr process1 = currentSession->getProcess();
		GroupPtr group1 = process1->getGroup();
		SuperGroupPtr superGroup1 = group1->getSuperGroup();
		currentSession.reset();
		
		// Get from /bar and keep its session open.
		options.appRoot = "/bar";
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 2;
		);
		SessionPtr session2 = currentSession;
		currentSession.reset();
		
		// Get from /baz. The process for /foo should be killed now.
		options.appRoot = "/baz";
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 3;
		);
		
		ensure_equals(pool->getProcessCount(), 2u);
		ensure(superGroup1->detached());
	}
	
	TEST_METHOD(21) {
		// If the pool is full, and one tries to asyncGet() from a nonexistant group,
		// and all existing processes are non-idle, then it will
		// kill the oldest process and spawn a new process.
		Options options = createOptions();
		pool->setMax(2);
		
		// Get from /foo and close its session immediately.
		options.appRoot = "/foo";
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		SessionPtr session1 = currentSession;
		ProcessPtr process1 = currentSession->getProcess();
		GroupPtr group1 = process1->getGroup();
		SuperGroupPtr superGroup1 = group1->getSuperGroup();
		
		// Get from /bar and keep its session open.
		options.appRoot = "/bar";
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 2;
		);
		SessionPtr session2 = currentSession;
		currentSession.reset();
		
		// Get from /baz. The process for /foo should be killed now.
		options.appRoot = "/baz";
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 3;
		);
		
		ensure_equals(pool->getProcessCount(), 2u);
		ensure(superGroup1->detached());
	}
	
	
	/*********** Test detachProcess() ***********/
	
	TEST_METHOD(30) {
		// detachProcess() detaches the process from the group.
		Options options = createOptions();
		options.appGroupName = "test";
		options.minProcesses = 2;
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		EVENTUALLY(5,
			result = number == 1;
		);

		pool->detachProcess(currentSession->getProcess());
		ensure(currentSession->getProcess()->detached());
		LockGuard l(pool->syncher);
		ensure_equals(pool->superGroups.get("test")->defaultGroup->count, 1);
	}
	
	TEST_METHOD(31) {
		// If the containing group had waiters on it, and detachProcess()
		// detaches the only process in the group, then a new process
		// is automatically spawned to handle the waiters.
		Options options = createOptions();
		options.appGroupName = "test";
		pool->setMax(1);
		pool->spawnerFactory->dummySpawnTime = 1000000;

		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		SessionPtr session1 = currentSession;
		currentSession.reset();

		pool->asyncGet(options, callback);
		
		{
			LockGuard l(pool->syncher);
			ensure_equals(pool->superGroups.get("test")->defaultGroup->getWaitlist.size(), 1u);
		}

		pool->detachProcess(session1->getProcess());
		{
			LockGuard l(pool->syncher);
			ensure(pool->superGroups.get("test")->defaultGroup->spawning());
			ensure_equals(pool->superGroups.get("test")->defaultGroup->count, 0);
			ensure_equals(pool->superGroups.get("test")->defaultGroup->getWaitlist.size(), 1u);
		}
	}
	
	TEST_METHOD(32) {
		// If the pool had waiters on it then detachProcess() will
		// automatically create the SuperGroups that were requested
		// by the waiters.
		Options options = createOptions();
		options.appGroupName = "test";
		pool->setMax(1);
		pool->spawnerFactory->dummySpawnTime = 30000;

		// Begin spawning a process.
		pool->asyncGet(options, callback);
		ensure(pool->atFullCapacity());

		// asyncGet() on another group should now put it on the waiting list.
		Options options2 = createOptions();
		options2.appGroupName = "test2";
		pool->spawnerFactory->dummySpawnTime = 90000;
		pool->asyncGet(options2, callback);
		{
			LockGuard l(pool->syncher);
			ensure_equals(pool->getWaitlist.size(), 1u);
		}

		// Eventually the dummy process for "test" is now done spawning.
		// We then detach it.
		EVENTUALLY(5,
			result = number == 1;
		);
		SessionPtr session1 = currentSession;
		currentSession.reset();
		pool->detachProcess(session1->getProcess());
		{
			LockGuard l(pool->syncher);
			ensure(pool->superGroups.get("test2") != NULL);
			ensure_equals(pool->getWaitlist.size(), 0u);
		}
	}
	
	TEST_METHOD(33) {
		// If the containing SuperGroup becomes garbage collectable after
		// detaching the process, then detachProcess() also detaches the
		// containing SuperGroup.
		Options options = createOptions();
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		ProcessPtr process = currentSession->getProcess();
		currentSession.reset();
		pool->detachProcess(process);
		LockGuard l(pool->syncher);
		ensure(pool->superGroups.empty());
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
		// Each spawned process has a GUPID, which can be looked up
		// through findProcessByGupid().
	}
	
	TEST_METHOD(43) {
		// findProcessByGupid() returns a NULL pointer if there is
		// no matching process.
	}
	
	// Process idle cleaning.
	// Spawner idle cleaning.
	// Process metrics collection.
	// Restarting.
	// Spawn exceptions.
	// Died processes.
	// Persistent connections.
	// Temporarily disabling a process.
	// When a process has become idle, and there are waiters on the pool, consider detaching it in order to satisfy a waiter.
}
