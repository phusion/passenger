#include <TestSupport.h>
#include <Core/ApplicationPool/Process.h>
#include <LoggingKit/Context.h>
#include <FileTools/FileManip.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool2;
using namespace std;

namespace tut {
	struct Core_ApplicationPool_ProcessTest: public TestBase {
		WrapperRegistry::Registry wrapperRegistry;
		SpawningKit::Context::Schema skContextSchema;
		SpawningKit::Context skContext;
		Context context;
		BasicGroupInfo groupInfo;
		vector<SpawningKit::Result::Socket> sockets;
		Pipe stdinFd, stdoutAndErrFd;
		FileDescriptor server1, server2, server3;

		Core_ApplicationPool_ProcessTest()
			: skContext(skContextSchema)
		{
			wrapperRegistry.finalize();
			skContext.resourceLocator = resourceLocator;
			skContext.wrapperRegistry = &wrapperRegistry;
			skContext.integrationMode = "standalone";
			skContext.finalize();

			context.spawningKitFactory = boost::make_shared<SpawningKit::Factory>(&skContext);
			context.finalize();

			groupInfo.context = &context;
			groupInfo.group = NULL;
			groupInfo.name = "test";

			struct sockaddr_in addr;
			socklen_t len = sizeof(addr);
			SpawningKit::Result::Socket socket;

			server1.assign(createTcpServer("127.0.0.1", 0, 0, __FILE__, __LINE__), NULL, 0);
			getsockname(server1, (struct sockaddr *) &addr, &len);
			socket.address = "tcp://127.0.0.1:" + toString(addr.sin_port);
			socket.protocol = "session";
			socket.concurrency = 3;
			socket.acceptHttpRequests = true;
			sockets.push_back(socket);

			server2.assign(createTcpServer("127.0.0.1", 0, 0, __FILE__, __LINE__), NULL, 0);
			getsockname(server2, (struct sockaddr *) &addr, &len);
			socket = SpawningKit::Result::Socket();
			socket.address = "tcp://127.0.0.1:" + toString(addr.sin_port);
			socket.protocol = "session";
			socket.concurrency = 3;
			socket.acceptHttpRequests = true;
			sockets.push_back(socket);

			server3.assign(createTcpServer("127.0.0.1", 0, 0, __FILE__, __LINE__), NULL, 0);
			getsockname(server3, (struct sockaddr *) &addr, &len);
			socket = SpawningKit::Result::Socket();
			socket.address = "tcp://127.0.0.1:" + toString(addr.sin_port);
			socket.protocol = "session";
			socket.concurrency = 3;
			socket.acceptHttpRequests = true;
			sockets.push_back(socket);

			stdinFd = createPipe(__FILE__, __LINE__);
			stdoutAndErrFd = createPipe(__FILE__, __LINE__);

			Json::Value config;
			vector<ConfigKit::Error> errors;
			LoggingKit::ConfigChangeRequest req;
			config["app_output_log_level"] = "debug";

			if (LoggingKit::context->prepareConfigChange(config, errors, req)) {
				LoggingKit::context->commitConfigChange(req);
			} else {
				P_BUG("Error configuring LoggingKit: " << ConfigKit::toString(errors));
			}
		}

		~Core_ApplicationPool_ProcessTest() {
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

		ProcessPtr createProcess(const Json::Value &extraArgs = Json::Value()) {
			SpawningKit::Result result;
			Json::Value args = extraArgs;
			vector<StaticString> internalFieldErrors;
			vector<StaticString> appSuppliedFieldErrors;

			result.pid = 123;
			result.gupid = "123";
			result.type = SpawningKit::Result::DUMMY;
			result.spawnStartTime = 1;
			result.spawnEndTime = 1;
			result.spawnStartTimeMonotonic = 1;
			result.spawnEndTimeMonotonic = 1;
			result.sockets = sockets;
			result.stdinFd = stdinFd[1];
			result.stdoutAndErrFd = stdoutAndErrFd[0];

			if (!result.validate(internalFieldErrors, appSuppliedFieldErrors)) {
				P_BUG("Cannot create dummy process:\n"
					<< toString(internalFieldErrors)
					<< "\n" << toString(appSuppliedFieldErrors));
			}

			args["spawner_creation_time"] = 0;

			Process *p = context.processObjectPool.malloc();
			p = new (p) Process(&groupInfo, 0, result, args);
			ProcessPtr process(p, false);

			process->shutdownNotRequired();
			return process;
		}
	};

