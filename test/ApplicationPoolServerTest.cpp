#include "tut.h"
#include "ApplicationPoolServer.h"
#include "Utils.h"
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;

namespace tut {
	static bool firstRun = true;
	static unsigned int initialFileDescriptors;
	
	static unsigned int countOpenFileDescriptors() {
		int ret;
		unsigned int result = 0;
		for (long i = sysconf(_SC_OPEN_MAX) - 1; i >= 0; i--) {
			do {
				ret = dup2(i, i);
			} while (ret == -1 && errno == EINTR);
			if (ret != -1) {
				result++;
			}
		}
		return result;
	}

	struct ApplicationPoolServerTest {
		ApplicationPoolServerPtr server;
		ApplicationPoolPtr pool, pool2;
		
		ApplicationPoolServerTest() {
			if (firstRun) {
				initialFileDescriptors = countOpenFileDescriptors();
				firstRun = false;
			}
			server = ptr(new ApplicationPoolServer(
				"../ext/apache2/ApplicationPoolServerExecutable",
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
	
	TEST_METHOD(5) {
		// ApplicationPoolServer should not leak file descriptors after running all
		// of the above tests.
		server = ApplicationPoolServerPtr();
		ensure_equals(countOpenFileDescriptors(), initialFileDescriptors);
	}
}

