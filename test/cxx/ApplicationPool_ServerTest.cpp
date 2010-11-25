#include "TestSupport.h"

#include <boost/bind.hpp>

#include "ApplicationPool/Pool.h"
#include "ApplicationPool/Server.h"
#include "MessageClient.h"
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
		shared_ptr<Pool> pool;
		shared_ptr<Server> poolServer;
		shared_ptr<MessageClient> client, client2;
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
			pool          = ptr(new Pool("../helper-scripts/passenger-spawn-server", generation));
			poolServer    = ptr(new Server(pool));
			messageServer->addHandler(poolServer);
			serverThread  = ptr(new oxt::thread(
				boost::bind(&MessageServer::mainLoop, messageServer.get())
			));
			client        = ptr(new MessageClient());
			client2       = ptr(new MessageClient());
			client->connect("unix:" + socketFilename, "test", "12345");
			client2->connect("unix:" + socketFilename, "test", "12345");
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool_ServerTest);
	
	TEST_METHOD(1) {
		// clear() requires CLEAR rights.
		initializePool();
		vector<string> args;
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		client->write("clear", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::CLEAR);
		client->write("clear", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
	}
	
	TEST_METHOD(2) {
		// setMaxIdleTime() requires SET_PARAMETERS rights.
		initializePool();
		vector<string> args;
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		client->write("setMaxIdleTime", "1", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		client->write("setMaxIdleTime", "1", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
	}
	
	TEST_METHOD(3) {
		// setMax() requires SET_PARAMETERS rights.
		initializePool();
		vector<string> args;
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		client->write("setMax", "2", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		client->write("setMax", "2", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
	}
	
	TEST_METHOD(4) {
		// getActive() requires GET_PARAMETERS rights.
		initializePool();
		vector<string> args;
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		client->write("getActive", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		client->write("getActive", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
		ensure(client->read(args));
	}
	
	TEST_METHOD(10) {
		// getCount() requires GET_PARAMETERS rights.
		initializePool();
		vector<string> args;
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		client->write("getCount", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		client->write("getCount", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
		ensure(client->read(args));
	}
	
	TEST_METHOD(11) {
		// setMaxPerApp() requires SET_PARAMETERS rights.
		initializePool();
		vector<string> args;
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		client->write("setMaxPerApp", "2", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::SET_PARAMETERS);
		client->write("setMaxPerApp", "2", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
	}
	
	TEST_METHOD(13) {
		// inspect() requires INSPECT_BASIC_INFO rights.
		initializePool();
		vector<string> args;
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		client->write("inspect", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::INSPECT_BASIC_INFO);
		client->write("inspect", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
		ensure(client->read(args));
	}
	
	TEST_METHOD(14) {
		// toXml() requires INSPECT_BASIC_INFO rights.
		initializePool();
		vector<string> args;
		string data;
		
		clientAccount->setRights(Account::GET_PARAMETERS);
		client->write("toXml", "true", NULL);
		ensure("(1)", client->read(args));
		ensure_equals(args[0], "SecurityException");
		
		clientAccount->setRights(Account::INSPECT_BASIC_INFO);
		client->write("toXml", "true", NULL);
		ensure("(2)", client->read(args));
		ensure_equals(args[0], "Passed security");
		ensure("(3)", client->readScalar(data));
	}
	
	TEST_METHOD(15) {
		// toXml() only prints private information if the client has the INSPECT_SENSITIVE_INFO right.
		initializePool();
		vector<string> args;
		string data;
		
		PoolOptions options;
		options.appRoot = "stub/rack";
		options.appType = "rack";
		pool->get(options);
		
		clientAccount->setRights(Account::INSPECT_BASIC_INFO);
		client->write("toXml", "true", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
		ensure(client->readScalar(data));
		ensure_equals("Does not contain private information",
			data.find("<server_sockets>"),
			string::npos);
		
		clientAccount->setRights(Account::INSPECT_BASIC_INFO | Account::INSPECT_SENSITIVE_INFO);
		client->write("toXml", "true", NULL);
		ensure(client->read(args));
		ensure_equals(args[0], "Passed security");
		ensure(client->readScalar(data));
		ensure("Contains private information", data.find("<server_sockets>") != string::npos);
	}
}
