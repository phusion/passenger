#include <TestSupport.h>
#include <ApplicationPool2/Process.h>
#include <Utils/IOUtils.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool2;
using namespace std;

namespace tut {
	struct ApplicationPool2_ProcessTest {
		Context context;
		BasicGroupInfo groupInfo;
		Json::Value sockets;
		SocketPair adminSocket;
		Pipe errorPipe;
		FileDescriptor server1, server2, server3;
		SpawningKit::OutputHandler gatherOutput;
		string gatheredOutput;
		boost::mutex gatheredOutputSyncher;

		ApplicationPool2_ProcessTest() {
			setPrintAppOutputAsDebuggingMessages(true);

			SpawningKit::ConfigPtr spawningKitConfig = boost::make_shared<SpawningKit::Config>();
			spawningKitConfig->resourceLocator = resourceLocator;
			spawningKitConfig->finalize();

			context.setSpawningKitFactory(boost::make_shared<SpawningKit::Factory>(spawningKitConfig));
			context.finalize();

			groupInfo.context = &context;
			groupInfo.group = NULL;
			groupInfo.name = "test";

			struct sockaddr_in addr;
			socklen_t len = sizeof(addr);
			Json::Value socket;

			server1 = createTcpServer("127.0.0.1", 0);
			getsockname(server1, (struct sockaddr *) &addr, &len);
			socket["name"] = "main1";
			socket["address"] = "tcp://127.0.0.1:" + toString(addr.sin_port);
			socket["protocol"] = "session";
			socket["concurrency"] = 3;
			sockets.append(socket);

			server2 = createTcpServer("127.0.0.1", 0);
			getsockname(server2, (struct sockaddr *) &addr, &len);
			socket = Json::Value();
			socket["name"] = "main2";
			socket["address"] = "tcp://127.0.0.1:" + toString(addr.sin_port);
			socket["protocol"] = "session";
			socket["concurrency"] = 3;
			sockets.append(socket);

			server3 = createTcpServer("127.0.0.1", 0);
			getsockname(server3, (struct sockaddr *) &addr, &len);
			socket = Json::Value();
			socket["name"] = "main3";
			socket["address"] = "tcp://127.0.0.1:" + toString(addr.sin_port);
			socket["protocol"] = "session";
			socket["concurrency"] = 3;
			sockets.append(socket);

			adminSocket = createUnixSocketPair();
			errorPipe = createPipe();

			gatherOutput = boost::bind(&ApplicationPool2_ProcessTest::_gatherOutput, this, _1, _2);
		}

		~ApplicationPool2_ProcessTest() {
			setLogLevel(DEFAULT_LOG_LEVEL);
			setPrintAppOutputAsDebuggingMessages(false);
		}

		void _gatherOutput(const char *data, unsigned int size) {
			boost::lock_guard<boost::mutex> l(gatheredOutputSyncher);
			gatheredOutput.append(data, size);
		}

		ProcessPtr createProcess() {
			SpawningKit::Result result;

			result["type"] = "dummy";
			result["pid"] = 123;
			result["gupid"] = "123";
			result["sockets"] = sockets;
			result["spawner_creation_time"] = 0;
			result["spawn_start_time"] = 0;
			result.adminSocket = adminSocket[0];
			result.errorPipe = errorPipe[0];

			ProcessPtr process(context.getProcessObjectPool().construct(
				&groupInfo, result), false);
			process->shutdownNotRequired();
			return process;
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool2_ProcessTest);

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
		ensure(session1->getSocket()->name != session2->getSocket()->name);
		ensure(session1->getSocket()->name != session3->getSocket()->name);
		ensure(session2->getSocket()->name != session3->getSocket()->name);

		// The next 2 newSession() commands check out sockets with sessions == 1.
		SessionPtr session4 = process->newSession();
		SessionPtr session5 = process->newSession();
		ensure(session4->getSocket()->name != session5->getSocket()->name);

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
		set_test_name("It forwards all adminSocket and errorPipe output, even after the "
			"Process object has been destroyed");
		ProcessPtr process = createProcess();
		setLogLevel(LVL_WARN);
		context.getSpawningKitConfig()->outputHandler = gatherOutput;

		writeExact(adminSocket[1], "adminSocket 1\n");
		writeExact(errorPipe[1], "errorPipe 1\n");

		EVENTUALLY(2,
			boost::lock_guard<boost::mutex> l(gatheredOutputSyncher);
			result = gatheredOutput.find("adminSocket 1\n") != string::npos
				&& gatheredOutput.find("errorPipe 1\n") != string::npos;
		);

		{
			boost::lock_guard<boost::mutex> l(gatheredOutputSyncher);
			gatheredOutput.clear();
		}
		process.reset();

		writeExact(adminSocket[1], "adminSocket 2\n");
		writeExact(errorPipe[1], "errorPipe 2\n");
		EVENTUALLY(2,
			boost::lock_guard<boost::mutex> l(gatheredOutputSyncher);
			result = gatheredOutput.find("adminSocket 2\n") != string::npos
				&& gatheredOutput.find("errorPipe 2\n") != string::npos;
		);
	}
}
