#include "tut.h"
#include "SpawnManager.h"
#include <sys/types.h>
#include <signal.h>
#include <cstring>
#include <unistd.h>
#include "valgrind.h"

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
		pid_t old_pid = manager.getServerPid();
		kill(manager.getServerPid(), SIGTERM);
		// Give the spawn server the time to properly terminate.
		usleep(500000);
		
		ApplicationPtr app(manager.spawn("."));
		ensure_equals("The Application object's PID is the same as the one specified by the stub",
			app->getPid(), 1234);
		
		// The following test will fail if we're inside Valgrind, but that's normal.
		if (!RUNNING_ON_VALGRIND) {
			ensure("The spawn server was restarted", manager.getServerPid() != old_pid);
		}
	}
	
	TEST_METHOD(3) {
		// If the spawn server dies after a restart, a SpawnException should be thrown.
		kill(manager.getServerPid(), SIGTERM);
		// Give the spawn server the time to properly terminate.
		usleep(500000);
		
		try {
			manager.nextRestartShouldFail = true;
			ApplicationPtr app(manager.spawn("."));
			fail("SpawnManager did not throw a SpawnException");
		} catch (const SpawnException &e) {
			// Success.
		}
	}
	
	// TODO: test spawning application as a different user
}
