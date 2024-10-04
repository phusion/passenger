#include <TestSupport.h>
#include <Core/SpawningKit/Handshake/Prepare.h>
#include <FileTools/FileManip.h>
#include <SystemTools/UserDatabase.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct SpawnEnvSetupperTest: public TestBase {
		WrapperRegistry::Registry wrapperRegistry;
		SpawningKit::Context::Schema schema;
		SpawningKit::Context context;
		SpawningKit::Config config;
		boost::shared_ptr<HandshakeSession> session;

		SpawnEnvSetupperTest()
			: context(schema)
		{
			wrapperRegistry.finalize();
			context.resourceLocator = resourceLocator;
			context.wrapperRegistry = &wrapperRegistry;
			context.integrationMode = "standalone";
			context.spawnDir = getSystemTempDir();
			context.finalize();

			string user = lookupSystemUsernameByUid(getuid());
			string group = lookupSystemGroupnameByGid(getgid());

			config.startCommand = "true";
			config.appGroupName = "appgroup";
			config.appRoot = "tmp.wsgi";
			config.startupFile = "tmp.wsgi/passenger_wsgi.py";
			config.appType = "wsgi";
			config.spawnMethod = "direct";
			config.bindAddress = "127.0.0.1";
			config.user = user;
			config.group = group;
			config.internStrings();
		}

		void init(JourneyType type, const Json::Value &extraArgs = Json::Value()) {
			vector<StaticString> errors;
			ensure("Config is valid", config.validate(errors));
			session = boost::make_shared<HandshakeSession>(context, config, type);

			session->journey.setStepInProgress(SPAWNING_KIT_PREPARATION);
			HandshakePrepare(*session, extraArgs).execute();

			session->journey.setStepInProgress(SPAWNING_KIT_HANDSHAKE_PERFORM);
			session->journey.setStepInProgress(SUBPROCESS_BEFORE_FIRST_EXEC);
		}

		bool execute(const StaticString &mode, bool quiet = false) {
			string command = escapeShell(resourceLocator->findSupportBinary(AGENT_EXE))
				+ " spawn-env-setupper "
				+ escapeShell(session->workDir->getPath())
				+ " " + mode;
			if (quiet) {
				command.append(" >/dev/null 2>/dev/null");
			}
			return runShellCommand(command) == 0;
		}
	};

	DEFINE_TEST_GROUP(SpawnEnvSetupperTest);


	/***** Dumping information *****/

	TEST_METHOD(1) {
		set_test_name("It sets the SUBPROCESS_BEFORE_FIRST_EXEC to the PERFORMED state");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_before_first_exec/state"),
			"STEP_PERFORMED");
	}

	TEST_METHOD(2) {
		set_test_name("It dumps environment variables into the work dir");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		string envvars = unsafeReadFile(session->workDir->getPath()
			+ "/envdump/envvars");
		ensure(containsSubstring(envvars, "PATH="));
	}

	TEST_METHOD(3) {
		set_test_name("It dumps user info into the work dir");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		string envvars = unsafeReadFile(session->workDir->getPath()
			+ "/envdump/user_info");
		ensure(containsSubstring(envvars, "uid="));
	}

	TEST_METHOD(4) {
		set_test_name("It dumps ulimits info into the work dir");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		string envvars = unsafeReadFile(session->workDir->getPath()
			+ "/envdump/ulimits");
		ensure(containsSubstring(envvars, "open files")
			|| containsSubstring(envvars, "nofiles"));
	}

	TEST_METHOD(5) {
		set_test_name("It sets default environment variables such as PASSENGER_APP_ENV");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		string envvars = unsafeReadFile(session->workDir->getPath()
			+ "/envdump/envvars");
		ensure(containsSubstring(envvars, "PASSENGER_APP_ENV="));
	}


	/***** Command execution and environment modification *****/

	TEST_METHOD(10) {
		set_test_name("It runs the start command inside the app root");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.startCommand = "touch foo";
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure("Start command succeeds", fileExists("tmp.wsgi/foo"));
	}

	TEST_METHOD(11) {
		set_test_name("It sets the environment variables specified in the config");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.environmentVariables.insert("MY_VAR", "value");
		config.startCommand = "echo 'import os, json; print(json.dumps(dict(os.environ)))' | python > env.json";
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		Json::Value doc;
		ensure("Load environment JSON dump",
			Json::Reader().parse(unsafeReadFile("tmp.wsgi/env.json"), doc));
		ensure_equals(doc["MY_VAR"].asString(), "value");
	}

	TEST_METHOD(12) {
		set_test_name("It switches to the corresponding user and group, if possible");

		if (geteuid() != 0) {
			return;
		}

		string user = testConfig["normal_user_1"].asString();
		string group = testConfig["normal_group_1"].asString();

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		runShellCommand("chown -R " + user + ":" + group + " tmp.wsgi");
		config.user = user;
		config.group = group;
		config.startCommand = "sh -c 'id -un > user.txt && id -gn > group.txt'";
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure_equals(strip(unsafeReadFile("tmp.wsgi/user.txt")), user);
		ensure_equals(strip(unsafeReadFile("tmp.wsgi/group.txt")), group);
	}

	TEST_METHOD(13) {
		set_test_name("It sets ulimits to the corresponding settings, if possible");

		if (geteuid() != 0) {
			return;
		}

		string user = testConfig["normal_user_1"].asString();
		string group = testConfig["normal_group_1"].asString();

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		runShellCommand("chown -R " + user + ":" + group + " tmp.wsgi");
		config.fileDescriptorUlimit = 128;
		config.startCommand = "sh -c 'ulimit -n > openfiles.txt'";
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure_equals(strip(unsafeReadFile("tmp.wsgi/openfiles.txt")), "128");
	}


	/***** Step state recording *****/

	TEST_METHOD(20) {
		set_test_name("It sets the SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL"
			" and SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL steps to the PERFORMED state");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_spawn_env_setupper_before_shell/state"),
			"STEP_PERFORMED");
		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_spawn_env_setupper_after_shell/state"),
			"STEP_PERFORMED");
	}

	TEST_METHOD(21) {
		set_test_name("If loadShellEnvvars is true, it sets SUBPROCESS_OS_SHELL"
			" step to the PERFORMED state");

		// This test is known to fail erroneously if all of
		// the following conditions apply:
		// - You are running this test with root privileges.
		// - The root user's shell is not supported by
		//   the shouldLoadShellEnvvars() function in SpawnEnvSetupperMain.cpp.

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.loadShellEnvvars = true;
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_os_shell/state"),
			"STEP_PERFORMED");
	}

	TEST_METHOD(22) {
		set_test_name("If loadShellEnvvars is false, it keeps the SUBPROCESS_OS_SHELL"
			" step in the NOT_STARTED state");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.loadShellEnvvars = false;
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure(!fileExists(session->workDir->getPath()
			+ "/response/steps/subprocess_os_shell"));
	}

	TEST_METHOD(23) {
		set_test_name("If startsUsingWrapper is true,"
			" and the start command can be executed,"
			" then it sets the SUBPROCESS_EXEC_WRAPPER step to the IN_PROGRESS state");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.startsUsingWrapper = true;
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_exec_wrapper/state"),
			"STEP_IN_PROGRESS");
	}

	TEST_METHOD(24) {
		set_test_name("If startsUsingWrapper is true,"
			" and the start command cannot be executed,"
			" then it sets the SUBPROCESS_EXEC_WRAPPER step to the ERRORED state");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.startsUsingWrapper = true;
		Json::Value extraArgs;
		extraArgs["_bin_sh_path"] = "/non-existant-command";
		init(SPAWN_DIRECTLY, extraArgs);
		ensure("SpawnEnvSetupper fails", !execute("--before", true));

		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_exec_wrapper/state"),
			"STEP_ERRORED");
	}

	TEST_METHOD(25) {
		set_test_name("If startsUsingWrapper is false,"
			" and the start command can be executed,"
			" then it sets the SUBPROCESS_APP_LOAD_OR_EXEC step to the IN_PROGRESS state");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.startsUsingWrapper = false;
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper succeeds", execute("--before"));

		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_app_load_or_exec/state"),
			"STEP_IN_PROGRESS");
	}

	TEST_METHOD(26) {
		set_test_name("If startsUsingWrapper is false,"
			" and the start command cannot be executed,"
			" then it sets the SUBPROCESS_APP_LOAD_OR_EXEC step to the ERRORED state");

		TempDirCopy dir("stub/wsgi", "tmp.wsgi");
		config.startsUsingWrapper = false;
		Json::Value extraArgs;
		extraArgs["_bin_sh_path"] = "/non-existant-command";
		init(SPAWN_DIRECTLY, extraArgs);
		ensure("SpawnEnvSetupper fails", !execute("--before", true));

		ensure_equals(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/steps/subprocess_app_load_or_exec/state"),
			"STEP_ERRORED");
	}


	/***** Miscellaneous *****/

	TEST_METHOD(30) {
		set_test_name("If the user does not have a access to one of the app root's"
			" parent directories, or the app root itself, then it provides a clear"
			" error message explaining this (level 1)");

		if (geteuid() == 0) {
			return;
		}

		runShellCommand("mkdir -p tmp.check/a/b/c");
		TempDirCopy dir("stub/wsgi", "tmp.check/a/b/c/d");
		TempDir dir2("tmp.check");
		runShellCommand("chmod 000 tmp.check/a/b/c/d");
		runShellCommand("chmod 600 tmp.check/a/b/c");
		runShellCommand("chmod 600 tmp.check/a");

		char buffer[PATH_MAX];
		string cwd = getcwd(buffer, sizeof(buffer));

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		config.appRoot = "tmp.check/a/b/c/d";
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper fails", !execute("--before", true));

		ensure(containsSubstring(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/error/summary"),
			"Directory '" + cwd + "/tmp.check/a' is inaccessible"));

		#if 0
		try {
			spawner->spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure("(1)", containsSubstring(e.getErrorPage(),
				"the parent directory '" + cwd + "/tmp.check/a' has wrong permissions"));
		}

		runShellCommand("chmod 700 tmp.check/a");
		try {
			spawner->spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure("(2)", containsSubstring(e.getErrorPage(),
				"the parent directory '" + cwd + "/tmp.check/a/b/c' has wrong permissions"));
		}

		runShellCommand("chmod 700 tmp.check/a/b/c");
		try {
			spawner->spawn(options);
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure("(3)", containsSubstring(e.getErrorPage(),
				"However this directory is not accessible because it has wrong permissions."));
		}

		runShellCommand("chmod 700 tmp.check/a/b/c/d");
		spawner->spawn(options); // Should not throw.
		#endif
	}

	TEST_METHOD(31) {
		set_test_name("If the user does not have a access to one of the app root's"
			" parent directories, or the app root itself, then it provides a clear"
			" error message explaining this (level 2)");

		if (geteuid() == 0) {
			return;
		}

		runShellCommand("mkdir -p tmp.check/a/b/c");
		TempDirCopy dir("stub/wsgi", "tmp.check/a/b/c/d");
		TempDir dir2("tmp.check");
		runShellCommand("chmod 000 tmp.check/a/b/c/d");
		runShellCommand("chmod 600 tmp.check/a/b/c");
		runShellCommand("chmod 700 tmp.check/a");

		char buffer[PATH_MAX];
		string cwd = getcwd(buffer, sizeof(buffer));

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		config.appRoot = "tmp.check/a/b/c/d";
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper fails", !execute("--before", true));

		ensure(containsSubstring(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/error/summary"),
			"Directory '" + cwd + "/tmp.check/a/b/c' is inaccessible"));
	}

	TEST_METHOD(32) {
		set_test_name("If the user does not have a access to one of the app root's"
			" parent directories, or the app root itself, then it provides a clear"
			" error message explaining this (level 3)");

		if (geteuid() == 0) {
			return;
		}

		runShellCommand("mkdir -p tmp.check/a/b/c");
		TempDirCopy dir("stub/wsgi", "tmp.check/a/b/c/d");
		TempDir dir2("tmp.check");
		runShellCommand("chmod 700 tmp.check/a/b/c/d");
		runShellCommand("chmod 600 tmp.check/a/b/c");
		runShellCommand("chmod 700 tmp.check/a");

		char buffer[PATH_MAX];
		string cwd = getcwd(buffer, sizeof(buffer));

		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}

		config.appRoot = "tmp.check/a/b/c/d";
		init(SPAWN_DIRECTLY);
		ensure("SpawnEnvSetupper fails", !execute("--before", true));

		ensure(containsSubstring(
			unsafeReadFile(session->workDir->getPath()
				+ "/response/error/summary"),
			"Directory '" + cwd + "/tmp.check/a/b/c' is inaccessible"));
	}
}
