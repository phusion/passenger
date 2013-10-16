#include "TestSupport.h"

#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include "MessageServer.h"
#include "MessageClient.h"
#include "Utils.h"
#include <string>
#include <cstring>
#include <unistd.h>
#include <errno.h>

using namespace Passenger;
using namespace boost;
using namespace std;

namespace tut {
	struct MessageServerTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		string socketFilename;
		string socketAddress;
		AccountsDatabasePtr accountsDatabase;
		AccountPtr clientAccount;
		boost::shared_ptr<MessageServer> server;
		boost::shared_ptr<oxt::thread> serverThread;
		
		MessageServerTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			socketFilename = generation->getPath() + "/socket";
			socketAddress = "unix:" + socketFilename;
			accountsDatabase = ptr(new AccountsDatabase());
			clientAccount = accountsDatabase->add("test", "12345", false);
			
			server = ptr(new MessageServer(socketFilename, accountsDatabase));
			serverThread  = ptr(new oxt::thread(
				boost::bind(&MessageServer::mainLoop, server.get())
			));
		}
		
		~MessageServerTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
			Passenger::setLogLevel(0);
		}
		
		class SlowClient: public MessageClient {
		private:
			unsigned int timeToSendUsername;
			unsigned int timeToSendPassword;
			
		protected:
			virtual void sendUsername(int fd, const StaticString &username, unsigned long long *timeout) {
				if (timeToSendUsername > 0) {
					usleep(timeToSendUsername * 1000);
				}
				writeScalarMessage(fd, username);
			}

			virtual void sendPassword(int fd, const StaticString &userSuppliedPassword, unsigned long long *timeout) {
				if (timeToSendPassword > 0) {
					usleep(timeToSendPassword * 1000);
				}
				writeScalarMessage(fd, userSuppliedPassword);
			}
			
		public:
			SlowClient(unsigned int timeToSendUsername,
			           unsigned int timeToSendPassword)
			         : MessageClient()
			{
				this->timeToSendUsername = timeToSendUsername;
				this->timeToSendPassword = timeToSendPassword;
			}
		};
		
		class CustomClient: public MessageClient {
		public:
			CustomClient *sendText(const string &text) {
				write(text.c_str(), NULL);
				return this;
			}
		};
		
		class LoggingHandler: public MessageServer::Handler {
		public:
			struct SpecificContext: public MessageServer::ClientContext {
				int id;
				
				SpecificContext(int i) {
					id = i;
				}
			};
			
			typedef boost::shared_ptr<SpecificContext> SpecificContextPtr;
			
			boost::mutex mutex;
			volatile int clientsAccepted;
			volatile int clientsDisconnected;
			string receivedData;
			volatile int id;
			volatile int returnValue;
			SpecificContextPtr latestContext;
			
			LoggingHandler() {
				clientsAccepted = 0;
				clientsDisconnected = 0;
				returnValue = true;
			}
			
			virtual MessageServer::ClientContextPtr newClient(MessageServer::CommonClientContext &context) {
				boost::lock_guard<boost::mutex> l(mutex);
				clientsAccepted++;
				return ptr(new SpecificContext(id));
			}
			
			virtual void clientDisconnected(MessageServer::CommonClientContext &context,
			                                MessageServer::ClientContextPtr &handlerSpecificContext)
			{
				boost::lock_guard<boost::mutex> l(mutex);
				clientsDisconnected++;
			}
			
			virtual bool processMessage(MessageServer::CommonClientContext &commonContext,
			                            MessageServer::ClientContextPtr &handlerSpecificContext,
			                            const vector<string> &args)
			{
				boost::lock_guard<boost::mutex> l(mutex);
				latestContext = static_pointer_cast<SpecificContext>(handlerSpecificContext);
				receivedData.append(args[0]);
				return returnValue;
			}
		};
		
		typedef boost::shared_ptr<LoggingHandler> LoggingHandlerPtr;
		
		class ProcessMessageReturnsFalseHandler: public MessageServer::Handler {
		public:
			virtual bool processMessage(MessageServer::CommonClientContext &commonContext,
			                            MessageServer::ClientContextPtr &handlerSpecificContext,
			                            const vector<string> &args)
			{
				return false;
			}
		};
	};

	DEFINE_TEST_GROUP(MessageServerTest);
	
	TEST_METHOD(1) {
		// It rejects the connection if the an invalid username or password was sent.
		accountsDatabase->add("hashed_user", Account::createHash("67890"), true);
		
		try {
			MessageClient().connect(socketAddress, "testt", "12345");
			fail("SecurityException expected when invalid username is given");
		} catch (const SecurityException &) {
			// Pass.
		}
		try {
			MessageClient().connect(socketAddress, "test", "123456");
			fail("SecurityException expected when invalid password is given for an account with plain text password");
		} catch (const SecurityException &) {
			// Pass.
		}
		try {
			MessageClient().connect(socketAddress, "test", "678900");
			fail("SecurityException expected when invalid password is given for an account with hashed password");
		} catch (const SecurityException &) {
			// Pass.
		}
	}
	
	TEST_METHOD(2) {
		// It supports hashed passwords.
		accountsDatabase->add("hashed_user", Account::createHash("67890"), true);
		MessageClient().connect(socketAddress, "hashed_user", "67890"); // Should not throw exception.
	}
	
	TEST_METHOD(3) {
		// It disconnects the client if the client does not supply a username and
		// password within a time limit.
		Passenger::setLogLevel(-1);
		server->setLoginTimeout(30000);
		
		/* These can throw either an IOException or SystemException:
		 * - An IOException is raised when connect() encounters EOF.
		 * - But when connect() fails during the middle of a read()
		 *   or write() (e.g. because the server disconnects before
		 *   connect() is done writing), then SystemException is raised.
		 */
		
		try {
			// This client takes too much time on sending the username.
			SlowClient(50, 0).connect(socketAddress, "test", "12345");
			fail("IOException or SystemException expected (1).");
		} catch (const IOException &e) {
			// Pass.
		} catch (const SystemException &e) {
			// Pass.
		}
		
		try {
			// This client takes too much time on sending the password.
			SlowClient(0, 50).connect(socketAddress, "test", "12345");
			fail("IOException or SystemException expected (2).");
		} catch (const IOException &e) {
			// Pass.
		} catch (const SystemException &e) {
			// Pass.
		}
		
		try {
			// This client is fast enough at sending the username and
			// password individually, but the combined time is too long.
			SlowClient(25, 25).connect(socketAddress, "test", "12345");
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
			MessageClient().connect(socketAddress, username, "1234");
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
			MessageClient().connect(socketAddress, "test", password);
			fail("SecurityException expected");
		} catch (const SecurityException &e) {
			// Pass.
		}
	}
	
	TEST_METHOD(6) {
		// It notifies all handlers when a new client has connected.
		LoggingHandlerPtr handler1(new LoggingHandler());
		LoggingHandlerPtr handler2(new LoggingHandler());
		server->addHandler(handler1);
		server->addHandler(handler2);
		MessageClient().connect(socketAddress, "test", "12345");
		MessageClient().connect(socketAddress, "test", "12345");
		
		usleep(10000); // Give the threads some time to do work.
		ensure_equals(handler1->clientsAccepted, 2);
		ensure_equals(handler2->clientsAccepted, 2);
	}
	
	TEST_METHOD(7) {
		// It notifies all handlers when a message is sent, but stops
		// at the first handler that returns true.
		LoggingHandlerPtr handler1(new LoggingHandler());
		LoggingHandlerPtr handler2(new LoggingHandler());
		LoggingHandlerPtr handler3(new LoggingHandler());
		server->addHandler(handler1);
		server->addHandler(handler2);
		server->addHandler(handler3);
		handler1->returnValue = false;
		
		CustomClient c1, c2;
		c1.connect(socketAddress, "test", "12345");
		c1.sendText("hello");
		c1.sendText(" ");
		usleep(10000); // Give the thread some time to do work.
		
		c2.connect(socketAddress, "test", "12345");
		c2.sendText("world");
		usleep(10000); // Give the thread some time to do work.
		
		ensure_equals("(1)", handler1->receivedData, "hello world");
		ensure_equals("(2)", handler2->receivedData, "hello world");
		ensure_equals("(3)", handler3->receivedData, "");
	}
	
	TEST_METHOD(8) {
		// It does not close the connection if some, but not all of the handlers'
		// processMessage() methods return false.
		MessageServer::HandlerPtr handler1(new LoggingHandler());
		MessageServer::HandlerPtr handler2(new ProcessMessageReturnsFalseHandler());
		server->addHandler(handler1);
		server->addHandler(handler2);
		
		CustomClient c;
		c.connect(socketAddress, "test", "12345");
		c.sendText("hi");
		usleep(10000); // Give the thread some time to do work.
		
		c.sendText("hi"); // Connection should still be valid.
	}
	
	TEST_METHOD(9) {
		// It closes the connection if all of the handlers'
		// processMessage() methods return false.
		MessageServer::HandlerPtr handler1(new ProcessMessageReturnsFalseHandler());
		MessageServer::HandlerPtr handler2(new ProcessMessageReturnsFalseHandler());
		server->addHandler(handler1);
		server->addHandler(handler2);
		
		CustomClient c;
		c.connect(socketAddress, "test", "12345");
		c.sendText("hi");
		usleep(10000); // Give the thread some time to do work.
		
		try {
			c.sendText("hi");
			fail("SystemException expected.");
		} catch (const SystemException &e) {
			ensure_equals(e.code(), EPIPE);
		}
	}
	
	TEST_METHOD(10) {
		// The specific context as returned by the handler's newClient()
		// method is passed to processMessage().
		LoggingHandlerPtr handler1(new LoggingHandler());
		LoggingHandlerPtr handler2(new LoggingHandler());
		LoggingHandlerPtr handler3(new LoggingHandler());
		server->addHandler(handler1);
		server->addHandler(handler2);
		server->addHandler(handler3);
		handler1->returnValue = false;
		handler2->returnValue = false;
		
		CustomClient c1, c2;
		
		handler1->id = 100;
		handler2->id = 101;
		c1.connect(socketAddress, "test", "12345");
		c1.sendText("hi");
		usleep(10000); // Give the thread some time to do work.
		ensure_equals(handler1->latestContext->id, 100);
		ensure_equals(handler2->latestContext->id, 101);
		
		handler1->id = 200;
		handler2->id = 201;
		c2.connect(socketAddress, "test", "12345");
		c2.sendText("hi");
		usleep(10000); // Give the thread some time to do work.
		ensure_equals(handler1->latestContext->id, 200);
		ensure_equals(handler2->latestContext->id, 201);
		
		c1.sendText("hi");
		usleep(10000); // Give the thread some time to do work.
		ensure_equals(handler1->latestContext->id, 100);
		ensure_equals(handler2->latestContext->id, 101);
	}
	
	TEST_METHOD(11) {
		// It notifies all handlers when a client is disconnected.
		LoggingHandlerPtr handler1(new LoggingHandler());
		LoggingHandlerPtr handler2(new LoggingHandler());
		server->addHandler(handler1);
		server->addHandler(handler2);
		{
			{
				MessageClient().connect(socketAddress, "test", "12345");
			}
			usleep(10000); // Give the threads some time to do work.
			ensure_equals(handler1->clientsDisconnected, 1);
			ensure_equals(handler2->clientsDisconnected, 1);
			
			MessageClient().connect(socketAddress, "test", "12345");
		}
		usleep(10000); // Give the threads some time to do work.
		ensure_equals(handler1->clientsDisconnected, 2);
		ensure_equals(handler2->clientsDisconnected, 2);
	}
}
