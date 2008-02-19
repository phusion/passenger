#include "tut.h"
#define TESTING_SPAWN_MANAGER
#include "SpawnManager.h"
#include <sys/types.h>
#include <signal.h>
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
		// should be restarted and another (successful) spawn should be attempted.
		pid_t old_pid = manager.getServerPID();
		kill(manager.getServerPID(), SIGTERM);
		ApplicationPtr app(manager.spawn("."));
		ensure("The spawn server was restarted", manager.getServerPID() != old_pid);
		ensure_equals("The Application object's PID is the same as the one specified by the stub",
			app->getPid(), 1234);
	}
	
	// TODO: test spawning application as a different user
	// TODO: if the spawn server dies after a restart, a SpawnException should be thrown
}
