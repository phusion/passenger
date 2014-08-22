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
		ProcessPtr process;
		PipeWatcher::DataCallback gatherOutput;
		string gatheredOutput;
		boost::mutex gatheredOutputSyncher;

		ApplicationPool2_DirectSpawnerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			PipeWatcher::onData = PipeWatcher::DataCallback();
			gatherOutput = boost::bind(&ApplicationPool2_DirectSpawnerTest::_gatherOutput, this, _1, _2);
			setLogLevel(LVL_ERROR); // TODO: change to LVL_WARN
			setPrintAppOutputAsDebuggingMessages(true);
		}

		~ApplicationPool2_DirectSpawnerTest() {
			setLogLevel(DEFAULT_LOG_LEVEL);
			setPrintAppOutputAsDebuggingMessages(false);
			unlink("stub/wsgi/passenger_wsgi.pyc");
			PipeWatcher::onData = PipeWatcher::DataCallback();
		}

		boost::shared_ptr<DirectSpawner> createSpawner(const Options &options) {
			return boost::make_shared<DirectSpawner>(
				generation, make_shared<SpawnerConfig>(*resourceLocator));
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

	DEFINE_TEST_GROUP_WITH_LIMIT(ApplicationPool2_DirectSpawnerTest, 90);

	#include "SpawnerTestCases.cpp"

	TEST_METHOD(80) {
		// If the application didn't start within the timeout
		// then whatever was written to stderr is used as the
		// SpawnException error page.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\t" "-e\t" "print STDERR \"hello world\\n\"; sleep(60)";
		options.startupFile  = ".";
		options.startTimeout = 300;

		DirectSpawner spawner(generation, make_shared<SpawnerConfig>(*resourceLocator));
		setLogLevel(LVL_CRIT);

		try {
			process = spawner.spawn(options);
			process->requiresShutdown = false;
			fail("Timeout expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_TIMEOUT);
			ensure(e.getErrorPage().find("hello world\n") != string::npos);
		}
	}

	TEST_METHOD(81) {
		// If the application crashed during startup without returning
		// a proper error response, then its stderr output is used
		// as error response instead.
		Options options = createOptions();
		options.appRoot      = "stub";
		options.startCommand = "perl\t" "-e\t" "print STDERR \"hello world\\n\"";
		options.startupFile  = ".";

		DirectSpawner spawner(generation, make_shared<SpawnerConfig>(*resourceLocator));
		setLogLevel(LVL_CRIT);

		try {
			process = spawner.spawn(options);
			process->requiresShutdown = false;
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getErrorKind(),
				SpawnException::APP_STARTUP_ERROR);
			ensure(e.getErrorPage().find("hello world\n") != string::npos);
		}
	}

	TEST_METHOD(82) {
		SHOW_EXCEPTION_BACKTRACE(
		// Test that everything works correctly if the app re-execs() itself.
		// https://code.google.com/p/phusion-passenger/issues/detail?id=842#c19
		Options options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb\t" "--execself";
		options.startupFile  = "start.rb";
		SpawnerPtr spawner = createSpawner(options);
		process = spawner->spawn(options);
		process->requiresShutdown = false;
		ensure_equals(process->sockets->size(), 1u);

		Connection conn = process->sockets->front().checkoutConnection();
		ScopeGuard guard(boost::bind(checkin, process, &conn));
		writeExact(conn.fd, "ping\n");
		ensure_equals(readAll(conn.fd), "pong\n");
		);
	}
}
