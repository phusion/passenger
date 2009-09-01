#include "tut.h"
#include "support/Support.h"

#include <boost/bind.hpp>

#include "MessageServer.h"
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
	struct MessageServerTest {
		string socketFilename;
		AccountsDatabasePtr accountsDatabase;
		AccountPtr clientAccount;
		shared_ptr<MessageServer> messageServer;
		shared_ptr<oxt::thread> serverThread;
		
		MessageServerTest() {
			socketFilename = getPassengerTempDir() + "/master/pool_server.sock";
			accountsDatabase = ptr(new AccountsDatabase());
			clientAccount = accountsDatabase->add("test", "12345", false);
			
			messageServer = ptr(new MessageServer(socketFilename, accountsDatabase));
			serverThread  = ptr(new oxt::thread(
				boost::bind(&MessageServer::mainLoop, messageServer.get())
			));
		}
		
		~MessageServerTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
		}
		
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

	DEFINE_TEST_GROUP(MessageServerTest);
	
	TEST_METHOD(1) {
		// It rejects the connection if the an invalid username or password was sent.
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
	
	TEST_METHOD(2) {
		// It supports hashed passwords.
		accountsDatabase->add("hashed_user", Account::createHash("67890"), true);
		Client().connect(socketFilename, "hashed_user", "67890"); // Should not throw exception.
	}
	
	TEST_METHOD(3) {
		// It disconnects the client if the client does not supply a username and
		// password within a time limit.
		messageServer->setLoginTimeout(40);
		
		/* These can throw either an IOException or SystemException:
		 * - An IOException is raised when connect() encounters EOF.
		 * - But when connect() fails during the middle of a read()
		 *   or write() (e.g. because the server disconnects before
		 *   connect() is done writing), then SystemException is raised.
		 */
		
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
	
	TEST_METHOD(4) {
		// It disconnects the client if it provides a username that's too large.
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
	
	TEST_METHOD(5) {
		// It disconnects the client if it provides a password that's too large.
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
}