	DEFINE_TEST_GROUP(Core_ApplicationPool_ProcessTest);

	TEST_METHOD(1) {
		set_test_name("Test initial state");
		ProcessPtr process = createProcess();
		ensure_equals(process->busyness(), 0);
		ensure(!process->isTotallyBusy());
	}

	TEST_METHOD(2) {
		set_test_name("Test opening and closing sessions");
		ProcessPtr process = createProcess();
		SessionPtr session = process->newSession();
		SessionPtr session2 = process->newSession();
		ensure_equals(process->sessions, 2);
		process->sessionClosed(session.get());
		ensure_equals(process->sessions, 1);
		process->sessionClosed(session2.get());
		ensure_equals(process->sessions, 0);
	}

	TEST_METHOD(3) {
		set_test_name("newSession() checks out the socket with the smallest busyness number "
			"and sessionClosed() restores the session busyness statistics");
		ProcessPtr process = createProcess();

		// The first 3 newSession() commands check out an idle socket.
		SessionPtr session1 = process->newSession();
		SessionPtr session2 = process->newSession();
		SessionPtr session3 = process->newSession();
		ensure(session1->getSocket()->address != session2->getSocket()->address);
		ensure(session1->getSocket()->address != session3->getSocket()->address);
		ensure(session2->getSocket()->address != session3->getSocket()->address);

		// The next 2 newSession() commands check out sockets with sessions == 1.
		SessionPtr session4 = process->newSession();
		SessionPtr session5 = process->newSession();
		ensure(session4->getSocket()->address != session5->getSocket()->address);

		// There should now be 1 process with 1 session
		// and 2 processes with 2 sessions.
		map<int, int> sessionCount;
		SocketList::const_iterator it;
		for (it = process->getSockets().begin(); it != process->getSockets().end(); it++) {
			sessionCount[it->sessions]++;
		}
		ensure_equals(sessionCount.size(), 2u);
		ensure_equals(sessionCount[1], 1);
		ensure_equals(sessionCount[2], 2);

		// Closing the first 3 sessions will result in no processes having 1 session
		// and 1 process having 2 sessions.
		process->sessionClosed(session1.get());
		process->sessionClosed(session2.get());
		process->sessionClosed(session3.get());
		sessionCount.clear();
		for (it = process->getSockets().begin(); it != process->getSockets().end(); it++) {
			sessionCount[it->sessions]++;
		}
		ensure_equals(sessionCount[0], 1);
		ensure_equals(sessionCount[1], 2);
	}

	TEST_METHOD(4) {
		set_test_name("If all sockets are at their full capacity then newSession() will fail");
		ProcessPtr process = createProcess();
		vector<SessionPtr> sessions;
		for (int i = 0; i < 9; i++) {
			ensure(!process->isTotallyBusy());
			SessionPtr session = process->newSession();
			ensure(session != NULL);
			sessions.push_back(session);
		}
		ensure(process->isTotallyBusy());
		ensure(process->newSession() == NULL);
	}

	TEST_METHOD(5) {
		set_test_name("It forwards all stdout and stderr output, even after the "
			"Process object has been destroyed");

		TempDir temp("tmp.log");
		Json::Value extraArgs;
		extraArgs["log_file"] = "tmp.log/file";
		fclose(fopen("tmp.log/file", "w"));

		ProcessPtr process = createProcess(extraArgs);
		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::WARN);
		}

		writeExact(stdoutAndErrFd[1], "stdout and err 1\n");
		writeExact(stdoutAndErrFd[1], "stdout and err 2\n");

		EVENTUALLY(2,
			string contents = unsafeReadFile("tmp.log/file");
			result = contents.find("stdout and err 1\n") != string::npos
				&& contents.find("stdout and err 2\n") != string::npos;
		);

		fclose(fopen("tmp.log/file", "w"));
		process.reset();

		writeExact(stdoutAndErrFd[1], "stdout and err 3\n");
		writeExact(stdoutAndErrFd[1], "stdout and err 4\n");
		EVENTUALLY(2,
			string contents = unsafeReadFile("tmp.log/file");
			result = contents.find("stdout and err 3\n") != string::npos
				&& contents.find("stdout and err 4\n") != string::npos;
		);
	}
}
