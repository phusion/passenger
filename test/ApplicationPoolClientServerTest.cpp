#include "tut.h"
#include "ApplicationPoolClientServer.h"
#include "Utils.cpp"
#include <cstring>
#include <unistd.h>

using namespace Passenger;

namespace tut {
	struct ApplicationPoolClientServerTest {
		ApplicationPoolServerPtr server;
		
		ApplicationPoolClientServerTest() {
			server = ptr(new ApplicationPoolServer("support/spawn_server_mock.rb"));
		}
	};

	DEFINE_TEST_GROUP(ApplicationPoolClientServerTest);

	TEST_METHOD(1) {
		// Constructor and destructor should not crash.
	}
	
	TEST_METHOD(2) {
		// Connecting to the ApplicationPoolServer, as well as destroying the
		// returned ApplicationPool object, should not crash.
		server->connect();
	}
	
	TEST_METHOD(3) {
		// If connect() has been called, then detach() should not crash, and the
		// ApplicationPoolServer's destructor should not crash either.
		pid_t pid = fork();
		if (pid == 0) {
			server->connect();
			server->detach();
			server = ApplicationPoolServerPtr();
			_exit(0);
		} else {
			waitpid(pid, NULL, 0);
		}
	}
	
	TEST_METHOD(4) {
		// If connect() has not been called, then detach() should not crash, and the
		// ApplicationPoolServer's destructor should not crash either.
		pid_t pid = fork();
		if (pid == 0) {
			server->detach();
			server = ApplicationPoolServerPtr();
			_exit(0);
		} else {
			waitpid(pid, NULL, 0);
		}
	}
}
