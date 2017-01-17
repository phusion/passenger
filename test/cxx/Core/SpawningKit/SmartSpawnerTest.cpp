#include <TestSupport.h>
#include <jsoncpp/json.h>
#include <Core/SpawningKit/SmartSpawner.h>
#include <Logging.h>
#include <FileDescriptor.h>
#include <Utils/IOUtils.h>
#include <unistd.h>
#include <climits>
#include <signal.h>
#include <fcntl.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_SmartSpawnerTest {
		ConfigPtr config;
		OutputHandler gatherOutput;
		string gatheredOutput;
		boost::mutex gatheredOutputSyncher;
		SpawningKit::Result result;

		Core_SpawningKit_SmartSpawnerTest() {
			config = boost::make_shared<Config>();
			config->resourceLocator = resourceLocator;
			config->finalize();

			gatherOutput = boost::bind(&Core_SpawningKit_SmartSpawnerTest::_gatherOutput, this, _1, _2);
			setLogLevel(LVL_WARN);
			setPrintAppOutputAsDebuggingMessages(true);
		}

		~Core_SpawningKit_SmartSpawnerTest() {
			setLogLevel(DEFAULT_LOG_LEVEL);
			setPrintAppOutputAsDebuggingMessages(false);
			unlink("stub/wsgi/passenger_wsgi.pyc");
		}

		boost::shared_ptr<SmartSpawner> createSpawner(const Options &options, bool exitImmediately = false) {
			char buf[PATH_MAX + 1];
			getcwd(buf, PATH_MAX);

			vector<string> command;
			command.push_back("ruby");
			command.push_back(string(buf) + "/support/placebo-preloader.rb");
			if (exitImmediately) {
				command.push_back("exit-immediately");
			}

			return boost::make_shared<SmartSpawner>(command,
				options, config);
		}

		Options createOptions() {
			Options options;
			options.spawnMethod = "smart";
			options.loadShellEnvvars = false;
			return options;
		}

		void _gatherOutput(const char *data, unsigned int size) {
			boost::lock_guard<boost::mutex> l(gatheredOutputSyncher);
			gatheredOutput.append(data, size);
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(Core_SpawningKit_SmartSpawnerTest, 90);

	#include "SpawnerTestCases.cpp"

	TEST_METHOD(80) {
		set_test_name("If the preloader has crashed then SmartSpawner will "
			"restart it and try again");
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb";
		options.startupFile  = "start.rb";
		boost::shared_ptr<SmartSpawner> spawner = createSpawner(options);
		setLogLevel(LVL_CRIT);
		spawner->spawn(options);

		kill(spawner->getPreloaderPid(), SIGTERM);
		// Give it some time to exit.
		usleep(300000);

		// No exception at next spawn.
		spawner->spawn(options);
	}

	TEST_METHOD(81) {
		set_test_name("If the preloader still crashes after the restart then "
			"SmartSpawner will throw an exception");
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb";
		options.startupFile  = "start.rb";
		setLogLevel(LVL_CRIT);
		boost::shared_ptr<SmartSpawner> spawner = createSpawner(options, true);
		try {
			spawner->spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &) {
			// Pass.
		}
	}

	TEST_METHOD(82) {
		set_test_name("If the preloader didn't start within the timeout "
			"then it's killed and an exception is thrown, with "
			"whatever stderr output as error page");
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb";
		options.startupFile  = "start.rb";
		options.startTimeout = 100;

		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world >&2; sleep 60");
		SmartSpawner spawner(preloaderCommand, options, config);
		setLogLevel(LVL_CRIT);

		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::PRELOADER_STARTUP_TIMEOUT);
			if (e.getErrorPage().find("hello world\n") == string::npos) {
				// This might be caused by the machine being too slow.
				// Try again with a higher timeout.
#if defined(BOOST_OS_MACOS)
				options.startTimeout = 3000;
#else
				options.startTimeout = 1000;
#endif
				SmartSpawner spawner2(preloaderCommand, options, config);
				try {
					spawner2.spawn(options);
					fail("SpawnException expected");
				} catch (const SpawnException &e2) {
					ensure_equals(e2.getErrorKind(),
						SpawnException::PRELOADER_STARTUP_TIMEOUT);
					if (e2.getErrorPage().find("hello world\n") == string::npos) {
						fail(("Unexpected error page:\n" + e2.getErrorPage()).c_str());
					}
				}
			}
		}
	}

	TEST_METHOD(83) {
		set_test_name("If the preloader crashed during startup without returning "
			"a proper error response, then its stderr output is used "
			"as error response instead");
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb";
		options.startupFile  = "start.rb";

		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world >&2");
		SmartSpawner spawner(preloaderCommand, options, config);
		setLogLevel(LVL_CRIT);

		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::PRELOADER_STARTUP_ERROR);
			ensure(e.getErrorPage().find("hello world\n") != string::npos);
		}
	}

	TEST_METHOD(84) {
		set_test_name("If the preloader encountered an error, then the resulting SpawnException "
			"takes note of the process's environment variables");
		string envvars = modp::b64_encode("PASSENGER_FOO\0foo\0",
			sizeof("PASSENGER_FOO\0foo\0") - 1);
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb";
		options.startupFile  = "start.rb";
		options.environmentVariables = envvars;

		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world >&2");
		SmartSpawner spawner(preloaderCommand, options, config);
		setLogLevel(LVL_CRIT);

		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e["envvars"], "PASSENGER_FOO=foo\n"));
		}
	}

	TEST_METHOD(85) {
		set_test_name("The spawned process can still write to its stderr "
			"after the SmartSpawner has been destroyed");
		DeleteFileEventually d("tmp.output");
		config->outputHandler = gatherOutput;
		Options options = createOptions();
		options.appRoot = "stub/rack";
		options.appType = "rack";

		{
			vector<string> preloaderCommand;
			preloaderCommand.push_back("ruby");
			preloaderCommand.push_back(resourceLocator->getHelperScriptsDir() +
				"/rack-preloader.rb");
			SmartSpawner spawner(preloaderCommand, options, config);
			result = spawner.spawn(options);
		}

		const char header[] =
			"REQUEST_METHOD\0GET\0"
			"PATH_INFO\0/print_stderr\0";

		FileDescriptor fd(connectToServer(result["sockets"][0]["address"].asCString(),
			__FILE__, __LINE__), NULL, 0);
		writeScalarMessage(fd, header, sizeof(header) - 1);
		shutdown(fd, SHUT_WR);
		readAll(fd);
		EVENTUALLY(2,
			boost::lock_guard<boost::mutex> l(gatheredOutputSyncher);
			result = gatheredOutput.find("hello world!\n") != string::npos;
		);
	}
}
