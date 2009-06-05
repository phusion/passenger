#include "tut.h"
#include "support/Support.h"

#include <string>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

#include "StandardApplicationPool.h"
#include "ApplicationPoolServer.h"
#include "ApplicationPoolClient.h"
#include "Utils.h"

using namespace Passenger;
using namespace std;
using namespace boost;
using namespace Test;

namespace tut {
	struct ApplicationPoolServer_ApplicationPoolTest {
		shared_ptr<StandardApplicationPool> realPool;
		shared_ptr<ApplicationPoolServer> server;
		shared_ptr<ApplicationPoolClient> pool, pool2;
		shared_ptr<oxt::thread> serverThread;
		string socketFilename;
		
		ApplicationPoolServer_ApplicationPoolTest() {
			socketFilename = getPassengerTempDir() + "/master/pool_server.sock";
			realPool = ptr(new StandardApplicationPool("../bin/passenger-spawn-server"));
			server = ptr(new ApplicationPoolServer(socketFilename, "12345", realPool));
			pool = ptr(new ApplicationPoolClient(socketFilename, "12345"));
			pool2 = ptr(new ApplicationPoolClient(socketFilename, "12345"));
			serverThread = ptr(new oxt::thread(
				boost::bind(&ApplicationPoolServer::mainLoop, server.get())
			));
		}
		
		~ApplicationPoolServer_ApplicationPoolTest() {
			if (serverThread != NULL) {
				serverThread->interrupt_and_join();
			}
		}
		
		ApplicationPoolPtr newPoolConnection() {
			return ptr(new ApplicationPoolClient(socketFilename, "12345"));
		}
	};
	
	DEFINE_TEST_GROUP(ApplicationPoolServer_ApplicationPoolTest);
	
	#define USE_TEMPLATE
	#include "ApplicationPoolTest.cpp"
}

