#include "TestSupport.h"

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
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		string socketFilename;
		AccountsDatabasePtr accountsDatabase;
		AccountPtr clientAccount;
		shared_ptr<MessageServer> messageServer;
		shared_ptr<Pool> realPool;
		shared_ptr<Server> poolServer;
		shared_ptr<Client> pool, pool2;
		shared_ptr<oxt::thread> serverThread;
		
		~ApplicationPool_ServerTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
		}
		
		void initializePool() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			socketFilename = generation->getPath() + "/socket";
			accountsDatabase = ptr(new AccountsDatabase());
			clientAccount = accountsDatabase->add("test", "12345", false);
			
			messageServer = ptr(new MessageServer(socketFilename, accountsDatabase));
			realPool      = ptr(new Pool("../helper-scripts/passenger-spawn-server", generation));
			poolServer    = ptr(new Server(realPool));
			messageServer->addHandler(poolServer);
			serverThread = ptr(new oxt::thread(
				boost::bind(&MessageServer::mainLoop, messageServer.get())
			));
			pool     = ptr(new Client());
			pool2    = ptr(new Client());
			pool->connect(socketFilename, "test", "12345");
			pool2->connect(socketFilename, "test", "12345");
		}
		
		SessionPtr spawnRackApp() {
			PoolOptions options("stub/rack");
			options.appType = "rack";
			return pool->get(options);
		}
		
		
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
		
		class SlowClient: public Client {
		private:
			unsigned int timeToSendUsername;
			unsigned int timeToSendPassword;
			
		protected:
			virtual void sendUsername(MessageChannel &channel, const string &username) {
				if (timeToSendUsername > 0) {
					usleep(timeToSendUsername * 1000);
				}
				channel.writeScalar(username);
			}

			virtual void sendPassword(MessageChannel &channel, const StaticString &userSuppliedPassword) {
				if (timeToSendPassword > 0) {
					usleep(timeToSendPassword * 1000);
				}
				channel.writeScalar(userSuppliedPassword.c_str(), userSuppliedPassword.size());
			}
			
		public:
			SlowClient(unsigned int timeToSendUsername,
			           unsigned int timeToSendPassword)
			         : Client()
			{
				this->timeToSendUsername = timeToSendUsername;
				this->timeToSendPassword = timeToSendPassword;
			}
		};
	};

	DEFINE_TEST_GROUP(ApplicationPool_ServerTest);
	
	TEST_METHOD(1) {
		// When calling get() with a PoolOptions object,
		// options.environmentVariables->getItems() isn't called unless
		// the pool had to spawn something.
		initializePool();
		
		shared_ptr<DummyStringListCreator> strList = ptr(new DummyStringListCreator());
		PoolOptions options("stub/rack");
		options.appType = "rack";
		options.environmentVariables = strList;
		
		SessionPtr session1 = pool->get(options);
		session1.reset();
		ensure_equals("(1)", strList->counter, 1);
		
		session1 = pool->get(options);
		session1.reset();
		ensure_equals("(2)", strList->counter, 1);
	}
	
	TEST_METHOD(5) {
		// get() requires GET rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::SET_PARAMETERS);
			spawnRackApp();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::GET);
		spawnRackApp(); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(6) {
		// clear() requires CLEAR rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::SET_PARAMETERS);
			pool->clear();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::CLEAR);
		pool->clear(); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(7) {
		// setMaxIdleTime() requires SET_PARAMETERS rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::GET_PARAMETERS);
			pool->setMaxIdleTime(60);
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		pool->setMaxIdleTime(60); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(8) {
		// setMax() requires SET_PARAMETERS rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::GET_PARAMETERS);
			pool->setMax(60);
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		pool->setMax(60); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(9) {
		// getActive() requires GET_PARAMETERS rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::SET_PARAMETERS);
			pool->getActive();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		pool->getActive(); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(10) {
		// getCount() requires GET_PARAMETERS rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::SET_PARAMETERS);
			pool->getCount();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		pool->getCount(); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(11) {
		// setMaxPerApp() requires SET_PARAMETERS rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::GET_PARAMETERS);
			pool->setMaxPerApp(2);
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		pool->setMaxPerApp(2); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(12) {
		// getSpawnServerPid() requires GET_PARAMETERS rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::SET_PARAMETERS);
			pool->getSpawnServerPid();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		pool->getSpawnServerPid(); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(13) {
		// inspect() requires INSPECT_BASIC_INFO rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::SET_PARAMETERS);
			pool->inspect();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::INSPECT_BASIC_INFO);
		pool->inspect(); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(14) {
		// toXml() requires INSPECT_BASIC_INFO rights.
		initializePool();
		
		try {
			clientAccount->setRights(Account::SET_PARAMETERS);
			pool->toXml();
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
		
		clientAccount->setRights(Account::INSPECT_BASIC_INFO);
		pool->toXml(); // Should not throw SecurityException now.
	}
	
	TEST_METHOD(15) {
		// toXml() only prints private information if the client has the INSPECT_SENSITIVE_INFO right.
		initializePool();
		PoolOptions options("stub/rack");
		options.appType = "rack";
		pool->get(options);
		
		clientAccount->setRights(Account::INSPECT_BASIC_INFO);
		ensure("Does not contain private information", pool->toXml().find("<server_sockets>") == string::npos);
		clientAccount->setRights(Account::INSPECT_BASIC_INFO | Account::INSPECT_SENSITIVE_INFO);
		ensure("Contains private information", pool->toXml().find("<server_sockets>") != string::npos);
	}
}
