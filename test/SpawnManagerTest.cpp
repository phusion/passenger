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
		ApplicationPtr app(manager.spawn(PoolOptions(".")));
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
		
		ApplicationPtr app(manager.spawn(PoolOptions(".")));
		ensure_equals("The Application object's PID is the same as the one specified by the stub",
			app->getPid(), 1234);
		
		// The following test will fail if we're inside Valgrind, but that's normal.
		// Killing the spawn server doesn't work.
		if (!RUNNING_ON_VALGRIND) {
			ensure("The spawn server was restarted", manager.getServerPid() != old_pid);
		}
	}
	
	TEST_METHOD(3) {
		// This test fails in Valgrind, but that's normal.
		// Killing the spawn server doesn't work.
		if (!RUNNING_ON_VALGRIND) {
			// If the spawn server dies after a restart, a SpawnException should be thrown.
			kill(manager.getServerPid(), SIGTERM);
			// Give the spawn server the time to properly terminate.
			usleep(500000);
			
			try {
				manager.nextRestartShouldFail = true;
				ApplicationPtr app(manager.spawn(PoolOptions(".")));
				fail("SpawnManager did not throw a SpawnException");
			} catch (const SpawnException &e) {
				// Success.
			}
		}
	}
}
