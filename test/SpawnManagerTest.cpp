#include "tut.h"
#include "SpawnManager.h"
#include <cstring>
#include <unistd.h>

using namespace Passenger;

namespace tut {
	struct SpawnManagerTest {
		SpawnManagerPtr spawnManager;

		SpawnManagerTest() {
		}
	};

	DEFINE_TEST_GROUP(SpawnManagerTest);

	TEST_METHOD(1) {
		// Spawning an application should return a valid Application object.
		SpawnManager manager("support/spawn_manager_mock.rb");
		ApplicationPtr app(manager.spawn("."));
		char buf[5];

		ensure_equals("The Application object's PID is the same as the one specified by the mock",
			app->getPid(), 1234);
		ensure_equals("Application.getWriter() is a valid, writable file descriptor",
			write(app->getWriter(), "hello", 5),
			5);
		ensure_equals("Application.getReader() is a valid, readable file descriptor",
			read(app->getReader(), buf, 5),
			5);
		ensure("The two channels are connected with each other, as specified by the mock object",
			memcmp(buf, "hello", 5) == 0);
	}
}
