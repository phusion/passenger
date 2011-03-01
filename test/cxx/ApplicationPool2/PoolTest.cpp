#include <TestSupport.h>
#include <ApplicationPool2/Pool.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct ApplicationPool2_PoolTest {
		BackgroundEventLoop bg;
		PoolPtr pool;
		GetCallback callback;
		SessionPtr currentSession;
		ExceptionPtr currentException;
		AtomicInt number;
		
		ApplicationPool2_PoolTest() {
			pool = make_shared<Pool>(bg.libev,
				make_shared<SpawnerFactory>(bg.libev, *resourceLocator));
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
	
	DEFINE_TEST_GROUP(ApplicationPool2_PoolTest);
	#if 0
	TEST_METHOD(1) {
		// Test initial state.
		ensure(!pool->atFullCapacity());
	}
	
	TEST_METHOD(2) {
		// get() actions on empty pools cannot be immediately satisfied.
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
		// If one matching process already exists and it's idle then
		// the get() will use it.
		Options options = createOptions();
		pool->asyncGet(options, callback);
		EVENTUALLY(5,
			result = number == 1;
		);
		
		ProcessPtr process = currentSession->getProcess();
		currentSession.reset();
		ensure_equals(process->usage(), 0u);
		
		ScopedLock l(pool->syncher);
		pool->asyncGet(options, callback, false);
		ensure_equals("callback is immediately called", number, 2);
	}
	#endif
	TEST_METHOD(4) {
		// If one matching process already exists but it's not idle,
		// and the limits prevent spawning of a new process,
		// then get() will put the get action on the group's wait
		// queue. When the process becomes idle it will process
		// the request.
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
		// If one matching process already exists but it's not idle,
		// and the limits and pool capacity allow spawning of a new process,
		// then get() will put the get action on the group's wait
		// queue while spawning a process in the background.
		// Either the existing process or the new process will process
		// the action, whichever becomes first available.
		
		// Here we test the case in which the existing process becomes
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
		
		// Now spawn a process that never finishes.
		options.spawnMethod  = "direct";
		options.startCommand = "sleep\1" "60";
		pool->asyncGet(options, callback);
		
		// Release first process.
		session1.reset();
		
		ensure_equals(number, 2);
		ensure_equals(currentSession->getProcess(), process1);
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
		// then get() will use that.
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
		
		ScopedLock l(pool->syncher);
		pool->asyncGet(options, callback);
		ensure_equals(number, 2);
		SessionPtr session2 = currentSession;
	}
	
	TEST_METHOD(8) {
		// If multiple matching processes exist, and none of them are idle,
		// and no more processes may be spawned,
		// then get() will put the action on the group's wait queue.
		// The process that first becomes idle will process the action.
	}
	
	TEST_METHOD(9) {
		// If multiple matching processes exist, and none of them are idle,
		// a new process may be spawned,
		// then get() will put the action on the group's wait queue.
		// The process that first becomes idle or the newly spawned process
		// will process the action, whichever is earlier.
	}
}
