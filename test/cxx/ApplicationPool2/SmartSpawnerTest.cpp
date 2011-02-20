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
		BackgroundEventLoop bg;
		
		ApplicationPool2_SmartSpawnerTest() {
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
				command,
				make_shared<RandomGenerator>(),
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
		
		// No exception.
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
			fail("Exception expected");
		} catch (const IOException &) {
			// Pass.
		} catch (const SystemException &) {
			// Pass.
		}
	}
}
