#include <TestSupport.h>
#include <ApplicationPool2/Pool.h>
#include <Utils/IOUtils.h>
#include <Utils/StrIntUtils.h>
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
		SpawnerConfigPtr spawnerConfig;
		SpawnerFactoryPtr spawnerFactory;
		PoolPtr pool;
		Pool::DebugSupportPtr debug;
		Ticket ticket;
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
			spawnerConfig = boost::make_shared<SpawnerConfig>(*resourceLocator);
			spawnerFactory = boost::make_shared<SpawnerFactory>(generation,
				spawnerConfig);
			pool = boost::make_shared<Pool>(spawnerFactory);
			pool->initialize();
			callback = boost::bind(&ApplicationPool2_PoolTest::_callback, this, _1, _2);
			setLogLevel(LVL_ERROR); // TODO: change to LVL_WARN
			setPrintAppOutputAsDebuggingMessages(true);
		}

		~ApplicationPool2_PoolTest() {
			// Explicitly destroy these here because they can run
			// additional code that depend on other fields in this
			// class.
			TRACE_POINT();
			clearAllSessions();
			UPDATE_TRACE_POINT();
			pool->destroy();
			UPDATE_TRACE_POINT();
			pool.reset();
			setLogLevel(DEFAULT_LOG_LEVEL);
			setPrintAppOutputAsDebuggingMessages(false);
			SystemTime::releaseAll();
		}

		void initPoolDebugging() {
			pool->initDebugging();
			debug = pool->debugSupport;
		}

		void clearAllSessions() {
			SessionPtr myCurrentSession;
			list<SessionPtr> mySessions;
			{
				LockGuard l(syncher);
				myCurrentSession = currentSession;
				mySessions = sessions;
				currentSession.reset();
				sessions.clear();
			}
			myCurrentSession.reset();
			mySessions.clear();
		}

		Options createOptions() {
			Options options;
			options.spawnMethod = "dummy";
			options.appRoot = "stub/rack";
			options.startCommand = "ruby\t" "start.rb";
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
				result = process->busyness() == 0;
			);
			return body;
		}

		// Ensure that n processes exist.
		Options ensureMinProcesses(unsigned int n) {
			Options options = createOptions();
			options.minProcesses = n;
			pool->asyncGet(options, callback);
			EVENTUALLY(5,
				result = number == 1;
			);
			EVENTUALLY(5,
				result = pool->getProcessCount() == n;
			);
			currentSession.reset();
			return options;
		}

		void disableProcess(ProcessPtr process, AtomicInt *result) {
			*result = (int) pool->disableProcess(process->gupid);
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
		ensure_equals(process->busyness(), 0);
		ensure(!process->isTotallyBusy());

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
		ensure(process->isTotallyBusy());

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
		// If one matching process already exists but it's at full utilization,
		// and the limits and pool capacity allow spawning of a new process,
		// then get() will put the get action on the group's wait
		// queue while spawning a process in the background.
		// Either the existing process or the newly spawned process
		// will process the action, whichever becomes first available.

		// Here we test the case in which the existing process becomes
		// available first.
		initPoolDebugging();

		// Spawn a regular process and keep its session open.
		Options options = createOptions();
		debug->messages->send("Proceed with spawn loop iteration 1");
		SessionPtr session1 = pool->get(options, &ticket);
		ProcessPtr process1 = session1->getProcess();

		// Now spawn a process that never finishes.
		pool->asyncGet(options, callback);

		// Release the session on the first process.
		session1.reset();

		EVENTUALLY(1,
			result = number == 1;
		);
		ensure_equals("The first process handled the second asyncGet() request",
			currentSession->getProcess(), process1);

		debug->messages->send("Proceed with spawn loop iteration 2");
		EVENTUALLY(5,
			result = number == 1;
		);
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
		// the one with the smallest utilization number.

		// Spawn 2 processes, each with a concurrency of 2.
		Options options = createOptions();
		options.minProcesses = 2;
		pool->setMax(2);
		GroupPtr group = pool->findOrCreateGroup(options);
		spawnerConfig->concurrency = 2;
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
		spawnerConfig->concurrency = 2;

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
		// longer at full utilization.
		sessions[0].reset();
		ensure_equals("The get request has been removed from the wait list",
			pool->superGroups.get("test")->groups[0]->getWaitlist.size(), 0u);
		ensure(pool->atFullCapacity());
	}

	TEST_METHOD(10) {
		// If multiple matching processes exist, and all of them are at full utilization,
		// and a new process may be spawned,
		// then asyncGet() will put the action on the group's wait queue and spawn the
		// new process.
		// The process that first becomes not at full utilization
		// or the newly spawned process
		// will process the action, whichever is earlier.
		// Here we test the case where an existing process is earlier.

		// Spawn 2 processes and open 4 sessions.
		Options options = createOptions();
		options.minProcesses = 2;
		pool->setMax(3);
		GroupPtr group = pool->findOrCreateGroup(options);
		spawnerConfig->concurrency = 2;

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
		spawnerConfig->spawnTime = 5000000;
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
		spawnerConfig->concurrency = 2;

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

	TEST_METHOD(12) {
		// Test shutting down.
		ensureMinProcesses(2);
		ensure(pool->detachSuperGroupByName("stub/rack"));
		ensure_equals(pool->getSuperGroupCount(), 0u);
	}

	TEST_METHOD(13) {
		// Test shutting down while Group is restarting.
		initPoolDebugging();
		debug->messages->send("Proceed with spawn loop iteration 1");
		ensureMinProcesses(1);

		ensure(pool->restartGroupByName("stub/rack#default"));
		debug->debugger->recv("About to end restarting");
		ensure(pool->detachSuperGroupByName("stub/rack"));
		ensure_equals(pool->getSuperGroupCount(), 0u);
	}

	TEST_METHOD(14) {
		// Test shutting down while Group is spawning.
		initPoolDebugging();
		Options options = createOptions();

		pool->asyncGet(options, callback);
		debug->debugger->recv("Begin spawn loop iteration 1");
		ensure(pool->detachSuperGroupByName("stub/rack"));
		ensure_equals(pool->getSuperGroupCount(), 0u);
	}

	TEST_METHOD(15) {
		// Test shutting down while SuperGroup is initializing.
		initPoolDebugging();
		debug->spawning = false;
		debug->superGroup = true;
		Options options = createOptions();

		pool->asyncGet(options, callback);
		debug->debugger->recv("About to finish SuperGroup initialization");
		ensure(pool->detachSuperGroupByName("stub/rack"));
		ensure_equals(pool->getSuperGroupCount(), 0u);
	}

	TEST_METHOD(16) {
		// Test shutting down while SuperGroup is restarting.
		initPoolDebugging();
		debug->spawning = false;
		debug->superGroup = true;
		debug->messages->send("Proceed with initializing SuperGroup");
		ensureMinProcesses(1);

		ensure_equals(pool->restartSuperGroupsByAppRoot("stub/rack"), 1u);
		debug->debugger->recv("About to finish SuperGroup restart");
		ensure(pool->detachSuperGroupByName("stub/rack"));
		ensure_equals(pool->getSuperGroupCount(), 0u);
	}

	TEST_METHOD(17) {
		// Test that restartGroupByName() spawns more processes to ensure
		// that minProcesses and other constraints are met.
		ensureMinProcesses(1);
		ensure(pool->restartGroupByName("stub/rack#default"));
		EVENTUALLY(5,
			result = pool->getProcessCount() == 1;
		);
	}

	TEST_METHOD(18) {
		// Test getting from an app for which minProcesses is set to 0,
		// and restart.txt already existed.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";
		options.minProcesses = 0;
		initPoolDebugging();
		debug->spawning = false;

		touchFile("tmp.wsgi/tmp/restart.txt", 1);
		pool->asyncGet(options, callback, false);
		debug->debugger->recv("About to end restarting");
		debug->messages->send("Finish restarting");
		EVENTUALLY(5,
			result = number == 1;
		);
		ensure_equals(pool->getProcessCount(), 1u);
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
		ensure_equals(superGroup1->getProcessCount(), 0u);
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
		ensure_equals(superGroup1->getProcessCount(), 0u);
	}

	TEST_METHOD(22) {
		// Suppose the pool is at full capacity, and one tries to asyncGet() from an
		// existant group that does not have any processes. It should kill a process
		// from another group, and the request should succeed.
		Options options = createOptions();
		SessionPtr session;
		pid_t pid1, pid2;
		pool->setMax(1);

		// Create a group /foo.
		options.appRoot = "/foo";
		SystemTime::force(1);
		session = pool->get(options, &ticket);
		pid1 = session->getPid();
		session.reset();

		// Create a group /bar.
		options.appRoot = "/bar";
		SystemTime::force(2);
		session = pool->get(options, &ticket);
		pid2 = session->getPid();
		session.reset();

		// Sleep for a short while to give Pool a chance to shutdown
		// the first process.
		usleep(300000);
		ensure_equals("(1)", pool->getProcessCount(), 1u);

		// Get from /foo.
		options.appRoot = "/foo";
		SystemTime::force(3);
		session = pool->get(options, &ticket);
		ensure("(2)", session->getPid() != pid1);
		ensure("(3)", session->getPid() != pid2);
		ensure_equals("(4)", pool->getProcessCount(), 1u);
	}

	TEST_METHOD(23) {
		// Suppose the pool is at full capacity, and one tries to asyncGet() from an
		// existant group that does not have any processes, and that happens to need
		// restarting. It should kill a process from another group and the request
		// should succeed.
		Options options1 = createOptions();
		Options options2 = createOptions();
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		SessionPtr session;
		pid_t pid1, pid2;
		pool->setMax(1);

		// Create a group tmp.wsgi.
		options1.appRoot = "tmp.wsgi";
		options1.appType = "wsgi";
		options1.startupFile = "passenger_wsgi.py";
		options1.spawnMethod = "direct";
		SystemTime::force(1);
		session = pool->get(options1, &ticket);
		pid1 = session->getPid();
		session.reset();

		// Create a group bar.
		options2.appRoot = "bar";
		SystemTime::force(2);
		session = pool->get(options2, &ticket);
		pid2 = session->getPid();
		session.reset();

		// Sleep for a short while to give Pool a chance to shutdown
		// the first process.
		usleep(300000);
		ensure_equals("(1)", pool->getProcessCount(), 1u);

		// Get from tmp.wsgi.
		SystemTime::force(3);
		touchFile("tmp.wsgi/tmp/restart.txt", 4);
		session = pool->get(options1, &ticket);
		ensure("(2)", session->getPid() != pid1);
		ensure("(3)", session->getPid() != pid2);
		ensure_equals("(4)", pool->getProcessCount(), 1u);
	}

	TEST_METHOD(24) {
		// Suppose the pool is at full capacity, with two groups:
		// - one that is spawning a process.
		// - one with no processes.
		// When one tries to asyncGet() from the second group, there should
		// be no process to kill, but when the first group is done spawning
		// it should throw away that process immediately to allow the second
		// group to spawn.
		Options options1 = createOptions();
		Options options2 = createOptions();
		initPoolDebugging();
		debug->restarting = false;
		pool->setMax(1);

		// Create a group foo.
		options1.appRoot = "foo";
		options1.noop = true;
		SystemTime::force(1);
		pool->get(options1, &ticket);

		// Create a group bar, but don't let it finish spawning.
		options2.appRoot = "bar";
		options2.noop = true;
		SystemTime::force(2);
		GroupPtr barGroup = pool->get(options2, &ticket)->getGroup();
		{
			LockGuard l(pool->syncher);
			ensure_equals("(1)", barGroup->spawn(), SR_OK);
		}
		debug->debugger->recv("Begin spawn loop iteration 1");

		// Now get from foo again and let the request be queued.
		options1.noop = false;
		SystemTime::force(3);
		pool->asyncGet(options1, callback);

		// Nothing should happen while bar is spawning.
		SHOULD_NEVER_HAPPEN(100,
			result = number > 0;
		);
		ensure_equals("(2)", pool->getProcessCount(), 0u);

		// Now let bar finish spawning. Eventually there should
		// only be one process: the one for foo.
		debug->messages->send("Proceed with spawn loop iteration 1");
		debug->debugger->recv("Spawn loop done");
		debug->messages->send("Proceed with spawn loop iteration 2");
		debug->debugger->recv("Spawn loop done");
		EVENTUALLY(5,
			LockGuard l(pool->syncher);
			vector<ProcessPtr> processes = pool->getProcesses(false);
			if (processes.size() == 1) {
				GroupPtr group = processes[0]->getGroup();
				result = group->name == "foo#default";
			} else {
				result = false;
			}
		);
	}

	TEST_METHOD(25) {
		// Suppose the pool is at full capacity, with two groups:
		// - one that is spawning a process, and has a queued request.
		// - one with no processes.
		// When one tries to asyncGet() from the second group, there should
		// be no process to kill, but when the first group is done spawning
		// it should throw away that process immediately to allow the second
		// group to spawn.
		Options options1 = createOptions();
		Options options2 = createOptions();
		initPoolDebugging();
		debug->restarting = false;
		pool->setMax(1);

		// Create a group foo.
		options1.appRoot = "foo";
		options1.noop = true;
		SystemTime::force(1);
		pool->get(options1, &ticket);

		// Create a group bar with a queued request, but don't let it finish spawning.
		options2.appRoot = "bar";
		SystemTime::force(2);
		pool->asyncGet(options2, callback);
		debug->debugger->recv("Begin spawn loop iteration 1");

		// Now get from foo again and let the request be queued.
		options1.noop = false;
		SystemTime::force(3);
		pool->asyncGet(options1, callback);

		// Nothing should happen while bar is spawning.
		SHOULD_NEVER_HAPPEN(100,
			result = number > 0;
		);
		ensure_equals("(1)", pool->getProcessCount(), 0u);

		// Now let bar finish spawning. The request for bar should be served.
		debug->messages->send("Proceed with spawn loop iteration 1");
		debug->debugger->recv("Spawn loop done");
		EVENTUALLY(5,
			result = number == 1;
		);
		ensure_equals(currentSession->getGroup()->name, "bar#default");

		// When that request is done, the process for bar should be killed,
		// and a process for foo should be spawned.
		currentSession.reset();
		debug->messages->send("Proceed with spawn loop iteration 2");
		debug->debugger->recv("Spawn loop done");
		EVENTUALLY(5,
			LockGuard l(pool->syncher);
			vector<ProcessPtr> processes = pool->getProcesses(false);
			if (processes.size() == 1) {
				GroupPtr group = processes[0]->getGroup();
				result = group->name == "foo#default";
			} else {
				result = false;
			}
		);

		EVENTUALLY(5,
			result = number == 2;
		);
	}


	/*********** Test detachProcess() ***********/

	TEST_METHOD(30) {
		// detachProcess() detaches the process from the group. The pool
		// will restore the minimum number of processes afterwards.
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

		ProcessPtr process = currentSession->getProcess();
		pool->detachProcess(currentSession->getProcess());
		{
			LockGuard l(pool->syncher);
			ensure(process->enabled == Process::DETACHED);
		}
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		currentSession.reset();
		EVENTUALLY(5,
			result = process->isDead();
		);
	}

	TEST_METHOD(31) {
		// If the containing group had waiters on it, and detachProcess()
		// detaches the only process in the group, then a new process
		// is automatically spawned to handle the waiters.
		Options options = createOptions();
		options.appGroupName = "test";
		pool->setMax(1);
		spawnerConfig->spawnTime = 1000000;

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
			ensure_equals(pool->superGroups.get("test")->defaultGroup->enabledCount, 0);
			ensure_equals(pool->superGroups.get("test")->defaultGroup->getWaitlist.size(), 1u);
		}

		EVENTUALLY(5,
			result = number == 2;
		);
	}

	TEST_METHOD(32) {
		// If the pool had waiters on it then detachProcess() will
		// automatically create the SuperGroups that were requested
		// by the waiters.
		Options options = createOptions();
		options.appGroupName = "test";
		options.minProcesses = 0;
		pool->setMax(1);
		spawnerConfig->spawnTime = 30000;

		// Begin spawning a process.
		pool->asyncGet(options, callback);
		ensure(pool->atFullCapacity());

		// asyncGet() on another group should now put it on the waiting list.
		Options options2 = createOptions();
		options2.appGroupName = "test2";
		options2.minProcesses = 0;
		spawnerConfig->spawnTime = 90000;
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
		EVENTUALLY(5,
			result = number == 2;
		);
	}

	TEST_METHOD(33) {
		// A SuperGroup does not become garbage collectable
		// after detaching all its processes.
		Options options = createOptions();
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		ProcessPtr process = currentSession->getProcess();
		currentSession.reset();
		SuperGroupPtr superGroup = process->getSuperGroup();
		pool->detachProcess(process);
		LockGuard l(pool->syncher);
		ensure_equals(pool->superGroups.size(), 1u);
		ensure(superGroup->isAlive());
		ensure(!superGroup->garbageCollectable());
	}

	TEST_METHOD(34) {
		// When detaching a process, it waits until all sessions have
		// finished before telling the process to shut down.
		Options options = createOptions();
		options.spawnMethod = "direct";
		options.minProcesses = 0;
		SessionPtr session = pool->get(options, &ticket);
		ProcessPtr process = session->getProcess();

		ensure(pool->detachProcess(process));
		{
			LockGuard l(pool->syncher);
			ensure_equals(process->enabled, Process::DETACHED);
		}
		SHOULD_NEVER_HAPPEN(100,
			LockGuard l(pool->syncher);
			result = !process->isAlive()
				|| !process->osProcessExists();
		);

		session.reset();
		EVENTUALLY(1,
			LockGuard l(pool->syncher);
			result = process->enabled == Process::DETACHED
				&& !process->osProcessExists()
				&& process->isDead();
		);
	}

	TEST_METHOD(35) {
		// When detaching a process, it waits until the OS processes
		// have exited before cleaning up the in-memory data structures.
		Options options = createOptions();
		options.spawnMethod = "direct";
		options.minProcesses = 0;
		ProcessPtr process = pool->get(options, &ticket)->getProcess();

		ScopeGuard g(boost::bind(::kill, process->pid, SIGCONT));
		kill(process->pid, SIGSTOP);

		ensure(pool->detachProcess(process));
		{
			LockGuard l(pool->syncher);
			ensure_equals(process->enabled, Process::DETACHED);
		}
		EVENTUALLY(1,
			result = process->getLifeStatus() == Process::SHUTDOWN_TRIGGERED;
		);

		SHOULD_NEVER_HAPPEN(100,
			LockGuard l(pool->syncher);
			result = process->isDead()
				|| !process->osProcessExists();
		);

		kill(process->pid, SIGCONT);
		g.clear();

		EVENTUALLY(1,
			LockGuard l(pool->syncher);
			result = process->enabled == Process::DETACHED
				&& !process->osProcessExists()
				&& process->isDead();
		);
	}

	TEST_METHOD(36) {
		// Detaching a process that is already being detached, works.
		Options options = createOptions();
		options.appGroupName = "test";
		options.minProcesses = 0;

		initPoolDebugging();
		debug->restarting = false;
		debug->spawning   = false;
		debug->detachedProcessesChecker = true;

		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 1;
		);
		EVENTUALLY(5,
			result = number == 1;
		);

		ProcessPtr process = currentSession->getProcess();
		pool->detachProcess(currentSession->getProcess());
		debug->debugger->recv("About to start detached processes checker");
		{
			LockGuard l(pool->syncher);
			ensure(process->enabled == Process::DETACHED);
		}

		pool->detachProcess(currentSession->getProcess());
		debug->messages->send("Proceed with starting detached processes checker");
		debug->messages->send("Proceed with starting detached processes checker");

		EVENTUALLY(5,
			result = pool->getProcessCount() == 0;
		);
		currentSession.reset();
		EVENTUALLY(5,
			result = process->isDead();
		);
	}


	/*********** Test disabling and enabling processes ***********/

	TEST_METHOD(40) {
		// Disabling a process under idle conditions should succeed immediately.
		ensureMinProcesses(2);
		vector<ProcessPtr> processes = pool->getProcesses();
		ensure_equals("Disabling succeeds",
			pool->disableProcess(processes[0]->gupid), DR_SUCCESS);

		LockGuard l(pool->syncher);
		ensure(processes[0]->isAlive());
		ensure_equals("Process is disabled",
			processes[0]->enabled,
			Process::DISABLED);
		ensure("Other processes are not affected",
			processes[1]->isAlive());
		ensure_equals("Other processes are not affected",
			processes[1]->enabled, Process::ENABLED);
	}

	TEST_METHOD(41) {
		// Disabling the sole process in a group, in case the pool settings allow
		// spawning another process, should trigger a new process spawn.
		ensureMinProcesses(1);
		Options options = createOptions();
		SessionPtr session = pool->get(options, &ticket);

		ensure_equals(pool->getProcessCount(), 1u);
		ensure(!pool->isSpawning());

		spawnerConfig->spawnTime = 60000;
		AtomicInt code = -1;
		TempThread thr(boost::bind(&ApplicationPool2_PoolTest::disableProcess,
			this, session->getProcess(), &code));
		EVENTUALLY2(100, 1,
			result = pool->isSpawning();
		);
		EVENTUALLY(1,
			result = pool->getProcessCount() == 2u;
		);
		ensure_equals((int) code, -1);
		session.reset();
		EVENTUALLY(1,
			result = code == (int) DR_SUCCESS;
		);
	}

	TEST_METHOD(42) {
		// Disabling the sole process in a group, in case pool settings don't allow
		// spawning another process, should fail.
		pool->setMax(1);
		ensureMinProcesses(1);

		vector<ProcessPtr> processes = pool->getProcesses();
		ensure_equals("(1)", processes.size(), 1u);

		DisableResult result = pool->disableProcess(processes[0]->gupid);
		ensure_equals("(2)", result, DR_ERROR);
		ensure_equals("(3)", pool->getProcessCount(), 1u);
	}

	TEST_METHOD(43) {
		// If there are no enabled processes in the group, then disabling should
		// succeed after the new process has been spawned.
		initPoolDebugging();
		debug->messages->send("Proceed with spawn loop iteration 1");
		debug->messages->send("Proceed with spawn loop iteration 2");

		Options options = createOptions();
		SessionPtr session1 = pool->get(options, &ticket);
		SessionPtr session2 = pool->get(options, &ticket);
		ensure_equals(pool->getProcessCount(), 2u);
		GroupPtr group = session1->getGroup();

		AtomicInt code1 = -1, code2 = -1;
		TempThread thr(boost::bind(&ApplicationPool2_PoolTest::disableProcess,
			this, session1->getProcess(), &code1));
		TempThread thr2(boost::bind(&ApplicationPool2_PoolTest::disableProcess,
			this, session2->getProcess(), &code2));
		EVENTUALLY(2,
			LockGuard l(pool->syncher);
			result = group->enabledCount == 0
				&& group->disablingCount == 2
				&& group->disabledCount == 0;
		);
		session1.reset();
		session2.reset();
		SHOULD_NEVER_HAPPEN(20,
			result = code1 != -1 || code2 != -1;
		);

		debug->messages->send("Proceed with spawn loop iteration 3");
		EVENTUALLY(5,
			result = code1 == DR_SUCCESS;
		);
		EVENTUALLY(5,
			result = code2 == DR_SUCCESS;
		);
		{
			LockGuard l(pool->syncher);
			ensure_equals(group->enabledCount, 1);
			ensure_equals(group->disablingCount, 0);
			ensure_equals(group->disabledCount, 2);
		}
	}

	TEST_METHOD(44) {
		// Suppose that a previous disable command triggered a new process spawn,
		// and the spawn fails. Then any disabling processes should become enabled
		// again, and the callbacks for the previous disable commands should be called.
		initPoolDebugging();
		debug->messages->send("Proceed with spawn loop iteration 1");
		debug->messages->send("Proceed with spawn loop iteration 2");

		Options options = createOptions();
		options.minProcesses = 2;
		SessionPtr session1 = pool->get(options, &ticket);
		SessionPtr session2 = pool->get(options, &ticket);
		ensure_equals(pool->getProcessCount(), 2u);

		AtomicInt code1 = -1, code2 = -1;
		TempThread thr(boost::bind(&ApplicationPool2_PoolTest::disableProcess,
			this, session1->getProcess(), &code1));
		TempThread thr2(boost::bind(&ApplicationPool2_PoolTest::disableProcess,
			this, session2->getProcess(), &code2));
		EVENTUALLY(2,
			GroupPtr group = session1->getGroup();
			LockGuard l(pool->syncher);
			result = group->enabledCount == 0
				&& group->disablingCount == 2
				&& group->disabledCount == 0;
		);
		SHOULD_NEVER_HAPPEN(20,
			result = code1 != -1 || code2 != -1;
		);

		setLogLevel(-2);
		debug->messages->send("Fail spawn loop iteration 3");
		EVENTUALLY(5,
			result = code1 == DR_ERROR;
		);
		EVENTUALLY(5,
			result = code2 == DR_ERROR;
		);
		{
			GroupPtr group = session1->getGroup();
			LockGuard l(pool->syncher);
			ensure_equals(group->enabledCount, 2);
			ensure_equals(group->disablingCount, 0);
			ensure_equals(group->disabledCount, 0);
		}
	}

	// TODO: asyncGet() should not select a disabling process if there are enabled processes.
	// TODO: asyncGet() should not select a disabling process when non-rolling restarting.
	// TODO: asyncGet() should select a disabling process if there are no enabled processes
	//       in the group. If this happens then asyncGet() will also spawn a new process.
	// TODO: asyncGet() should not select a disabled process.

	// TODO: If there are no enabled processes and all disabling processes are at full
	//       utilization, and the process that was being spawned becomes available
	//       earlier than any of the disabling processes, then the newly spawned process
	//       should handle the request.

	// TODO: A disabling process becomes disabled as soon as it's done with
	//       all its request.

	TEST_METHOD(50) {
		// Disabling a process that's already being disabled should result in the
		// callback being called after disabling is done.
		ensureMinProcesses(2);
		Options options = createOptions();
		SessionPtr session = pool->get(options, &ticket);

		AtomicInt code = -1;
		TempThread thr(boost::bind(&ApplicationPool2_PoolTest::disableProcess,
			this, session->getProcess(), &code));
		SHOULD_NEVER_HAPPEN(100,
			result = code != -1;
		);
		session.reset();
		EVENTUALLY(1,
			result = code != -1;
		);
		ensure_equals(code, (int) DR_SUCCESS);
	}

	// TODO: Enabling a process that's disabled succeeds immediately.
	// TODO: Enabling a process that's disabling succeeds immediately. The disable
	//       callbacks will be called with DR_CANCELED.

	TEST_METHOD(51) {
		// If the number of processes is already at maximum, then disabling
		// a process will cause that process to be disabled, without spawning
		// a new process.
		pool->setMax(2);
		ensureMinProcesses(2);

		vector<ProcessPtr> processes = pool->getProcesses();
		ensure_equals(processes.size(), 2u);
		DisableResult result = pool->disableProcess(processes[0]->gupid);
		ensure_equals(result, DR_SUCCESS);

		{
			ScopedLock l(pool->syncher);
			GroupPtr group = processes[0]->getGroup();
			ensure_equals(group->enabledCount, 1);
			ensure_equals(group->disablingCount, 0);
			ensure_equals(group->disabledCount, 1);
		}
	}


	/*********** Other tests ***********/

	TEST_METHOD(60) {
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
		clearAllSessions();
		pool->detachSuperGroupByName("test");
		ensure(!pool->atFullCapacity());
	}

	TEST_METHOD(61) {
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

	TEST_METHOD(62) {
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

	TEST_METHOD(63) {
		// findProcessByGupid() returns a NULL pointer if there is
		// no matching process.
		ensure(pool->findProcessByGupid("none") == NULL);
	}

	TEST_METHOD(64) {
		// Test process idle cleaning.
		Options options = createOptions();
		pool->setMaxIdleTime(50000);
		SessionPtr session1 = pool->get(options, &ticket);
		SessionPtr session2 = pool->get(options, &ticket);
		ensure_equals(pool->getProcessCount(), 2u);

		session2.reset();

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

	TEST_METHOD(65) {
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

	TEST_METHOD(66) {
		// It should restart the app if restart.txt is created or updated.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";
		pool->setMax(1);

		// Send normal request.
		ensure_equals(sendRequest(options, "/"), "front page");

		// Modify application; it shouldn't have effect yet.
		writeFile("tmp.wsgi/passenger_wsgi.py",
			"def application(env, start_response):\n"
			"	start_response('200 OK', [('Content-Type', 'text/html')])\n"
			"	return ['restarted']\n");
		ensure_equals(sendRequest(options, "/"), "front page");

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

	TEST_METHOD(67) {
		// Test spawn exceptions.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";

		writeFile("tmp.wsgi/passenger_wsgi.py",
			"import sys\n"
			"sys.stderr.write('Something went wrong!')\n"
			"exit(1)\n");

		setLogLevel(-2);
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);

		ensure(currentException != NULL);
		boost::shared_ptr<SpawnException> e = dynamic_pointer_cast<SpawnException>(currentException);
		ensure(e->getErrorPage().find("Something went wrong!") != string::npos);
	}

	TEST_METHOD(68) {
		// If a process fails to spawn, then it stops trying to spawn minProcesses processes.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";
		options.minProcesses = 4;

		writeFile("tmp.wsgi/counter", "0");
		chmod("tmp.wsgi/counter", 0666);
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

		setLogLevel(-2);
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		EVENTUALLY(5,
			result = pool->getProcessCount() == 2;
		);
		EVENTUALLY(5,
			result = !pool->isSpawning();
		);
		SHOULD_NEVER_HAPPEN(500,
			result = pool->getProcessCount() > 2;
		);
	}

	TEST_METHOD(69) {
		// It removes the process from the pool if session->initiate() fails.
		Options options = createOptions();
		options.appRoot = "stub/wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
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

	TEST_METHOD(70) {
		// When a process has become idle, and there are waiters on the pool,
		// consider detaching it in order to satisfy a waiter.
		Options options1 = createOptions();
		Options options2 = createOptions();
		options2.appRoot = "stub/wsgi";

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
		ensure_equals(superGroup1->defaultGroup->enabledCount, 1);
		ensure_equals(superGroup2->defaultGroup->enabledCount, 1);
	}

	TEST_METHOD(71) {
		// A process is detached after processing maxRequests sessions.
		Options options = createOptions();
		options.minProcesses = 0;
		options.maxRequests = 5;
		pool->setMax(1);

		SessionPtr session = pool->get(options, &ticket);
		ensure_equals(pool->getProcessCount(), 1u);
		pid_t origPid = session->getPid();
		session.reset();

		for (int i = 0; i < 3; i++) {
			pool->get(options, &ticket).reset();
			ensure_equals(pool->getProcessCount(), 1u);
			ensure_equals(pool->getProcesses()[0]->pid, origPid);
		}

		pool->get(options, &ticket).reset();
		EVENTUALLY(2,
			result = pool->getProcessCount() == 0;
		);
	}

	TEST_METHOD(72) {
		// If we restart while spawning is in progress, and the restart
		// finishes before the process is done spawning, then that
		// process will not be attached and the original spawn loop will
		// abort. A new spawn loop will start to ensure that resource
		// constraints are met.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		initPoolDebugging();
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.minProcesses = 3;

		// Trigger spawn loop and freeze it at the point where it's spawning
		// the second process.
		pool->asyncGet(options, callback);
		debug->debugger->recv("Begin spawn loop iteration 1");
		debug->messages->send("Proceed with spawn loop iteration 1");
		debug->debugger->recv("Begin spawn loop iteration 2");
		ensure_equals("(1)", pool->getProcessCount(), 1u);

		// Trigger restart, wait until it's finished.
		touchFile("tmp.wsgi/tmp/restart.txt", 1);
		pool->asyncGet(options, callback);
		debug->messages->send("Finish restarting");
		debug->debugger->recv("Restarting done");
		ensure_equals("(2)", pool->getProcessCount(), 0u);

		// The restarter should have created a new spawn loop and
		// instructed the old one to stop.
		debug->debugger->recv("Begin spawn loop iteration 3");

		// We let the old spawn loop continue, which should drop
		// the second process and abort.
		debug->messages->send("Proceed with spawn loop iteration 2");
		debug->debugger->recv("Spawn loop done");
		ensure_equals("(3)", pool->getProcessCount(), 0u);

		// We let the new spawn loop continue.
		debug->messages->send("Proceed with spawn loop iteration 3");
		debug->messages->send("Proceed with spawn loop iteration 4");
		debug->messages->send("Proceed with spawn loop iteration 5");
		debug->debugger->recv("Spawn loop done");
		ensure_equals("(4)", pool->getProcessCount(), 3u);
	}

	TEST_METHOD(73) {
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
		spawnerConfig->spawnerCreationSleepTime = 20000;
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

	TEST_METHOD(74) {
		// If a process fails to spawn, it sends a SpawnException result to all get waiters.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		chmod("tmp.wsgi", 0777);
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";
		pool->setMax(1);

		writeFile("tmp.wsgi/passenger_wsgi.py",
			"import os, time, sys\n"
			"\n"
			"def file_exists(filename):\n"
			"	try:\n"
			"		os.stat(filename)\n"
			"		return True\n"
			"	except OSError:\n"
			"		return False\n"
			"\n"
			"f = open('spawned.txt', 'w')\n"
			"f.write(str(os.getpid()))\n"
			"f.close()\n"
			"while not file_exists('continue.txt'):\n"
			"	time.sleep(0.05)\n"
			"sys.stderr.write('Something went wrong!')\n"
			"exit(1)\n");

		retainSessions = true;
		setLogLevel(-2);
		pool->asyncGet(options, callback);
		pool->asyncGet(options, callback);
		pool->asyncGet(options, callback);
		pool->asyncGet(options, callback);

		EVENTUALLY(5,
			result = fileExists("tmp.wsgi/spawned.txt");
		);
		usleep(20000);
		writeFile("tmp.wsgi/passenger_wsgi.py", readAll("stub/wsgi/passenger_wsgi.py"));
		pid_t pid = (pid_t) stringToLL(readAll("tmp.wsgi/spawned.txt"));
		kill(pid, SIGTERM);
		EVENTUALLY(5,
			result = number == 4;
		);
		ensure_equals(pool->getProcessCount(), 0u);
		ensure(sessions.empty());
	}

	TEST_METHOD(75) {
		// If a process fails to spawn, the existing processes
		// are kept alive.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";
		options.minProcesses = 2;

		// Spawn 2 processes.
		retainSessions = true;
		pool->asyncGet(options, callback);
		pool->asyncGet(options, callback);
		EVENTUALLY(10,
			result = number == 2;
		);
		ensure_equals(pool->getProcessCount(), 2u);

		// Mess up the application and spawn a new one.
		writeFile("tmp.wsgi/passenger_wsgi.py",
			"import sys\n"
			"sys.stderr.write('Something went wrong!')\n"
			"exit(1)\n");
		try {
			setLogLevel(-2);
			currentSession = pool->get(options, &ticket);
			fail("SpawnException expected");
		} catch (const SpawnException &) {
			ensure_equals(pool->getProcessCount(), 2u);
		}
	}

	TEST_METHOD(76) {
		// No more than maxOutOfBandWorkInstances process will be performing
		// out-of-band work at the same time.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";
		options.maxOutOfBandWorkInstances = 2;
		initPoolDebugging();
		debug->restarting = false;
		debug->spawning = false;
		debug->oobw = true;

		// Spawn 3 processes and initiate 2 OOBW requests.
		SessionPtr session1 = pool->get(options, &ticket);
		SessionPtr session2 = pool->get(options, &ticket);
		SessionPtr session3 = pool->get(options, &ticket);
		session1->requestOOBW();
		session1.reset();
		session2->requestOOBW();
		session2.reset();

		// 2 OOBW requests eventually start.
		debug->debugger->recv("OOBW request about to start");
		debug->debugger->recv("OOBW request about to start");

		// Request another OOBW, but this one is not initiated.
		session3->requestOOBW();
		session3.reset();
		SHOULD_NEVER_HAPPEN(100,
			result = debug->debugger->peek("OOBW request about to start") != NULL;
		);

		// Let one OOBW request finish. The third one should eventually
		// start.
		debug->messages->send("Proceed with OOBW request");
		debug->debugger->recv("OOBW request about to start");

		debug->messages->send("Proceed with OOBW request");
		debug->messages->send("Proceed with OOBW request");
		debug->debugger->recv("OOBW request finished");
		debug->debugger->recv("OOBW request finished");
		debug->debugger->recv("OOBW request finished");
	}

	TEST_METHOD(77) {
		// If the getWaitlist already has maxRequestQueueSize items,
		// then an exception is returned.
		Options options = createOptions();
		options.appGroupName = "test1";
		options.maxRequestQueueSize = 3;
		GroupPtr group = pool->findOrCreateGroup(options);
		spawnerConfig->concurrency = 3;
		initPoolDebugging();
		pool->setMax(1);

		for (int i = 0; i < 3; i++) {
			pool->asyncGet(options, callback);
		}
		ensure_equals(number, 0);
		{
			LockGuard l(pool->syncher);
			ensure_equals(group->getWaitlist.size(),
				3u);
		}

		try {
			pool->get(options, &ticket);
			fail("Expected RequestQueueFullException");
		} catch (const RequestQueueFullException &e) {
			// OK
		}

		debug->messages->send("Proceed with spawn loop iteration 1");
		debug->messages->send("Spawn loop done");
		EVENTUALLY(5,
			result = number == 3;
		);
	}

	TEST_METHOD(78) {
		// Test restarting while a previous restart was already being finalized.
		// The previous finalization should abort.
		Options options = createOptions();
		initPoolDebugging();
		debug->spawning = false;
		pool->get(options, &ticket);

		ensure_equals(pool->restartSuperGroupsByAppRoot(options.appRoot), 1u);
		debug->debugger->recv("About to end restarting");
		ensure_equals(pool->restartSuperGroupsByAppRoot(options.appRoot), 1u);
		debug->debugger->recv("About to end restarting");
		debug->messages->send("Finish restarting");
		debug->messages->send("Finish restarting");
		debug->debugger->recv("Restarting done");
		debug->debugger->recv("Restarting aborted");
	}

	TEST_METHOD(79) {
		// Test sticky sessions.

		// Spawn 2 processes and get their sticky session IDs and PIDs.
		ensureMinProcesses(2);
		Options options = createOptions();
		SessionPtr session1 = pool->get(options, &ticket);
		SessionPtr session2 = pool->get(options, &ticket);
		int id1 = session1->getStickySessionId();
		int id2 = session2->getStickySessionId();
		pid_t pid1 = session1->getPid();
		pid_t pid2 = session2->getPid();
		session1.reset();
		session2.reset();

		// Make two requests with id1 as sticky session ID. They should
		// both go to process pid1.
		options.stickySessionId = id1;
		session1 = pool->get(options, &ticket);
		ensure_equals("Request 1.1 goes to process 1", session1->getPid(), pid1);
		// The second request should be queued, and should not finish until
		// the first request is finished.
		ensure_equals(number, 1);
		pool->asyncGet(options, callback);
		SHOULD_NEVER_HAPPEN(100,
			result = number > 1;
		);
		session1.reset();
		EVENTUALLY(1,
			result = number == 2;
		);
		ensure_equals("Request 1.2 goes to process 1", currentSession->getPid(), pid1);
		currentSession.reset();

		// Make two requests with id2 as sticky session ID. They should
		// both go to process pid2.
		options.stickySessionId = id2;
		session1 = pool->get(options, &ticket);
		ensure_equals("Request 2.1 goes to process 2", session1->getPid(), pid2);
		// The second request should be queued, and should not finish until
		// the first request is finished.
		pool->asyncGet(options, callback);
		SHOULD_NEVER_HAPPEN(100,
			result = number > 2;
		);
		session1.reset();
		EVENTUALLY(1,
			result = number == 3;
		);
		ensure_equals("Request 2.2 goes to process 2", currentSession->getPid(), pid2);
		currentSession.reset();
	}

	// TODO: Persistent connections.
	// TODO: If one closes the session before it has reached EOF, and process's maximum concurrency
	//       has already been reached, then the pool should ping the process so that it can detect
	//       when the session's connection has been released by the app.


	/*********** Test previously discovered bugs ***********/

	TEST_METHOD(85) {
		// Test detaching, then restarting. This should not violate any invariants.
		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		Options options = createOptions();
		options.appRoot = "tmp.wsgi";
		options.appType = "wsgi";
		options.startupFile = "passenger_wsgi.py";
		options.spawnMethod = "direct";

		SessionPtr session = pool->get(options, &ticket);
		string gupid = session->getProcess()->gupid;
		session.reset();
		pool->detachProcess(gupid);
		touchFile("tmp.wsgi/tmp/restart.txt", 1);
		pool->get(options, &ticket).reset();
	}


	/*****************************/
}
