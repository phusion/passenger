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
		
		ApplicationPool2_PoolTest() {
			pool = make_shared<Pool>(bg.libev,
				make_shared<SpawnerFactory>(bg.libev, *resourceLocator));
			bg.start();
			callback = boost::bind(&ApplicationPool2_PoolTest::_callback, this, _1, _2);
		}
		
		Options createOptions() {
			Options options;
			options.loadShellEnvvars = false;
			return options;
		}
		
		void _callback(const SessionPtr &session, const ExceptionPtr &e) {
			currentSession = session;
			currentException = e;
		}
	};
	
	DEFINE_TEST_GROUP(ApplicationPool2_PoolTest);
	
	TEST_METHOD(1) {
		// Test initial state.
		ensure(!pool->atFullCapacity());
	}
	
	TEST_METHOD(2) {
		// get() actions on empty pools cannot be immediately satisfied.
		// Instead they're put on a wait list.
		Options options = createOptions();
		options.appRoot = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		ScopedLock l(pool->syncher);
		pool->asyncGet(options, callback, false);
		ensure(currentSession == NULL);
		ensure(currentException == NULL);
		ensure(pool->getWaitlist.empty());
		ensure(!pool->superGroups.empty());
	}
}
