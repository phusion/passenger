#include <TestSupport.h>
#include <jsoncpp/json.h>
#include <Core/ApplicationPool/Options.h>
#include <Core/SpawningKit/SmartSpawner.h>
#include <LoggingKit/LoggingKit.h>
#include <LoggingKit/Context.h>
#include <FileDescriptor.h>
#include <IOTools/IOUtils.h>
#include <unistd.h>
#include <climits>
#include <signal.h>
#include <fcntl.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_SmartSpawnerTest: public TestBase {
		WrapperRegistry::Registry wrapperRegistry;
		SpawningKit::Context::Schema schema;
		SpawningKit::Context context;
		SpawningKit::Result result;

		Core_SpawningKit_SmartSpawnerTest()
			: context(schema)
		{
			wrapperRegistry.finalize();
			context.resourceLocator = resourceLocator;
			context.wrapperRegistry = &wrapperRegistry;
			context.integrationMode = "standalone";
			context.spawnDir = getSystemTempDir();
			context.finalize();

			Json::Value config;
			vector<ConfigKit::Error> errors;
			LoggingKit::ConfigChangeRequest req;
			config["level"] = "warn";
			config["app_output_log_level"] = "debug";

			if (LoggingKit::context->prepareConfigChange(config, errors, req)) {
				LoggingKit::context->commitConfigChange(req);
			} else {
				P_BUG("Error configuring LoggingKit: " << ConfigKit::toString(errors));
			}
		}

		~Core_SpawningKit_SmartSpawnerTest() {
			Json::Value config;
			vector<ConfigKit::Error> errors;
			LoggingKit::ConfigChangeRequest req;
			config["level"] = DEFAULT_LOG_LEVEL_NAME;
			config["app_output_log_level"] = DEFAULT_APP_OUTPUT_LOG_LEVEL_NAME;

			if (LoggingKit::context->prepareConfigChange(config, errors, req)) {
				LoggingKit::context->commitConfigChange(req);
			} else {
				P_BUG("Error configuring LoggingKit: " << ConfigKit::toString(errors));
			}

			unlink("stub/wsgi/passenger_wsgi.pyc");
		}

		boost::shared_ptr<SmartSpawner> createSpawner(const SpawningKit::AppPoolOptions &options, bool exitImmediately = false) {
			char buf[PATH_MAX + 1];
			getcwd(buf, PATH_MAX);

			vector<string> command;
			command.push_back("ruby");
			command.push_back(string(buf) + "/support/placebo-preloader.rb");
			if (exitImmediately) {
				command.push_back("exit-immediately");
			}

			return boost::make_shared<SmartSpawner>(&context, command,
				options);
		}

		SpawningKit::AppPoolOptions createOptions() {
			SpawningKit::AppPoolOptions options;
			options.appType     = "directly-through-start-command";
			options.spawnMethod = "smart";
			options.loadShellEnvvars = false;
			return options;
		}
	};

	DEFINE_TEST_GROUP(Core_SpawningKit_SmartSpawnerTest);

	#include "SpawnerTestCases.cpp"

	TEST_METHOD(10) {
		set_test_name("If the preloader has crashed then SmartSpawner will "
			"restart it and try again");
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.appStartCommand = "ruby start.rb";
		options.startupFile  = "start.rb";
		boost::shared_ptr<SmartSpawner> spawner = createSpawner(options);

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		spawner->spawn(options);

		kill(spawner->getPreloaderPid(), SIGTERM);
		// Give it some time to exit.
		usleep(300000);

		// No exception at next spawn.
		spawner->spawn(options);
	}

	TEST_METHOD(11) {
		set_test_name("If the preloader still crashes after the restart then "
			"SmartSpawner will throw an exception");
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.appStartCommand = "ruby start.rb";
		options.startupFile  = "start.rb";

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		boost::shared_ptr<SmartSpawner> spawner = createSpawner(options, true);
		try {
			spawner->spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &) {
			// Pass.
		}
	}

	TEST_METHOD(12) {
		set_test_name("If the preloader didn't start within the timeout"
			" then it's killed and an exception is thrown, which"
			" contains whatever it printed to stdout and stderr");

		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.appStartCommand = "ruby start.rb";
		options.startupFile  = "start.rb";
		options.startTimeout = 100;

		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world; sleep 60");
		SmartSpawner spawner(&context, preloaderCommand, options);

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorCategory(), SpawningKit::TIMEOUT_ERROR);
			if (e.getStdoutAndErrData().find("hello world\n") == string::npos) {
				// This might be caused by the machine being too slow.
				// Try again with a higher timeout.
				options.startTimeout = 10000;

				SmartSpawner spawner2(&context, preloaderCommand, options);
				try {
					spawner2.spawn(options);
					fail("SpawnException expected");
				} catch (const SpawnException &e2) {
					ensure_equals(e2.getErrorCategory(), SpawningKit::TIMEOUT_ERROR);
					if (e2.getStdoutAndErrData().find("hello world\n") == string::npos) {
						fail(("Unexpected stdout/stderr output:\n" +
							e2.getStdoutAndErrData()).c_str());
					}
				}
			}
		}
	}

	TEST_METHOD(13) {
		set_test_name("If the preloader crashed during startup,"
			" then the resulting exception contains the stdout"
			" and stderr output");

		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.appStartCommand = "ruby start.rb";
		options.startupFile  = "start.rb";

		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world; exit 1");
		SmartSpawner spawner(&context, preloaderCommand, options);

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorCategory(), SpawningKit::INTERNAL_ERROR);
			ensure(e.getStdoutAndErrData().find("hello world\n") != string::npos);
		}
	}

	TEST_METHOD(14) {
		set_test_name("If the preloader encountered an error,"
			" then the resulting exception"
			" takes note of the process's environment variables");

		string envvars = modp::b64_encode("PASSENGER_FOO\0foo\0",
			sizeof("PASSENGER_FOO\0foo\0") - 1);
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.appStartCommand = "ruby start.rb";
		options.startupFile  = "start.rb";
		options.environmentVariables = envvars;

		vector<string> preloaderCommand;
		preloaderCommand.push_back("bash");
		preloaderCommand.push_back("-c");
		preloaderCommand.push_back("echo hello world >&2; exit 1");
		SmartSpawner spawner(&context, preloaderCommand, options);

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSubprocessEnvvars(), "PASSENGER_FOO=foo\n"));
		}
	}
}
