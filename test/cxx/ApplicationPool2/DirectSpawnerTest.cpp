#include <TestSupport.h>
#include <ApplicationPool2/Spawner.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct ApplicationPool2_DirectSpawnerTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		BackgroundEventLoop bg;
		
		ApplicationPool2_DirectSpawnerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			bg.start();
		}
		
		shared_ptr<DirectSpawner> createSpawner(const Options &options) {
			return make_shared<DirectSpawner>(bg.libev,
				*resourceLocator, generation);
		}
		
		Options createOptions() {
			Options options;
			options.spawnMethod = "direct";
			options.loadShellEnvvars = false;
			return options;
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool2_DirectSpawnerTest);
	
	#include "SpawnerTestCases.cpp"
	
	TEST_METHOD(30) {
		// If the application didn't start within the timeout
		// then whatever was written to stderr is used as the
		// SpawnException error page.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "bash\1" "-c\1" "echo hello world >&2; sleep 60";
		options.startupFile  = ".";
		options.startTimeout = 300;
		
		DirectSpawner spawner(bg.libev, *resourceLocator, generation);
		spawner.forwardStderr = false;
		
		try {
			spawner.spawn(options);
			fail("Timeout expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_TIMEOUT);
			ensure_equals(e.getErrorPage(),
				"hello world\n");
		}
	}
	
	TEST_METHOD(31) {
		// If the application crashed during startup without returning
		// a proper error response, then its stderr output is used
		// as error response instead.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "bash\1" "-c\1" "echo hello world >&2";
		options.startupFile  = ".";
		
		DirectSpawner spawner(bg.libev, *resourceLocator, generation);
		spawner.forwardStderr = false;
		
		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_PROTOCOL_ERROR);
			ensure_equals(e.getErrorPage(),
				"hello world\n");
		}
	}
}
