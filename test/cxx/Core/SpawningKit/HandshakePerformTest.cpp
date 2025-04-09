#include <TestSupport.h>
#include <Core/SpawningKit/Handshake/Prepare.h>
#include <Core/SpawningKit/Handshake/Perform.h>
#include <LoggingKit/Context.h>
#include <SystemTools/UserDatabase.h>
#include <boost/bind/bind.hpp>
#include <cstdio>
#include <IOTools/IOUtils.h>

using namespace std;
using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_HandshakePerformTest: public TestBase {
		WrapperRegistry::Registry wrapperRegistry;
		SpawningKit::Context::Schema schema;
		SpawningKit::Context context;
		SpawningKit::Config config;
		boost::shared_ptr<HandshakeSession> session;
		pid_t pid;
		Pipe stdoutAndErr;
		HandshakePerform::DebugSupport *debugSupport;
		AtomicInt counter;
		FileDescriptor server;

		Core_SpawningKit_HandshakePerformTest()
			: context(schema),
			  pid(getpid()),
			  debugSupport(NULL)
		{
			wrapperRegistry.finalize();
			context.resourceLocator = resourceLocator;
			context.wrapperRegistry = &wrapperRegistry;
			context.integrationMode = "standalone";
			context.spawnDir = getSystemTempDir();

			string user = lookupSystemUsernameByUid(getuid());
			string group = lookupSystemGroupnameByGid(getgid());

			config.appGroupName = "appgroup";
			config.appRoot = "/tmp/myapp";
			config.startCommand = "echo hi";
			config.startupFile = "/tmp/myapp/app.py";
			config.appType = "wsgi";
			config.spawnMethod = "direct";
			config.bindAddress = "127.0.0.1";
			config.user = user;
			config.group = group;
			config.internStrings();
		}

		~Core_SpawningKit_HandshakePerformTest() {
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
		}

		void init(JourneyType type) {
			vector<StaticString> errors;
			ensure("Config is valid", config.validate(errors));

			context.finalize();

			session = boost::make_shared<HandshakeSession>(context, config, type);

			session->journey.setStepInProgress(SPAWNING_KIT_PREPARATION);
			HandshakePrepare(*session).execute().finalize();

			session->journey.setStepInProgress(SPAWNING_KIT_HANDSHAKE_PERFORM);
			session->journey.setStepInProgress(SUBPROCESS_BEFORE_FIRST_EXEC);
		}

		void execute() {
			HandshakePerform performer(*session, pid, FileDescriptor(), stdoutAndErr.first);
			performer.debugSupport = debugSupport;
			performer.execute();
			counter++;
		}

		static Json::Value createGoodPropertiesJson() {
			Json::Value socket, doc;
			socket["address"] = "tcp://127.0.0.1:3000";
			socket["protocol"] = "http";
			socket["concurrency"] = 1;
			socket["accept_http_requests"] = true;
			doc["sockets"].append(socket);
			return doc;
		}

		void signalFinish() {
			writeFile(session->responseDir + "/finish", "1");
		}

		void signalFinishWithError() {
			writeFile(session->responseDir + "/finish", "0");
		}
	};

	struct FreePortDebugSupport: public HandshakePerform::DebugSupport {
		Core_SpawningKit_HandshakePerformTest *test;
		HandshakeSession *session;
		AtomicInt expectedStartPort;

		virtual void beginWaitUntilSpawningFinished() {
			expectedStartPort = session->expectedStartPort;
			test->counter++;
		}
	};

	struct CrashingDebugSupport: public HandshakePerform::DebugSupport {
		virtual void beginWaitUntilSpawningFinished() {
			throw RuntimeException("oh no!");
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(Core_SpawningKit_HandshakePerformTest, 80);


	/***** General logic *****/

	TEST_METHOD(1) {
		set_test_name("If the app is generic, it finishes when the app is pingable");

		FreePortDebugSupport debugSupport;
		this->debugSupport = &debugSupport;
		config.genericApp = true;
		init(SPAWN_DIRECTLY);
		debugSupport.test = this;
		debugSupport.session = session.get();
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		EVENTUALLY(1,
			result = counter == 1;
		);

		server.assign(createTcpServer("127.0.0.1", debugSupport.expectedStartPort.get()),
			NULL, 0);

		EVENTUALLY(1,
			result = counter == 2;
		);
	}

	TEST_METHOD(2) {
		set_test_name("If findFreePort is true, it finishes when the app is pingable");

		FreePortDebugSupport debugSupport;
		this->debugSupport = &debugSupport;
		config.findFreePort = true;
		init(SPAWN_DIRECTLY);
		debugSupport.test = this;
		debugSupport.session = session.get();
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		EVENTUALLY(1,
			result = counter == 1;
		);

		server.assign(createTcpServer("127.0.0.1", debugSupport.expectedStartPort.get()),
			NULL, 0);

		EVENTUALLY(1,
			result = counter == 2;
		);
	}

	TEST_METHOD(3) {
		set_test_name("It finishes when the app has sent the finish signal");

		init(SPAWN_DIRECTLY);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		SHOULD_NEVER_HAPPEN(100,
			result = counter > 0;
		);

		createFile(session->responseDir + "/properties.json",
			createGoodPropertiesJson().toStyledString());
		signalFinish();

		EVENTUALLY(1,
			result = counter == 1;
		);
	}

	TEST_METHOD(10) {
		set_test_name("It raises an error if the process exits prematurely");

		init(SPAWN_DIRECTLY);
		pid = fork();
		if (pid == 0) {
			// Exit child
			_exit(1);
		}

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(StaticString(e.what()),
				"The application process exited prematurely.");
		}
	}

	TEST_METHOD(11) {
		set_test_name("It raises an error if the procedure took too long");

		config.startTimeoutMsec = 50;
		init(SPAWN_DIRECTLY);
		pid = fork();
		if (pid == 0) {
			// Exit child
			usleep(1000000);
			_exit(1);
		}

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(StaticString(e.what()),
				"A timeout occurred while spawning an application process.");
		}
	}

	TEST_METHOD(15) {
		set_test_name("In the event of an error, it sets the SPAWNING_KIT_HANDSHAKE_PERFORM step to the errored state");

		CrashingDebugSupport debugSupport;
		this->debugSupport = &debugSupport;
		init(SPAWN_DIRECTLY);

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &) {
			ensure_equals(session->journey.getFirstFailedStep(), SPAWNING_KIT_HANDSHAKE_PERFORM);
		}
	}

	TEST_METHOD(16) {
		set_test_name("In the event of an error, the exception contains journey state information from the response directory");

		CrashingDebugSupport debugSupport;
		this->debugSupport = &debugSupport;
		init(SPAWN_DIRECTLY);

		createFile(session->responseDir + "/steps/subprocess_listen/state", "STEP_ERRORED");

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &) {
			ensure_equals(session->journey.getStepInfo(SUBPROCESS_LISTEN).state,
				STEP_ERRORED);
		}
	}

	TEST_METHOD(17) {
		set_test_name("In the event of an error, the exception contains subprocess stdout and stderr data");

		Pipe p = createPipe(__FILE__, __LINE__);
		CrashingDebugSupport debugSupport;
		init(SPAWN_DIRECTLY);
		HandshakePerform performer(*session, pid, FileDescriptor(), p.first);
		performer.debugSupport = &debugSupport;

		Json::Value config;
		vector<ConfigKit::Error> errors;
		LoggingKit::ConfigChangeRequest req;
		config["app_output_log_level"] = "debug";
		if (LoggingKit::context->prepareConfigChange(config, errors, req)) {
			LoggingKit::context->commitConfigChange(req);
		} else {
			P_BUG("Error configuring LoggingKit: " << ConfigKit::toString(errors));
		}

		writeExact(p.second, "hi\n");

		try {
			performer.execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getStdoutAndErrData(), "hi\n");
		}
	}

	TEST_METHOD(18) {
		set_test_name("In the event of an error caused by the subprocess, the exception contains messages from"
			" the subprocess as dumped in the response directory");

		init(SPAWN_DIRECTLY);
		pid = fork();
		if (pid == 0) {
			// Exit child
			_exit(1);
		}

		createFile(session->responseDir + "/error/summary", "the summary");
		createFile(session->responseDir + "/error/problem_description.txt", "the <problem>");
		createFile(session->responseDir + "/error/advanced_problem_details", "the advanced problem details");
		createFile(session->responseDir + "/error/solution_description.html", "the <b>solution</b>");

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getSummary(), "the summary");
			ensure_equals(e.getProblemDescriptionHTML(), "the &lt;problem&gt;");
			ensure_equals(e.getAdvancedProblemDetails(), "the advanced problem details");
			ensure_equals(e.getSolutionDescriptionHTML(), "the <b>solution</b>");
		}
	}

	TEST_METHOD(19) {
		set_test_name("In the event of success, it loads the journey state information from the response directory");

		init(SPAWN_DIRECTLY);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		createFile(session->responseDir + "/properties.json",
			createGoodPropertiesJson().toStyledString());
		createFile(session->responseDir + "/steps/subprocess_listen/state",
			"STEP_PERFORMED");
		signalFinish();

		EVENTUALLY(5,
			result = counter == 1;
		);

		ensure_equals(session->journey.getStepInfo(SUBPROCESS_LISTEN).state, STEP_PERFORMED);
	}

	TEST_METHOD(20) {
		// Limited test of whether the code mitigates symlink attacks.
		set_test_name("It does not read from symlinks");

		init(SPAWN_DIRECTLY);

		createFile(session->responseDir + "/properties-real.json",
			createGoodPropertiesJson().toStyledString());
		symlink("properties-real.json",
			(session->responseDir + "/properties.json").c_str());
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish,
			this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSummary(),
				"Cannot open 'properties.json'"));
		}
	}


	/***** Success response handling *****/

	TEST_METHOD(30) {
		set_test_name("The result object contains basic information such as FDs and time");

		init(SPAWN_DIRECTLY);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		createFile(session->responseDir + "/properties.json",
			createGoodPropertiesJson().toStyledString());
		createFile(session->responseDir + "/steps/subprocess_listen/state",
			"STEP_PERFORMED");
		signalFinish();

		EVENTUALLY(5,
			result = counter == 1;
		);

		ensure_equals(session->result.pid, pid);
		ensure(session->result.spawnStartTime != 0);
		ensure(session->result.spawnEndTime >= session->result.spawnStartTime);
		ensure(session->result.spawnStartTimeMonotonic != 0);
		ensure(session->result.spawnEndTimeMonotonic >= session->result.spawnStartTimeMonotonic);
	}

	TEST_METHOD(31) {
		set_test_name("The result object contains sockets specified in properties.json");

		init(SPAWN_DIRECTLY);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		createFile(session->responseDir + "/properties.json",
			createGoodPropertiesJson().toStyledString());
		createFile(session->responseDir + "/steps/subprocess_listen/state",
			"STEP_PERFORMED");
		signalFinish();

		EVENTUALLY(5,
			result = counter == 1;
		);

		ensure_equals(session->result.sockets.size(), 1u);
		ensure_equals(session->result.sockets[0].address, "tcp://127.0.0.1:3000");
		ensure_equals(session->result.sockets[0].protocol, "http");
		ensure_equals(session->result.sockets[0].concurrency, 1);
		ensure(session->result.sockets[0].acceptHttpRequests);
	}

	TEST_METHOD(32) {
		set_test_name("If the app is generic, it automatically registers the free port as a request-handling socket");

		FreePortDebugSupport debugSupport;
		this->debugSupport = &debugSupport;
		config.genericApp = true;
		init(SPAWN_DIRECTLY);
		debugSupport.test = this;
		debugSupport.session = session.get();
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		EVENTUALLY(1,
			result = counter == 1;
		);
		server.assign(createTcpServer("127.0.0.1", debugSupport.expectedStartPort.get()),
			NULL, 0);
		EVENTUALLY(1,
			result = counter == 2;
		);

		ensure_equals(session->result.sockets.size(), 1u);
		ensure_equals(session->result.sockets[0].address, "tcp://127.0.0.1:" + toString(session->expectedStartPort));
		ensure_equals(session->result.sockets[0].protocol, "http");
		ensure_equals(session->result.sockets[0].concurrency, -1);
		ensure(session->result.sockets[0].acceptHttpRequests);
	}

	TEST_METHOD(33) {
		set_test_name("If findFreePort is true, it automatically registers the free port as a request-handling socket");

		FreePortDebugSupport debugSupport;
		this->debugSupport = &debugSupport;
		config.findFreePort = true;
		init(SPAWN_DIRECTLY);
		debugSupport.test = this;
		debugSupport.session = session.get();
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::execute, this));

		EVENTUALLY(1,
			result = counter == 1;
		);
		server.assign(createTcpServer("127.0.0.1", debugSupport.expectedStartPort.get()),
			NULL, 0);
		EVENTUALLY(1,
			result = counter == 2;
		);

		ensure_equals(session->result.sockets.size(), 1u);
		ensure_equals(session->result.sockets[0].address, "tcp://127.0.0.1:" + toString(session->expectedStartPort));
		ensure_equals(session->result.sockets[0].protocol, "http");
		ensure_equals(session->result.sockets[0].concurrency, -1);
		ensure(session->result.sockets[0].acceptHttpRequests);
	}

	TEST_METHOD(34) {
		set_test_name("It raises an error if we expected the subprocess to create a properties.json,"
			" but the file does not conform to the required format");

		init(SPAWN_DIRECTLY);
		createFile(session->responseDir + "/properties.json", "{ \"sockets\": {} }");
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish, this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSummary(), "'sockets' must be an array"));
		}
	}

	TEST_METHOD(35) {
		set_test_name("It raises an error if we expected the subprocess to specify at"
			" least one request-handling socket in properties.json, yet the file does"
			" not specify any");

		Json::Value socket, doc;
		socket["address"] = "tcp://127.0.0.1:3000";
		socket["protocol"] = "http";
		socket["concurrency"] = 1;
		doc["sockets"].append(socket);

		init(SPAWN_DIRECTLY);
		createFile(session->responseDir + "/properties.json", doc.toStyledString());
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish, this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSummary(),
				"the application did not report any sockets to receive requests on"));
		}
	}

	TEST_METHOD(36) {
		set_test_name("It raises an error if we expected the subprocess to specify at"
			" least one request-handling socket in properties.json, yet properties.json"
			" does not exist");

		init(SPAWN_DIRECTLY);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish, this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSummary(), "sockets are not supplied"));
		}
	}

	TEST_METHOD(37) {
		set_test_name("It raises an error if we expected the subprocess to specify at"
			" least one preloader command socket in properties.json, yet the file does"
			" not specify any");

		Json::Value socket, doc;
		socket["address"] = "tcp://127.0.0.1:3000";
		socket["protocol"] = "http";
		socket["concurrency"] = 1;
		doc["sockets"].append(socket);

		init(START_PRELOADER);
		createFile(session->responseDir + "/properties.json", doc.toStyledString());
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish, this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSummary(),
				"the application did not report any sockets to receive preloader commands on"));
		}
	}

	TEST_METHOD(38) {
		set_test_name("It raises an error if we expected the subprocess to specify at"
			" least one preloader command socket in properties.json, yet properties.json"
			" does not exist");

		init(START_PRELOADER);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish, this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSummary(),
				"sockets are not supplied"));
		}
	}

	TEST_METHOD(39) {
		set_test_name("It raises an error if properties.json specifies a Unix domain socket"
			" that is not located in the apps.s subdir of the instance directory");

		TempDir tmpDir("tmp.instance");

		context.instanceDir = absolutizePath("tmp.instance");
		init(SPAWN_DIRECTLY);
		Json::Value doc = createGoodPropertiesJson();
		doc["sockets"][0]["address"] = "unix:/foo";
		createFile(session->responseDir + "/properties.json", doc.toStyledString());
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish, this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure(containsSubstring(e.getSummary(),
				"must be an absolute path to a file in"));
		}
	}

	TEST_METHOD(40) {
		set_test_name("It raises an error if properties.json specifies a Unix domain socket"
			" that is not owned by the app");

		if (geteuid() != 0) {
			return;
		}

		TempDir tmpDir("tmp.instance");
		mkdir("tmp.instance/apps.s", 0700);
		string socketPath = absolutizePath("tmp.instance/apps.s/foo.sock");

		init(SPAWN_DIRECTLY);
		Json::Value doc = createGoodPropertiesJson();
		doc["sockets"][0]["address"] = "unix:" + socketPath;
		createFile(session->responseDir + "/properties.json", doc.toStyledString());
		safelyClose(createUnixServer(socketPath));
		chown(socketPath.c_str(), 1, 1);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinish, this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure("(1)", containsSubstring(e.getSummary(), "must be owned by user"));
		}
	}


	/***** Error response handling *****/

	TEST_METHOD(50) {
		set_test_name("It raises an error if the application responded with an error");

		init(SPAWN_DIRECTLY);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinishWithError,
			this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getSummary(), "The web application aborted with an error during startup.");
		}
	}

	TEST_METHOD(51) {
		set_test_name("The exception contains error messages provided by the application");

		init(SPAWN_DIRECTLY);
		writeFile(session->workDir->getPath() + "/response/error/summary",
			"the summary");
		writeFile(session->workDir->getPath() + "/response/error/problem_description.html",
			"the problem description");
		writeFile(session->workDir->getPath() + "/response/error/solution_description.html",
			"the solution description");
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinishWithError,
			this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getSummary(), "the summary");
			ensure_equals(e.getProblemDescriptionHTML(), "the problem description");
			ensure_equals(e.getSolutionDescriptionHTML(), "the solution description");
		}
	}

	TEST_METHOD(52) {
		set_test_name("The exception describes which steps in the journey had failed");

		init(SPAWN_DIRECTLY);
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinishWithError,
			this));

		try {
			execute();
		} catch (const SpawnException &e) {
			ensure_equals(e.getJourney().getFirstFailedStep(), SUBPROCESS_BEFORE_FIRST_EXEC);
		}
	}

	TEST_METHOD(53) {
		set_test_name("The exception contains the subprocess' output");

		Json::Value config;
		vector<ConfigKit::Error> errors;
		LoggingKit::ConfigChangeRequest req;
		config["app_output_log_level"] = "debug";
		if (LoggingKit::context->prepareConfigChange(config, errors, req)) {
			LoggingKit::context->commitConfigChange(req);
		} else {
			P_BUG("Error configuring LoggingKit: " << ConfigKit::toString(errors));
		}

		init(SPAWN_DIRECTLY);
		stdoutAndErr = createPipe(__FILE__, __LINE__);
		writeExact(stdoutAndErr.second, "oh no");
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinishWithError,
			this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getStdoutAndErrData(), "oh no");
		}
	}

	TEST_METHOD(54) {
		set_test_name("The exception contains the subprocess' environment variables dump");

		init(SPAWN_DIRECTLY);
		writeFile(session->workDir->getPath() + "/envdump/envvars",
			"the env dump");
		TempThread thr(boost::bind(&Core_SpawningKit_HandshakePerformTest::signalFinishWithError,
			this));

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals(e.getSubprocessEnvvars(), "the env dump");
		}
	}

	TEST_METHOD(55) {
		set_test_name("If the subprocess fails without setting a specific"
			" journey step to the ERRORED state,"
			" and there is a subprocess journey step in the IN_PROGRESS state,"
			" then we set that latter step to the ERRORED state");

		init(SPAWN_DIRECTLY);
		pid = fork();
		if (pid == 0) {
			// Exit child
			_exit(1);
		}

		createFile(session->responseDir
			+ "/steps/subprocess_before_first_exec/state",
			"STEP_PERFORMED");
		createFile(session->responseDir
			+ "/steps/subprocess_before_first_exec/duration",
			"1");
		createFile(session->responseDir
			+ "/steps/subprocess_spawn_env_setupper_before_shell/state",
			"STEP_IN_PROGRESS");

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals("SPAWNING_KIT_HANDSHAKE_PERFORM is in the IN_PROGRESS state",
				e.getJourney().getStepInfo(SPAWNING_KIT_HANDSHAKE_PERFORM).state,
				STEP_IN_PROGRESS);

			ensure_equals("SUBPROCESS_BEFORE_FIRST_EXEC is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_BEFORE_FIRST_EXEC).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL is in the ERRORED state",
				e.getJourney().getStepInfo(SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL).state,
				STEP_ERRORED);
			ensure_equals("SUBPROCESS_OS_SHELL is in the NOT_STARTED state",
				e.getJourney().getStepInfo(SUBPROCESS_OS_SHELL).state,
				STEP_NOT_STARTED);
			ensure_equals("SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL is in the NOT_STARTED state",
				e.getJourney().getStepInfo(SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL).state,
				STEP_NOT_STARTED);
		}
	}

	TEST_METHOD(56) {
		set_test_name("If the subprocess fails without setting a specific"
			" journey step to the ERRORED state,"
			" and there is no subprocess journey step in the IN_PROGRESS state,"
			" and no subprocess journey steps are in the PERFORMED state,"
			" then we set the first subprocess journey step to the ERRORED state");

		init(SPAWN_DIRECTLY);
		pid = fork();
		if (pid == 0) {
			// Exit child
			_exit(1);
		}

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals("SPAWNING_KIT_HANDSHAKE_PERFORM is in the IN_PROGRESS state",
				e.getJourney().getStepInfo(SPAWNING_KIT_HANDSHAKE_PERFORM).state,
				STEP_IN_PROGRESS);

			ensure_equals("SUBPROCESS_BEFORE_FIRST_EXEC is in the ERRORED state",
				e.getJourney().getStepInfo(SUBPROCESS_BEFORE_FIRST_EXEC).state,
				STEP_ERRORED);
			ensure_equals("SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL is in the NOT_STARTED state",
				e.getJourney().getStepInfo(SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL).state,
				STEP_NOT_STARTED);
		}
	}

	TEST_METHOD(57) {
		set_test_name("If the subprocess fails without setting a specific"
			" journey step to the ERRORED state,"
			" and there is no subprocess journey step in the IN_PROGRESS state,"
			" and some but not all subprocess journey steps are in the PERFORMED state,"
			" then we set the step that comes right after the last PERFORMED step,"
			" to the ERRORED state");

		init(SPAWN_DIRECTLY);
		pid = fork();
		if (pid == 0) {
			// Exit child
			_exit(1);
		}

		createFile(session->responseDir
			+ "/steps/subprocess_before_first_exec/state",
			"STEP_PERFORMED");
		createFile(session->responseDir
			+ "/steps/subprocess_before_first_exec/duration",
			"1");

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals("SPAWNING_KIT_HANDSHAKE_PERFORM is in the IN_PROGRESS state",
				e.getJourney().getStepInfo(SPAWNING_KIT_HANDSHAKE_PERFORM).state,
				STEP_IN_PROGRESS);

			ensure_equals("SUBPROCESS_BEFORE_FIRST_EXEC is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_BEFORE_FIRST_EXEC).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL is in the ERRORED state",
				e.getJourney().getStepInfo(SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL).state,
				STEP_ERRORED);
			ensure_equals("SUBPROCESS_OS_SHELL is in the NOT_STARTED state",
				e.getJourney().getStepInfo(SUBPROCESS_OS_SHELL).state,
				STEP_NOT_STARTED);
			ensure_equals("SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL is in the NOT_STARTED state",
				e.getJourney().getStepInfo(SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL).state,
				STEP_NOT_STARTED);
		}
	}

	TEST_METHOD(58) {
		set_test_name("If the subprocess fails without setting a specific"
			" journey step to the ERRORED state,"
			" and there is no subprocess journey step in the IN_PROGRESS state,"
			" and all subprocess journey steps are in the PERFORMED state,"
			" then we set the last subprocess step to the ERRORED state");

		init(SPAWN_DIRECTLY);
		pid = fork();
		if (pid == 0) {
			// Exit child
			_exit(1);
		}

		JourneyStep firstStep = getFirstSubprocessJourneyStep();
		JourneyStep lastStep = getLastSubprocessJourneyStep();
		JourneyStep step;

		for (step = firstStep; step < lastStep; step = JourneyStep((int) step + 1)) {
			if (!session->journey.hasStep(step)) {
				continue;
			}

			createFile(session->responseDir
				+ "/steps/" + journeyStepToStringLowerCase(step) + "/state",
				"STEP_PERFORMED");
			createFile(session->responseDir
				+ "/steps/" + journeyStepToStringLowerCase(step) + "/duration",
				"1");
		}

		try {
			execute();
			fail("SpawnException expected");
		} catch (const SpawnException &e) {
			ensure_equals("SPAWNING_KIT_HANDSHAKE_PERFORM is in the IN_PROGRESS state",
				e.getJourney().getStepInfo(SPAWNING_KIT_HANDSHAKE_PERFORM).state,
				STEP_IN_PROGRESS);

			ensure_equals("SUBPROCESS_BEFORE_FIRST_EXEC is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_BEFORE_FIRST_EXEC).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_OS_SHELL is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_OS_SHELL).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_APP_LOAD_OR_EXEC is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_APP_LOAD_OR_EXEC).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_APP_LOAD_OR_EXEC is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_APP_LOAD_OR_EXEC).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_LISTEN is in the PERFORMED state",
				e.getJourney().getStepInfo(SUBPROCESS_LISTEN).state,
				STEP_PERFORMED);
			ensure_equals("SUBPROCESS_FINISH is in the ERRORED state",
				e.getJourney().getStepInfo(SUBPROCESS_FINISH).state,
				STEP_ERRORED);
		}
	}
}
