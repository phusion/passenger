#include "tut.h"
#define TESTING_SPAWN_MANAGER
#include "SpawnManager.h"
#include <cstring>
#include <unistd.h>

using namespace Passenger;

namespace tut {
	struct SpawnManagerTest {
		SpawnManager manager;
		
		SpawnManagerTest(): manager("stub/spawn_server.rb") {}
	};

	DEFINE_TEST_GROUP(SpawnManagerTest);

	TEST_METHOD(1) {
		// Spawning an application should return a valid Application object.
		ApplicationPtr app(manager.spawn("."));
		ensure_equals("The Application object's PID is the same as the one specified by the stub",
			app->getPid(), 1234);
	}
	
	TEST_METHOD(2) {
		// If something goes wrong during spawning, the spawn manager
		// should be restarted and another spawn should be attempted.
		manager.nextSpawnShouldFail = true;
		ApplicationPtr app(manager.spawn("."));
		ensure(!manager.nextSpawnShouldFail);
		ensure_equals("The Application object's PID is the same as the one specified by the stub",
			app->getPid(), 1234);
	}
}
