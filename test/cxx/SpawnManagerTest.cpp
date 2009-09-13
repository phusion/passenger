#include "TestSupport.h"
#include "SpawnManager.h"
#include <sys/types.h>
#include <signal.h>
#include <cstring>
#include <unistd.h>
#include "valgrind.h"

using namespace Passenger;

namespace tut {
	struct SpawnManagerTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		SpawnManagerPtr manager;
		
		void initialize() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			manager = ptr(new SpawnManager("stub/spawn_server.rb", generation));
		}
	};

	DEFINE_TEST_GROUP(SpawnManagerTest);

	TEST_METHOD(1) {
		// Spawning an application should return a valid Application object.
		initialize();
		ApplicationPtr app(manager->spawn(PoolOptions(".")));
		ensure_equals("The Application object's PID is the same as the one returned by the stub",
			app->getPid(), (pid_t) 1234);
	}
	
	TEST_METHOD(2) {
		// If something goes wrong during spawning, the spawn manager
		// should be restarted and another (successful) spawn should be attempted.
		initialize();
		pid_t old_pid = manager->getServerPid();
		manager->killSpawnServer();
		// Give the spawn server the time to properly terminate.
		usleep(500000);
		
		ApplicationPtr app(manager->spawn(PoolOptions(".")));
		ensure_equals("The Application object's PID is the same as the one specified by the stub",
			app->getPid(), 1234);
		
		// The following test will fail if we're inside Valgrind, but that's normal.
		// Killing the spawn server doesn't work there.
		if (!RUNNING_ON_VALGRIND) {
			ensure("The spawn server was restarted", manager->getServerPid() != old_pid);
		}
	}
	
	class BuggySpawnManager: public SpawnManager {
	protected:
		virtual void spawnServerStarted() {
			if (nextRestartShouldFail) {
				nextRestartShouldFail = false;
				killSpawnServer();
				usleep(25000);
			}
		}
		
	public:
		bool nextRestartShouldFail;
		
		BuggySpawnManager(const ServerInstanceDir::GenerationPtr &generation)
			: SpawnManager("stub/spawn_server.rb", generation)
		{
			nextRestartShouldFail = false;
		}
	};
	
	TEST_METHOD(3) {
		// If the spawn server dies after a restart, a SpawnException should be thrown.
		
		// This test fails in Valgrind, but that's normal.
		// Killing the spawn server doesn't work there.
		if (!RUNNING_ON_VALGRIND) {
			BuggySpawnManager manager(generation);
			manager.killSpawnServer();
			// Give the spawn server the time to properly terminate.
			usleep(250000);
			
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
