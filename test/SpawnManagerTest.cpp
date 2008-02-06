#include "tut.h"
#include "SpawnManager.h"
#include <cstring>
#include <unistd.h>

using namespace Passenger;

namespace tut {
	struct SpawnManagerTest {
	};

	DEFINE_TEST_GROUP(SpawnManagerTest);

	TEST_METHOD(1) {
		// Spawning an application should return a valid Application object.
		SpawnManager manager("stub/spawn_server.rb");
		ApplicationPtr app(manager.spawn("."));
		ensure_equals("The Application object's PID is the same as the one specified by the stub",
			app->getPid(), 1234);
	}
}
