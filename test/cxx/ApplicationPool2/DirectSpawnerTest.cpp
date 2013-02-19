#include <TestSupport.h>
#include <ApplicationPool2/DirectSpawner.h>
#include <Utils/json.h>
#include <fcntl.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct ApplicationPool2_DirectSpawnerTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		BackgroundEventLoop bg;
		ProcessPtr process;
		
		ApplicationPool2_DirectSpawnerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			bg.start();
		}

		~ApplicationPool2_DirectSpawnerTest() {
			unlink("stub/wsgi/passenger_wsgi.pyc");
			Process::maybeShutdown(process);
		}
		
		shared_ptr<DirectSpawner> createSpawner(const Options &options) {
			return make_shared<DirectSpawner>(bg.safe,
				*resourceLocator, generation);
		}
		
		Options createOptions() {
			Options options;
			options.spawnMethod = "direct";
			options.loadShellEnvvars = false;
			return options;
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(ApplicationPool2_DirectSpawnerTest, 90);
	
	#include "SpawnerTestCases.cpp"
	
	TEST_METHOD(80) {
		// If the application didn't start within the timeout
		// then whatever was written to stderr is used as the
		// SpawnException error page.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\1" "-e\1" "print STDERR \"hello world\\n\"; sleep(60)";
		options.startupFile  = ".";
		options.startTimeout = 300;
		
		DirectSpawner spawner(bg.safe, *resourceLocator, generation);
		spawner.getConfig()->forwardStderr = false;
		
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
	
	TEST_METHOD(81) {
		// If the application crashed during startup without returning
		// a proper error response, then its stderr output is used
		// as error response instead.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\1" "-e\1" "print STDERR \"hello world\\n\"";
		options.startupFile  = ".";
		
		DirectSpawner spawner(bg.safe, *resourceLocator, generation);
		spawner.getConfig()->forwardStderr = false;
		
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
