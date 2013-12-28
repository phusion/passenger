#include <TestSupport.h>
#include <ApplicationPool2/Process.h>
#include <Utils/IOUtils.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool2;
using namespace std;

namespace tut {
	struct ApplicationPool2_ProcessTest {
		BackgroundEventLoop bg;
		SocketListPtr sockets;
		SocketPair adminSocket;
		Pipe errorPipe;
		FileDescriptor server1, server2, server3;
		
		ApplicationPool2_ProcessTest() {
			bg.start();
			
			struct sockaddr_in addr;
			socklen_t len = sizeof(addr);
			sockets = boost::make_shared<SocketList>();
			
			server1 = createTcpServer("127.0.0.1", 0);
			getsockname(server1, (struct sockaddr *) &addr, &len);
			sockets->add("main1",
				"tcp://127.0.0.1:" + toString(addr.sin_port),
				"session", 3);
			
			server2 = createTcpServer("127.0.0.1", 0);
			getsockname(server2, (struct sockaddr *) &addr, &len);
			sockets->add("main2",
				"tcp://127.0.0.1:" + toString(addr.sin_port),
				"session", 3);
			
			server3 = createTcpServer("127.0.0.1", 0);
			getsockname(server3, (struct sockaddr *) &addr, &len);
			sockets->add("main3",
				"tcp://127.0.0.1:" + toString(addr.sin_port),
				"session", 3);
			
			adminSocket = createUnixSocketPair();
			errorPipe = createPipe();
		}
	};
	
	DEFINE_TEST_GROUP(ApplicationPool2_ProcessTest);
	
	TEST_METHOD(1) {
		// Test initial state.
		ProcessPtr process = boost::make_shared<Process>(bg.safe,
			123, "", "", adminSocket[0],
			errorPipe[0], sockets, 0, 0);
		process->dummy = true;
		process->requiresShutdown = false;
		ensure_equals(process->busyness(), 0);
		ensure(!process->isTotallyBusy());
	}
	
	TEST_METHOD(2) {
		// Test opening and closing sessions.
		ProcessPtr process = boost::make_shared<Process>(bg.safe,
			123, "", "", adminSocket[0],
			errorPipe[0], sockets, 0, 0);
		process->dummy = true;
		process->requiresShutdown = false;
		SessionPtr session = process->newSession();
		SessionPtr session2 = process->newSession();
		ensure_equals(process->sessions, 2);
		process->sessionClosed(session.get());
		ensure_equals(process->sessions, 1);
		process->sessionClosed(session2.get());
		ensure_equals(process->sessions, 0);
	}
	
	TEST_METHOD(3) {
		// newSession() checks out the socket with the smallest busyness number
		// and sessionClosed() restores the session busyness statistics.
		ProcessPtr process = boost::make_shared<Process>(bg.safe,
			123, "", "", adminSocket[0],
			errorPipe[0], sockets, 0, 0);
		process->dummy = true;
		process->requiresShutdown = false;
		
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
		for (it = process->sockets->begin(); it != process->sockets->end(); it++) {
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
		for (it = process->sockets->begin(); it != process->sockets->end(); it++) {
			sessionCount[it->sessions]++;
		}
		ensure_equals(sessionCount[0], 1);
		ensure_equals(sessionCount[1], 2);
	}
	
	TEST_METHOD(4) {
		// If all sockets are at their full capacity then newSession() will fail.
		ProcessPtr process = boost::make_shared<Process>(bg.safe,
			123, "", "", adminSocket[0],
			errorPipe[0], sockets, 0, 0);
		process->dummy = true;
		process->requiresShutdown = false;
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
}
