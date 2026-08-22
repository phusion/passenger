#include <TestSupport.h>
#include <BackgroundEventLoop.h>
#include <ServerKit/Context.h>
#include <ConfigKit/Store.h>
#include <Watchdog/Config.h>
#include <Watchdog/ApiServer.h>

using namespace Passenger;
using namespace Passenger::Watchdog;
using namespace std;

namespace tut {
	struct Watchdog_ApiServerTest: public TestBase {
		BackgroundEventLoop bg;
		ServerKit::Schema skSchema;
		ServerKit::Context context;
		Watchdog::Schema watchdogSchema;

		Watchdog_ApiServerTest()
			: bg(false, true),
			  context(skSchema)
		{
			context.libev = bg.safe;
			context.libuv = bg.libuv_loop;
			context.initialize();
		}
	};

	DEFINE_TEST_GROUP(Watchdog_ApiServerTest);

	TEST_METHOD(1) {
		set_test_name("The account database is non-empty in the default config, "
			"because a ro_admin and admin account are always seeded on top of "
			"the (by default empty) user-configured authorizations "
			"(regression test for SEC-75753)");

		Json::Value initialValues;
		initialValues["passenger_root"] = "/tmp";
		ConfigKit::Store watchdogConfig(watchdogSchema, initialValues);

		// Mirrors what initializeApiServer() in WatchdogMain.cpp always seeds,
		// regardless of what the user configured (by default nothing).
		Json::Value roAdmin;
		roAdmin["username"] = "ro_admin";
		roAdmin["password"] = "ro_admin_password";
		roAdmin["level"] = "readonly";
		Json::Value admin;
		admin["username"] = "admin";
		admin["password"] = "admin_password";
		admin["level"] = "full";
		Json::Value seededAuthorizations(Json::arrayValue);
		seededAuthorizations.append(roAdmin);
		seededAuthorizations.append(admin);

		// Calls the same function WatchdogMain.cpp calls, so that a
		// regression in its key-assignment logic makes this test fail.
		Json::Value apiServerConfig = ApiServer::buildConfig(watchdogConfig,
			seededAuthorizations, "fd_passing_password");

		ApiServer::ApiServer server(&context, watchdogSchema.apiServer.schema,
			apiServerConfig, watchdogSchema.apiServer.translator);

		// The server was never listen()ed on, so this finishes synchronously
		// and satisfies the FINISHED_SHUTDOWN assertion in ~BaseServer(). Must
		// run before the ensure() below, which throws on failure: otherwise a
		// failing assertion would skip this and crash the whole test binary
		// via ~BaseServer()'s own serverState assertion during unwinding.
		server.shutdown(true);

		ensure("the API account database must not be empty",
			!server.getApiAccountDatabase().empty());
	}
}
