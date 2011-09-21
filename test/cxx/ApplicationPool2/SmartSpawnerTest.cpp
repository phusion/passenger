#include <TestSupport.h>
#include <ApplicationPool2/Spawner.h>
#include <Logging.h>
#include <unistd.h>
#include <climits>
#include <signal.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct ApplicationPool2_SmartSpawnerTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		BackgroundEventLoop bg;
		
		ApplicationPool2_SmartSpawnerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			bg.start();
		}
		
		~ApplicationPool2_SmartSpawnerTest() {
			setLogLevel(0);
		}
		
		shared_ptr<SmartSpawner> createSpawner(const Options &options, bool exitImmediately = false) {
			char buf[PATH_MAX + 1];
			getcwd(buf, PATH_MAX);
			
			vector<string> command;
			command.push_back("ruby");
			command.push_back(string(buf) + "/support/placebo-preloader.rb");
			if (exitImmediately) {
				command.push_back("exit-immediately");
			}
			
			return make_shared<SmartSpawner>(bg.libev,
				*resourceLocator,
				generation,
				command,
				options);
		}
		
		Options createOptions() {
			Options options;
			options.spawnMethod = "smart";
			options.loadShellEnvvars = false;
			return options;
		}
	};
	
	DEFINE_TEST_GROUP(ApplicationPool2_SmartSpawnerTest);
	
	#include "SpawnerTestCases.cpp"
	
	TEST_METHOD(30) {
		// If the preloader has crashed then SmartSpawner will
		// restart it and try again.
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		shared_ptr<SmartSpawner> spawner = createSpawner(options);
		spawner->spawn(options);
		
		kill(spawner->getPreloaderPid(), SIGTERM);
		// Give it some time to exit.
		usleep(300000);
		
		// No exception at next spawn.
		setLogLevel(-1);
		spawner->spawn(options);
	}
	
	TEST_METHOD(31) {
		// If the preloader still crashes after the restart then
		// SmartSpawner will throw an exception.
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		setLogLevel(-1);
		shared_ptr<SmartSpawner> spawner = createSpawner(options, true);
		try {
			spawner->spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &) {
			// Pass.
		}
	}
	
	TEST_METHOD(32) {
		// If the preloader didn't start within the timeout
		// then it's killed and an exception is thrown, with
		// whatever stderr output as error page.
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		options.startTimeout = 300;
		
		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world >&2; sleep 60");
		SmartSpawner spawner(bg.libev,
			*resourceLocator,
			generation,
			preloaderCommand,
			options);
		spawner.forwardStdout = false;
		spawner.forwardStderr = false;
		
		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::PRELOADER_STARTUP_TIMEOUT);
			ensure_equals(e.getErrorPage(),
				"hello world\n");
		}
	}
	
	TEST_METHOD(33) {
		// If the preloader crashed during startup without returning
		// a proper error response, then its stderr output is used
		// as error response instead.
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\1" "start.rb";
		options.startupFile  = "stub/rack/start.rb";
		
		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world >&2");
		SmartSpawner spawner(bg.libev,
			*resourceLocator,
			generation,
			preloaderCommand,
			options);
		spawner.forwardStdout = false;
		spawner.forwardStderr = false;
		
		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR);
			ensure_equals(e.getErrorPage(),
				"hello world\n");
		}
	}
}
