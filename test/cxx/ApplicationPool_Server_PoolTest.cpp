#include "TestSupport.h"

#include <string>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

#include "ApplicationPool/Pool.h"
#include "ApplicationPool/Server.h"
#include "ApplicationPool/Client.h"
#include "Utils.h"

using namespace Passenger;
using namespace std;
using namespace boost;

namespace tut {
	struct ApplicationPool_Server_PoolTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		AccountsDatabasePtr accountsDatabase;
		shared_ptr<MessageServer> messageServer;
		shared_ptr<ApplicationPool::Pool> realPool;
		shared_ptr<ApplicationPool::Server> poolServer;
		shared_ptr<ApplicationPool::Client> pool, pool2;
		shared_ptr<oxt::thread> serverThread;
		string socketFilename;
		
		ApplicationPool_Server_PoolTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			socketFilename = generation->getPath() + "/socket";
			accountsDatabase = ptr(new AccountsDatabase());
			accountsDatabase->add("test", "12345", false);
			
			messageServer = ptr(new MessageServer(socketFilename, accountsDatabase));
			realPool      = ptr(new ApplicationPool::Pool("../helper-scripts/passenger-spawn-server", generation));
			poolServer    = ptr(new ApplicationPool::Server(realPool));
			messageServer->addHandler(poolServer);
			serverThread = ptr(new oxt::thread(
				boost::bind(&MessageServer::mainLoop, messageServer.get())
			));
			pool  = newPoolConnection();
			pool2 = newPoolConnection();
		}
		
		~ApplicationPool_Server_PoolTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
		}
		
		void reinitializeWithSpawnManager(AbstractSpawnManagerPtr spawnManager) {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
			
			messageServer.reset(); // Wait until the previous instance has removed the socket.
			messageServer = ptr(new MessageServer(socketFilename, accountsDatabase));
			realPool      = ptr(new ApplicationPool::Pool(spawnManager));
			poolServer    = ptr(new ApplicationPool::Server(realPool));
			messageServer->addHandler(poolServer);
			serverThread = ptr(new oxt::thread(
				boost::bind(&MessageServer::mainLoop, messageServer.get())
			));
			pool  = newPoolConnection();
			pool2 = newPoolConnection();
		}
		
		shared_ptr<ApplicationPool::Client> newPoolConnection() {
			shared_ptr<ApplicationPool::Client> p(new ApplicationPool::Client());
			p->connect(socketFilename, "test", "12345");
			return p;
		}
	};
	
	DEFINE_TEST_GROUP(ApplicationPool_Server_PoolTest);
	
	#define USE_TEMPLATE
	#include "ApplicationPool_PoolTestCases.cpp"
}

