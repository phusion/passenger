#include "tut.h"
#include "ApplicationPoolServer.h"
#include "Utils.h"
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;

namespace tut {
	struct ApplicationPoolServerTest {
		ApplicationPoolServerPtr server;
		ApplicationPoolPtr pool, pool2;
		
		ApplicationPoolServerTest() {
			server = ptr(new ApplicationPoolServer(
				"./ApplicationPoolServerExecutable",
				"stub/spawn_server.rb"));
		}
	};

	DEFINE_TEST_GROUP(ApplicationPoolServerTest);

	TEST_METHOD(1) {
		// Constructor and destructor should not crash or block indefinitely.
		// (And yes, this test method is intended to be blank.)
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
			server.reset();
			_exit(0);
		} else {
			int status;
			
			waitpid(pid, &status, 0);
			if (status != 0) {
				fail("Child process exited abnormally.");
			}
		}
	}

	TEST_METHOD(4) {
		// If connect() has not been called, then detach() should not crash, and the
		// ApplicationPoolServer's destructor should not crash either.
		pid_t pid = fork();
		if (pid == 0) {
			server->detach();
			server.reset();
			_exit(0);
		} else {
			int status;
			
			waitpid(pid, &status, 0);
			if (status != 0) {
				fail("Child process exited abnormally.");
			}
		}
	}
}

