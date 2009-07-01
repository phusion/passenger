#include "tut.h"
#include "support/Support.h"

#include <boost/bind.hpp>

#include "ApplicationPool/Pool.h"
#include "ApplicationPool/Server.h"
#include "ApplicationPool/Client.h"
#include "Utils.h"
#include <string>
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool;
using namespace boost;
using namespace std;

namespace tut {
	struct ApplicationPool_ServerTest {
		AccountsDatabasePtr accountsDatabase;
		shared_ptr<Pool> realPool;
		shared_ptr<Server> server;
		shared_ptr<Client> pool, pool2;
		shared_ptr<oxt::thread> serverThread;
		
		~ApplicationPool_ServerTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
		}
		
		void initializePool() {
			string socketFilename = getPassengerTempDir() + "/master/pool_server.sock";
			if (accountsDatabase == NULL) {
				initializeAccountsDatabase();
				accountsDatabase->add("test", "12345", false);
			}
			
			realPool = ptr(new Pool("../bin/passenger-spawn-server"));
			server   = ptr(new Server(socketFilename, accountsDatabase, realPool));
			serverThread = ptr(new oxt::thread(
				boost::bind(&Server::mainLoop, server.get())
			));
			pool     = ptr(new Client(socketFilename, "test", "12345"));
			pool2    = ptr(new Client(socketFilename, "test", "12345"));
		}
		
		void initializeAccountsDatabase() {
			accountsDatabase = ptr(new AccountsDatabase());
		}
		
		Application::SessionPtr spawnRackApp() {
			PoolOptions options("stub/rack");
			options.appType = "rack";
			return pool->get(options);
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool_ServerTest);
	
	/* A StringListCreator which not only returns a dummy value, but also
	 * increments a counter each time getItems() is called. */
	class DummyStringListCreator: public StringListCreator {
	public:
		mutable int counter;
		
		DummyStringListCreator() {
			counter = 0;
		}
		
		virtual const StringListPtr getItems() const {
			StringListPtr result = ptr(new StringList());
			counter++;
			result->push_back("hello");
			result->push_back("world");
			return result;
		}
	};
	
	TEST_METHOD(1) {
		// When calling get() with a PoolOptions object,
		// options.environmentVariables->getItems() isn't called unless
		// the pool had to spawn something.
		initializePool();
		
		shared_ptr<DummyStringListCreator> strList = ptr(new DummyStringListCreator());
		PoolOptions options("stub/rack");
		options.appType = "rack";
		options.environmentVariables = strList;
		
		Application::SessionPtr session1 = pool->get(options);
		session1.reset();
		ensure_equals(strList->counter, 1);
		
		session1 = pool->get(options);
		session1.reset();
		ensure_equals(strList->counter, 1);
	}
	
	TEST_METHOD(2) {
		initializeAccountsDatabase();
		AccountPtr account = accountsDatabase->add("test", "12345", false, Account::NONE);
		initializePool();
		
		try {
			spawnRackApp();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		account->setRights(Account::GET);
		spawnRackApp(); // Should not throw SecurityException now.
	}
	
}

