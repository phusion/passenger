#include "tut.h"
#include <boost/thread.hpp>
#include "ApplicationPoolServer.h"

using namespace Passenger;
using namespace boost;

namespace tut {
	struct ApplicationPoolServer_ApplicationPoolTest {
		ApplicationPoolServerPtr server;
		ApplicationPoolPtr pool, pool2;
		
		ApplicationPoolServer_ApplicationPoolTest() {
			server = ptr(new ApplicationPoolServer(
				"../ext/apache2/ApplicationPoolServerExecutable",
				"../bin/passenger-spawn-server"));
			pool = server->connect();
			pool2 = server->connect();
		}
		
		ApplicationPoolPtr newPoolConnection() {
			return server->connect();
		}
	};
	
	DEFINE_TEST_GROUP(ApplicationPoolServer_ApplicationPoolTest);
	
	#define USE_TEMPLATE
	#include "ApplicationPoolTest.cpp"
}

