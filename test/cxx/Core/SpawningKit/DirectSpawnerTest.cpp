#include <TestSupport.h>
#include <jsoncpp/json.h>
#include <Core/ApplicationPool/Options.h>
#include <Core/SpawningKit/DirectSpawner.h>
#include <FileDescriptor.h>
#include <Utils/IOUtils.h>
#include <algorithm>
#include <fcntl.h>

using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_DirectSpawnerTest {
		SpawningKit::Context::Schema schema;
		SpawningKit::Context context;
		SpawningKit::Result result;

		Core_SpawningKit_DirectSpawnerTest()
			: context(schema)
		{
			context.resourceLocator = resourceLocator;
			context.integrationMode = "standalone";
			context.finalize();

			setLogLevel(LVL_WARN);
			setPrintAppOutputAsDebuggingMessages(true);
		}

		~Core_SpawningKit_DirectSpawnerTest() {
			setLogLevel(DEFAULT_LOG_LEVEL);
			setPrintAppOutputAsDebuggingMessages(false);
			unlink("stub/wsgi/passenger_wsgi.pyc");
		}

		boost::shared_ptr<DirectSpawner> createSpawner(const SpawningKit::AppPoolOptions &options) {
			return boost::make_shared<DirectSpawner>(&context);
		}

		SpawningKit::AppPoolOptions createOptions() {
			SpawningKit::AppPoolOptions options;
			options.spawnMethod = "direct";
			options.loadShellEnvvars = false;
			return options;
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(Core_SpawningKit_DirectSpawnerTest, 90);

	#include "SpawnerTestCases.cpp"

	TEST_METHOD(82) {
		set_test_name("Test that everything works correctly if the app re-execs() itself");
		// https://code.google.com/p/phusion-passenger/issues/detail?id=842#c19
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.startCommand = "ruby\t" "start.rb\t" "--execself";
		options.startupFile  = "start.rb";
		SpawnerPtr spawner = createSpawner(options);
		result = spawner->spawn(options);
		ensure_equals(result.sockets.size(), 1u);

		FileDescriptor fd(connectToServer(result.sockets[0].address,
			__FILE__, __LINE__), NULL, 0);
		writeExact(fd, "ping\n");
		ensure_equals(readAll(fd), "pong\n");
	}
}
