#include <TestSupport.h>
#include <jsoncpp/json.h>
#include <Core/ApplicationPool/Options.h>
#include <Core/SpawningKit/DirectSpawner.h>
#include <LoggingKit/Context.h>
#include <FileDescriptor.h>
#include <IOTools/IOUtils.h>
#include <algorithm>
#include <fcntl.h>

using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_DirectSpawnerTest: public TestBase {
		WrapperRegistry::Registry wrapperRegistry;
		SpawningKit::Context::Schema schema;
		SpawningKit::Context context;
		SpawningKit::Result result;

		Core_SpawningKit_DirectSpawnerTest()
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

		~Core_SpawningKit_DirectSpawnerTest() {
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

		boost::shared_ptr<DirectSpawner> createSpawner(const SpawningKit::AppPoolOptions &options) {
			return boost::make_shared<DirectSpawner>(&context);
		}

		SpawningKit::AppPoolOptions createOptions() {
			SpawningKit::AppPoolOptions options;
			options.appType     = "directly-through-start-command";
			options.spawnMethod = "direct";
			options.loadShellEnvvars = false;
			return options;
		}
	};

	DEFINE_TEST_GROUP(Core_SpawningKit_DirectSpawnerTest);

	#include "SpawnerTestCases.cpp"

	TEST_METHOD(10) {
		set_test_name("Test that everything works correctly if the app re-execs() itself");
		// https://code.google.com/p/phusion-passenger/issues/detail?id=842#c19
		SpawningKit::AppPoolOptions options = createOptions();
		options.appRoot      = "stub/rack";
		options.appStartCommand = "ruby start.rb --execself";
		options.startupFile  = "start.rb";
		SpawnerPtr spawner = createSpawner(options);
		result = spawner->spawn(options);
		ensure_equals(result.sockets.size(), 1u);

		FileDescriptor fd(connectToServer(result.sockets[0].address,
			__FILE__, __LINE__), NULL, 0);
		writeExact(fd, "ping\n");
		ensure_equals(readAll(fd, 1024).first, "pong\n");
	}
}
