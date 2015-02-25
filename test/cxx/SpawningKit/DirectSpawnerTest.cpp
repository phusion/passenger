#include <TestSupport.h>
#include <SpawningKit/DirectSpawner.h>
#include <FileDescriptor.h>
#include <Utils/json.h>
#include <Utils/IOUtils.h>
#include <algorithm>
#include <fcntl.h>

using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct SpawningKit_DirectSpawnerTest {
		ConfigPtr config;
		OutputHandler gatherOutput;
		string gatheredOutput;
		boost::mutex gatheredOutputSyncher;
		Result result;

		SpawningKit_DirectSpawnerTest() {
			config = boost::make_shared<Config>();
			config->resourceLocator = resourceLocator;
			config->finalize();

			gatherOutput = boost::bind(&SpawningKit_DirectSpawnerTest::_gatherOutput, this, _1, _2);
			setLogLevel(LVL_WARN);
			setPrintAppOutputAsDebuggingMessages(true);
		}

		~SpawningKit_DirectSpawnerTest() {
			setLogLevel(DEFAULT_LOG_LEVEL);
			setPrintAppOutputAsDebuggingMessages(false);
			unlink("stub/wsgi/passenger_wsgi.pyc");
		}

		boost::shared_ptr<DirectSpawner> createSpawner(const Options &options) {
			return boost::make_shared<DirectSpawner>(config);
		}

		Options createOptions() {
			Options options;
			options.spawnMethod = "direct";
			options.loadShellEnvvars = false;
			return options;
		}

		void _gatherOutput(const char *data, unsigned int size) {
			boost::lock_guard<boost::mutex> l(gatheredOutputSyncher);
			gatheredOutput.append(data, size);
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(SpawningKit_DirectSpawnerTest, 90);

	#include "SpawnerTestCases.cpp"

	TEST_METHOD(80) {
		set_test_name("If the application didn't start within the timeout "
			"then whatever was written to stderr is used as the "
			"SpawnException error page");
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\t" "-e\t" "print STDERR \"hello world\\n\"; sleep(60)";
		options.startupFile  = ".";
		options.startTimeout = 100;

		DirectSpawner spawner(config);
		setLogLevel(LVL_CRIT);

		EVENTUALLY(5,
			try {
				spawner.spawn(options);
				fail("Timeout expected");
			} catch (const SpawnException &e) {
				ensure_equals(e.getErrorKind(),
					SpawnException::APP_STARTUP_TIMEOUT);
				result = e.getErrorPage().find("hello world\n") != string::npos;
				if (!result) {
					// It didn't work, maybe because the server is too busy.
					// Try again with higher timeout.
					options.startTimeout = std::min<unsigned int>(
						options.startTimeout * 2, 1000);
				}
			}
		);
	}

	TEST_METHOD(81) {
		set_test_name("If the application crashed during startup without returning "
			"a proper error response, then its stderr output is used "
			"as error response instead");
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\t" "-e\t" "print STDERR \"hello world\\n\"";
		options.startupFile  = ".";

		DirectSpawner spawner(config);
		setLogLevel(LVL_CRIT);

		try {
			spawner.spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_ERROR);
			ensure(e.getErrorPage().find("hello world\n") != string::npos);
		}
	}

	TEST_METHOD(82) {
		set_test_name("Test that everything works correctly if the app re-execs() itself");
		// https://code.google.com/p/phusion-passenger/issues/detail?id=842#c19
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb\t" "--execself";
		options.startupFile  = "start.rb";
		SpawnerPtr spawner = createSpawner(options);
		result = spawner->spawn(options);
		ensure_equals(result.sockets.size(), 1u);

		FileDescriptor fd(connectToServer(result.sockets[0].address));
		writeExact(fd, "ping\n");
		ensure_equals(readAll(fd), "pong\n");
	}
}
