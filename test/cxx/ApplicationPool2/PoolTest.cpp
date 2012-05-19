#include <TestSupport.h>
#include <ApplicationPool2/Pool.h>
#include <Utils/IOUtils.h>
#include <Utils/json.h>
#include <MessageReadersWriters.h>
#include <map>
#include <vector>
#include <cerrno>
#include <signal.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct ApplicationPool2_PoolTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		BackgroundEventLoop bg;
		SpawnerFactoryPtr spawnerFactory;
		PoolPtr pool;
		GetCallback callback;
		SessionPtr currentSession;
		ExceptionPtr currentException;
		AtomicInt number;
		boost::mutex syncher;
		list<SessionPtr> sessions;
		bool retainSessions;
		
		ApplicationPool2_PoolTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			retainSessions = false;
			spawnerFactory = make_shared<SpawnerFactory>(bg.safe, *resourceLocator, generation);
			pool = make_shared<Pool>(bg.safe.get(), spawnerFactory);
			bg.start();
			callback = boost::bind(&ApplicationPool2_PoolTest::_callback, this, _1, _2);
		}
		
		~ApplicationPool2_PoolTest() {
			// Explicitly destroy these here because they can run
			// additional code that depend on other fields in this
			// class.
			setLogLevel(0);
			pool->destroy();
			pool.reset();
			lock_guard<boost::mutex> l(syncher);
			currentSession.reset();
			sessions.clear();
		}
		
		Options createOptions() {
			Options options;
			options.spawnMethod = "dummy";
			options.appRoot = "stub/rack";
			options.startCommand = "ruby\1" "start.rb";
			options.startupFile  = "start.rb";
			options.loadShellEnvvars = false;
			options.user = testConfig["normal_user_1"].asCString();
			options.defaultUser = testConfig["default_user"].asCString();
			options.defaultGroup = testConfig["default_group"].asCString();
			return options;
		}
		
		void _callback(const SessionPtr &session, const ExceptionPtr &e) {
			SessionPtr oldSession;
			{
				LockGuard l(syncher);
				oldSession = currentSession;
				currentSession = session;
				currentException = e;
				number++;
				if (retainSessions && session != NULL) {
					sessions.push_back(session);
				}
			}
			// destroy old session object outside the lock.
		}

		void sendHeaders(int connection, ...) {
			va_list ap;
			const char *arg;
			vector<StaticString> args;

			va_start(ap, connection);
			while ((arg = va_arg(ap, const char *)) != NULL) {
				args.push_back(StaticString(arg, strlen(arg) + 1));
			}
			va_end(ap);

			shared_array<StaticString> args_array(new StaticString[args.size() + 1]);
			unsigned int totalSize = 0;
			for (unsigned int i = 0; i < args.size(); i++) {
				args_array[i + 1] = args[i];
				totalSize += args[i].size();
			}
			char sizeHeader[sizeof(uint32_t)];
			Uint32Message::generate(sizeHeader, totalSize);
			args_array[0] = StaticString(sizeHeader, sizeof(uint32_t));
			
			gatheredWrite(connection, args_array.get(), args.size() + 1, NULL);
		}

		string stripHeaders(const string &str) {
			string::size_type pos = str.find("\r\n\r\n");
			if (pos == string::npos) {
				return str;
			} else {
				string result = str;
				result.erase(0, pos + 4);
				return result;
			}
		}

		string sendRequest(const Options &options, const char *path) {
			int oldNumber = number;
			pool->asyncGet(options, callback);
			EVENTUALLY(5,
				result = number == oldNumber + 1;
			);
			if (currentException != NULL) {
				P_ERROR("get() exception: " << currentException->what());
				abort();
			}
			currentSession->initiate();
			sendHeaders(currentSession->fd(),
				"PATH_INFO", path,
				"REQUEST_METHOD", "GET",
				NULL);
			shutdown(currentSession->fd(), SHUT_WR);
			string body = stripHeaders(readAll(currentSession->fd()));
			ProcessPtr process = currentSession->getProcess();
			currentSession.reset();
			EVENTUALLY(5,
				result = process->usage() == 0;
			);
			return body;
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
		pool->asyncGet(options, callback);
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

	
	/*********** Test disabling and enabling processes ***********/

	TEST_METHOD(40) {
		// Disabling a process under idle conditions should succeed immediately.
		/*
		Options options = createOptions();
		options.minProcesses = 2;
		options.noop = true;
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);

		options.minProcesses = 0;
		options.noop = false;
		vector<ProcessPtr> processes = pool->getProcesses();
		ensure_equals(processes, );
		*/
	}

	// Disabling the sole process in a group should trigger a new process spawn.
	// Disabling should succeed after the new process has been spawned.

	// Duppose that a previous disable command triggered a new process spawn,
	// and the spawn fails. Then the processes which were marked as 'disabled'
	// should be marked 'enabled' again, and the callbacks for the previous
	// disable commands should be called.

	// asyncGet() should not select a process that's being disabled, unless
	// it's the only process in the group.

	// Disabling a process that's already being disabled should result in the
	// callback being called after disabling is done.

	// Enabling a process that's being disabled should immediately mark the process
	// as being enabled and should call all the queued disable command callbacks.

	// Enabling a process that's disabled works.
	
	
	/*********** Other tests ***********/
	
	TEST_METHOD(50) {
		// The pool is considered to be at full capacity if and only
		// if all SuperGroups are at full capacity.
		Options options = createOptions();
		Options options2 = createOptions();
		options2.appGroupName = "test";

		pool->setMax(2);
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);

		pool->asyncGet(options2, callback);
		EVENTUALLY(5,
			result = number == 2;
		);

		ensure_equals(pool->getProcessCount(), 2u);
		ensure(pool->atFullCapacity());
		pool->detachSuperGroup(pool->getSuperGroup("test"));
		ensure(!pool->atFullCapacity());
	}
	
	TEST_METHOD(51) {
		// If the pool is at full capacity, then increasing 'max' will cause
		// new processes to be spawned. Any queued get requests are processed
		// as those new processes become available or as existing processes
		// become available.
		Options options = createOptions();
		retainSessions = true;
		pool->setMax(1);

		pool->asyncGet(options, callback);
		pool->asyncGet(options, callback);
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);

		pool->setMax(4);
		EVENTUALLY(5,
			result = number == 3;
		);
		ensure_equals(pool->getProcessCount(), 3u);
	}
	
	TEST_METHOD(52) {
		// Each spawned process has a GUPID, which can be looked up
		// through findProcessByGupid().
		Options options = createOptions();
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		string gupid = currentSession->getProcess()->gupid;
		ensure(!gupid.empty());
		ensure_equals(currentSession->getProcess(), pool->findProcessByGupid(gupid));
	}
	
	TEST_METHOD(53) {
		// findProcessByGupid() returns a NULL pointer if there is
		// no matching process.
		ensure(pool->findProcessByGupid("none") == NULL);
	}

	TEST_METHOD(54) {
		// Test process idle cleaning.
		Options options = createOptions();
		retainSessions = true;
		pool->setMaxIdleTime(50000);
		pool->asyncGet(options, callback);
		pool->asyncGet(options, callback);
		EVENTUALLY(2,
			result = number == 2;
		);
		ensure_equals(pool->getProcessCount(), 2u);
		
		currentSession.reset();
		sessions.pop_back();

		// One of the processes still has a session open and should
		// not be idle cleaned.
		EVENTUALLY(2,
			result = pool->getProcessCount() == 1;
		);
		SHOULD_NEVER_HAPPEN(150,
			result = pool->getProcessCount() == 0;
		);

		// It shouldn't clean more processes than minInstances allows.
		sessions.clear();
		SHOULD_NEVER_HAPPEN(150,
			result = pool->getProcessCount() == 0;
		);
	}

	TEST_METHOD(55) {
		// Test spawner idle cleaning.
		Options options = createOptions();
		options.appGroupName = "test1";
		Options options2 = createOptions();
		options2.appGroupName = "test2";

		retainSessions = true;
		pool->setMaxIdleTime(50000);
		pool->asyncGet(options, callback);
		pool->asyncGet(options2, callback);
		EVENTUALLY(2,
			result = number == 2;
		);
		ensure_equals(pool->getProcessCount(), 2u);
		
		EVENTUALLY(2,
			SpawnerPtr spawner = pool->getSuperGroup("test1")->defaultGroup->spawner;
			result = static_pointer_cast<DummySpawner>(spawner)->cleanCount >= 1;
		);
		EVENTUALLY(2,
			SpawnerPtr spawner = pool->getSuperGroup("test2")->defaultGroup->spawner;
			result = static_pointer_cast<DummySpawner>(spawner)->cleanCount >= 1;
		);
	}

	TEST_METHOD(56) {
		// It should restart the app if restart.txt is created or updated.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.spawnMethod = "direct";
		ProcessPtr process;
		pool->setMax(1);

		// Send normal request.
		ensure_equals(sendRequest(options, "/"), "hello <b>world</b>");

		// Modify application; it shouldn't have effect yet.
		writeFile("tmp.wsgi/passenger_wsgi.py",
			"def application(env, start_response):\n"
			"	start_response('200 OK', [('Content-Type', 'text/html')])\n"
			"	return ['restarted']\n");
		ensure_equals(sendRequest(options, "/"), "hello <b>world</b>");

		// Create restart.txt and send request again. The change should now be activated.
		touchFile("tmp.wsgi/tmp/restart.txt", 1);
		ensure_equals(sendRequest(options, "/"), "restarted");

		// Modify application again; it shouldn't have effect yet.
		writeFile("tmp.wsgi/passenger_wsgi.py",
			"def application(env, start_response):\n"
			"	start_response('200 OK', [('Content-Type', 'text/html')])\n"
			"	return ['restarted 2']\n");
		ensure_equals(sendRequest(options, "/"), "restarted");

		// Touch restart.txt and send request again. The change should now be activated.
		touchFile("tmp.wsgi/tmp/restart.txt", 2);
		ensure_equals(sendRequest(options, "/"), "restarted 2");
	}

	TEST_METHOD(57) {
		// Test spawn exceptions.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.spawnMethod = "direct";
		spawnerFactory->forwardStderr = false;

		writeFile("tmp.wsgi/passenger_wsgi.py",
			"import sys\n"
			"sys.stderr.write('Something went wrong!')\n"
			"exit(1)\n");
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);

		ensure(currentException != NULL);
		shared_ptr<SpawnException> e = dynamic_pointer_cast<SpawnException>(currentException);
		ensure_equals(e->getErrorPage(), "Something went wrong!");
	}

	TEST_METHOD(58) {
		// If a process fails to spawn, then it stops trying to spawn minProcesses processes.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.spawnMethod = "direct";
		options.minProcesses = 4;
		spawnerFactory->forwardStderr = false;

		writeFile("tmp.wsgi/counter", "0");
		// Our application starts successfully the first two times,
		// and fails all the other times.
		writeFile("tmp.wsgi/passenger_wsgi.py",
			"import sys\n"

			"def application(env, start_response):\n"
			"	pass\n"

			"counter = int(open('counter', 'r').read())\n"
			"f = open('counter', 'w')\n"
			"f.write(str(counter + 1))\n"
			"f.close()\n"
			"if counter >= 2:\n"
			"	sys.stderr.write('Something went wrong!')\n"
			"	exit(1)\n");

		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		EVENTUALLY(2,
			result = !pool->getSuperGroup("tmp.wsgi")->defaultGroup->spawning();
		);
		SHOULD_NEVER_HAPPEN(500,
			result = pool->getProcessCount() > 2;
		);
	}

	TEST_METHOD(59) {
		// It removes the process from the pool if session->initiate() fails.
		Options options = createOptions();
		options.appRoot = "stub/wsgi";
		options.appType = "wsgi";
		options.spawnMethod = "direct";

		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		pid_t pid = currentSession->getPid();
		
		kill(pid, SIGTERM);
		// Wait until process is gone.
		EVENTUALLY(5,
			result = kill(pid, 0) == -1 && (errno == ESRCH || errno == EPERM || errno == ECHILD);
		);

		try {
			currentSession->initiate();
			fail("Initiate is supposed to fail");
		} catch (const SystemException &e) {
			ensure_equals(e.code(), ECONNREFUSED);
		}
		ensure_equals(pool->getProcessCount(), 0u);
	}

	TEST_METHOD(60) {
		// When a process has become idle, and there are waiters on the pool,
		// consider detaching it in order to satisfy a waiter.
		Options options1 = createOptions();
		Options options2 = createOptions();
		options2.appRoot = "stub/wsgi";
		options2.allowTrashingNonIdleProcesses = false;

		retainSessions = true;
		pool->setMax(2);
		pool->asyncGet(options1, callback);
		pool->asyncGet(options1, callback);
		EVENTUALLY(3,
			result = pool->getProcessCount() == 2;
		);
		pool->asyncGet(options2, callback);
		ensure_equals(pool->getWaitlist.size(), 1u);
		ensure_equals(number, 2);

		currentSession.reset();
		sessions.pop_front();
		EVENTUALLY(3,
			result = number == 3;
		);
		ensure_equals(pool->getProcessCount(), 2u);
		SuperGroupPtr superGroup1 = pool->superGroups.get("stub/rack");
		SuperGroupPtr superGroup2 = pool->superGroups.get("stub/rack");
		ensure_equals(superGroup1->defaultGroup->count, 1);
		ensure_equals(superGroup2->defaultGroup->count, 1);
	}

	TEST_METHOD(61) {
		// A process is detached after processing maxRequests sessions.
		{
			Ticket ticket;
			Options options = createOptions();
			options.maxRequests = 5;
			pool->get(options, &ticket).reset();

			vector<ProcessPtr> processes = pool->getProcesses();
			ensure_equals(processes.size(), 1u);
			pid_t origPid = processes[0]->pid;

			for (int i = 0; i < 3; i++) {
				pool->get(options, &ticket).reset();
				processes = pool->getProcesses();
				ensure_equals(processes.size(), 1u);
				ensure_equals(processes[0]->pid, origPid);
			}

			pool->get(options, &ticket).reset();
		}
		ensure_equals(pool->getProcessCount(), 0u);
	}

	TEST_METHOD(62) {
		// If we restart while spawning is in progress, then the spawn
		// loop will exit as soon as it has detected that we're restarting.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		spawnerFactory->dummySpawnTime = 20000;
		spawnerFactory->dummySpawnerCreationSleepTime = 100000;

		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.minProcesses = 3;

		// Trigger spawn loop. The spawn loop itself won't take longer than 3*20=60 msec.
		pool->findOrCreateGroup(options);
		ScopedLock l(pool->syncher);
		pool->asyncGet(options, callback, false);
		// Wait until spawn loop tries to grab the lock.
		EVENTUALLY(2,
			LockGuard l2(pool->debugSyncher);
			result = pool->spawnLoopIteration == 1;
		);
		l.unlock();

		// At this point, the spawn loop is about to attach its first spawned
		// process to the group. We wait until it has succeeded doing so.
		// Remaining maximum time in the spawn loop: 2*20=40 msec.
		EVENTUALLY2(200, 0,
			result = pool->getProcessCount() == 1;
		);

		// Trigger restart. It will immediately detach the sole process in the pool,
		// and it will finish after approximately 100 msec,
		// allowing the spawn loop to detect that the restart flag is true.
		touchFile("tmp.wsgi/tmp/restart.txt");
		pool->asyncGet(options, callback);
		ensure_equals("(1)", pool->getProcessCount(), 0u);

		// The spawn loop will succeed at spawning the second process.
		// Upon attaching it, it should detect the restart the stop,
		// so that it never spawns the third process.
		SHOULD_NEVER_HAPPEN(300,
			LockGuard l2(pool->debugSyncher);
			result = pool->spawnLoopIteration > 2;
		);
		ensure_equals("(2)", pool->getProcessCount(), 1u);
	}

	TEST_METHOD(63) {
		// If a get() request comes in while the restart is in progress, then
		// that get() request will be put into the get waiters list, which will
		// be processed after spawning is done.

		// Spawn 2 processes.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.minProcesses = 2;
		pool->asyncGet(options, callback);
		EVENTUALLY(2,
			result = pool->getProcessCount() == 2;
		);
		
		// Trigger a restart. The creation of the new spawner should take a while.
		spawnerFactory->dummySpawnerCreationSleepTime = 20000;
		touchFile("tmp.wsgi/tmp/restart.txt");
		pool->asyncGet(options, callback);
		GroupPtr group = pool->findOrCreateGroup(options);
		ensure_equals(pool->getProcessCount(), 0u);
		ensure_equals(group->getWaitlist.size(), 1u);

		// Now that the restart is in progress, perform a get().
		pool->asyncGet(options, callback);
		ensure_equals(group->getWaitlist.size(), 2u);
		EVENTUALLY(2,
			result = number == 3;
		);
		ensure_equals("The restart function respects minProcesses",
			pool->getProcessCount(), 2u);
	}

	// Process metrics collection.
	// Persistent connections.

	// If a process fails to spawn, it sends a SpawnException result to all get waiters.
	// If a process fails to spawn, the existing processes are kept alive and continue to be able to serve requests.
	// If one closes the session before it has reached EOF, and process's maximum concurrency
	// has already been reached, then the pool should ping the process so that it can detect
	// when the session's connection has been released by the app.

	/*****************************/
}
