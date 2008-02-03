#include "tut.h"
#include "ApplicationPoolClientServer.h"
#include "Utils.cpp"
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;

namespace tut {
	static bool firstRun = true;
	static unsigned int initialFileDescriptors;

	struct ApplicationPoolClientServerTest {
		ApplicationPoolServerPtr server;
		
		ApplicationPoolClientServerTest() {
			if (firstRun) {
				initialFileDescriptors = countOpenFileDescriptors();
				firstRun = false;
			}
			server = ptr(new ApplicationPoolServer("support/spawn_server_mock.rb"));
		}
		
		unsigned int countOpenFileDescriptors() {
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
	
	TEST_METHOD(5) {
		// ApplicationPoolServer should not leak file descriptors after running all
		// of the above tests.
		server = ApplicationPoolServerPtr();
		ensure_equals(countOpenFileDescriptors(), initialFileDescriptors);
	}
}
