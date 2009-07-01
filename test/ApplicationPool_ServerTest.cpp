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
		string socketFilename;
		AccountsDatabasePtr accountsDatabase;
		AccountPtr clientAccount;
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
			socketFilename = getPassengerTempDir() + "/master/pool_server.sock";
			accountsDatabase = ptr(new AccountsDatabase());
			clientAccount = accountsDatabase->add("test", "12345", false);
			
			realPool = ptr(new Pool("../bin/passenger-spawn-server"));
			server   = ptr(new Server(socketFilename, accountsDatabase, realPool));
			serverThread = ptr(new oxt::thread(
				boost::bind(&Server::mainLoop, server.get())
			));
			pool     = ptr(new Client());
			pool2    = ptr(new Client());
			pool->connect(socketFilename, "test", "12345");
			pool2->connect(socketFilename, "test", "12345");
		}
		
		Application::SessionPtr spawnRackApp() {
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
		
		Application::SessionPtr session1 = pool->get(options);
		session1.reset();
		ensure_equals(strList->counter, 1);
		
		session1 = pool->get(options);
		session1.reset();
		ensure_equals(strList->counter, 1);
	}
	
	TEST_METHOD(2) {
		// It supports hashed passwords.
		initializePool();
		accountsDatabase->add("hashed_user", Account::createHash("67890"), true);
		Client().connect(socketFilename, "hashed_user", "67890"); // Should not throw exception.
	}
	
	TEST_METHOD(3) {
		// It rejects the connection if the an invalid username or password was sent.
		initializePool();
		accountsDatabase->add("hashed_user", Account::createHash("67890"), true);
		
		try {
			Client().connect(socketFilename, "testt", "12345");
			fail("SecurityException expected when invalid username is given");
		} catch (const SecurityException &) {
			// Pass.
		}
		try {
			Client().connect(socketFilename, "test", "123456");
			fail("SecurityException expected when invalid password is given for an account with plain text password");
		} catch (const SecurityException &) {
			// Pass.
		}
		try {
			Client().connect(socketFilename, "test", "678900");
			fail("SecurityException expected when invalid password is given for an account with hashed password");
		} catch (const SecurityException &) {
			// Pass.
		}
	}
	
	TEST_METHOD(4) {
		// It disconnects the client if the client does not supply a username and
		// password within a time limit.
		initializePool();
		server->setLoginTimeout(40);
		
		try {
			// This client takes too much time on sending the username.
			SlowClient(50, 0).connect(socketFilename, "test", "12345");
			fail("IOException or SystemException expected (1).");
		} catch (const IOException &e) {
			// Pass.
		} catch (const SystemException &e) {
			// Pass.
		}
		
		try {
			// This client takes too much time on sending the password.
			SlowClient(0, 50).connect(socketFilename, "test", "12345");
			fail("IOException or SystemException expected (2).");
		} catch (const IOException &e) {
			// Pass.
		} catch (const SystemException &e) {
			// Pass.
		}
		
		try {
			// This client is fast enough at sending the username and
			// password individually, but the combined time is too long.
			SlowClient(25, 25).connect(socketFilename, "test", "12345");
			fail("IOException or SystemException expected (3).");
		} catch (const IOException &e) {
			// Pass.
		} catch (const SystemException &e) {
			// Pass.
		}
	}
	
	TEST_METHOD(5) {
		// It disconnects the client if it provides a username that's too large.
		initializePool();
		char username[1024];
		memset(username, 'x', sizeof(username));
		username[sizeof(username) - 1] = '\0';
		try {
			Client().connect(socketFilename, username, "1234");
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
	}
	
	TEST_METHOD(6) {
		// It disconnects the client if it provides a password that's too large.
		initializePool();
		char password[1024];
		memset(password, 'x', sizeof(password));
		password[sizeof(password) - 1] = '\0';
		try {
			Client().connect(socketFilename, "test", password);
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
	}
	
	TEST_METHOD(10) {
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
	
	TEST_METHOD(11) {
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
	
	TEST_METHOD(12) {
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
	
	TEST_METHOD(13) {
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
	
	TEST_METHOD(14) {
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
	
	TEST_METHOD(15) {
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
	
	TEST_METHOD(16) {
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
	
	TEST_METHOD(17) {
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
}

