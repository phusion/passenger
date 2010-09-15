#include "TestSupport.h"
#include "SpawnManager.h"
#include <sys/types.h>
#include <signal.h>
#include <cstring>
#include <unistd.h>
#include "valgrind.h"

using namespace Passenger;

namespace tut {
	struct SpawnManagerTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		SpawnManagerPtr manager;
		AccountsDatabasePtr accountsDatabase;
		PoolOptions rackOptions;
		
		SpawnManagerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			rackOptions.appRoot = "stub/rack";
			rackOptions.appType = "rack";
		}
		
		void initialize() {
			manager = ptr(new SpawnManager("../helper-scripts/passenger-spawn-server", generation,
				accountsDatabase));
		}
		
		void sendTestRequest(SessionPtr &session, bool authenticate = true, const char *uri = "/foo/new") {
			string headers;
			#define ADD_HEADER(name, value) \
				headers.append(name); \
				headers.append(1, '\0'); \
				headers.append(value); \
				headers.append(1, '\0')
			ADD_HEADER("HTTP_HOST", "www.test.com");
			ADD_HEADER("QUERY_STRING", "");
			ADD_HEADER("REQUEST_URI", uri);
			ADD_HEADER("REQUEST_METHOD", "GET");
			ADD_HEADER("REMOTE_ADDR", "localhost");
			ADD_HEADER("SCRIPT_NAME", "");
			ADD_HEADER("PATH_INFO", uri);
			if (authenticate) {
				ADD_HEADER("PASSENGER_CONNECT_PASSWORD", session->getConnectPassword());
			}
			session->sendHeaders(headers);
		}
	};

	DEFINE_TEST_GROUP(SpawnManagerTest);

	TEST_METHOD(1) {
		// Spawning an application should return a valid Application object.
		initialize();
		ProcessPtr process = manager->spawn(rackOptions);
		SessionPtr session = process->newSession();
		sendTestRequest(session);
		session->shutdownWriter();
		string result = readAll(session->getStream());
		ensure(result.find("hello <b>world</b>") != string::npos);
	}
	
	TEST_METHOD(2) {
		// If something goes wrong during spawning, the spawn manager
		// should be restarted and another (successful) spawn should be attempted.
		initialize();
		pid_t old_pid = manager->getServerPid();
		manager->killSpawnServer();
		// Give the spawn server the time to properly terminate.
		usleep(500000);
		
		ProcessPtr process = manager->spawn(rackOptions);
		SessionPtr session = process->newSession();
		sendTestRequest(session);
		session->shutdownWriter();
		string result = readAll(session->getStream());
		ensure(result.find("hello <b>world</b>") != string::npos);
		
		// The following test will fail if we're inside Valgrind, but that's normal.
		// Killing the spawn server doesn't work there.
		if (!RUNNING_ON_VALGRIND) {
			ensure("The spawn server was restarted", manager->getServerPid() != old_pid);
		}
	}
	
	class BuggySpawnManager: public SpawnManager {
	protected:
		virtual void spawnServerStarted() {
			if (nextRestartShouldFail) {
				nextRestartShouldFail = false;
				killSpawnServer();
				usleep(25000);
			}
		}
		
	public:
		bool nextRestartShouldFail;
		
		BuggySpawnManager(const ServerInstanceDir::GenerationPtr &generation)
			: SpawnManager("stub/spawn_server.rb", generation)
		{
			nextRestartShouldFail = false;
		}
	};
	
	TEST_METHOD(3) {
		// If the spawn server dies after a restart, a SpawnException should be thrown.
		
		// This test fails in Valgrind, but that's normal.
		// Killing the spawn server doesn't work there.
		if (!RUNNING_ON_VALGRIND) {
			BuggySpawnManager manager(generation);
			manager.killSpawnServer();
			// Give the spawn server the time to properly terminate.
			usleep(250000);
			
			try {
				manager.nextRestartShouldFail = true;
				ProcessPtr process = manager.spawn(rackOptions);
				fail("SpawnManager did not throw a SpawnException");
			} catch (const SpawnException &e) {
				// Success.
			}
		}
	}
	
	TEST_METHOD(4) {
		// The connect password is passed to the spawned application, which rejects
		// sessions that aren't authenticated with the right password.
		initialize();
		ProcessPtr process = manager->spawn(rackOptions);
		SessionPtr session = process->newSession();
		sendTestRequest(session, false);
		session->shutdownWriter();
		string result = readAll(session->getStream());
		ensure_equals(result, "");
	}
	
	TEST_METHOD(5) {
		// It automatically creates a unique account for the application,
		// which is deleted when no longer needed.
		accountsDatabase = ptr(new AccountsDatabase());
		initialize();
		
		ProcessPtr process1 = manager->spawn(rackOptions);
		vector<string> usernames1 = accountsDatabase->listUsernames();
		ensure_equals(accountsDatabase->size(), 1u);
		
		ProcessPtr process2 = manager->spawn(rackOptions);
		vector<string> usernames2 = accountsDatabase->listUsernames();
		ensure_equals(accountsDatabase->size(), 2u);
		
		process1.reset();
		ensure_equals(accountsDatabase->size(), 1u);
		ensure_equals(accountsDatabase->get(usernames1[0]), AccountPtr());
		
		process2.reset();
		ensure_equals(accountsDatabase->size(), 0u);
	}
}
