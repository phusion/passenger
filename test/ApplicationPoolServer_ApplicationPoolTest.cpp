#include "tut.h"
#include "ApplicationPoolClientServer.h"

using namespace Passenger;

namespace tut {
	struct ApplicationPoolServer_ApplicationPoolTest {
		ApplicationPoolServerPtr server;
		ApplicationPoolPtr pool, pool2;
		
		ApplicationPoolServer_ApplicationPoolTest() {
			server = ptr(new ApplicationPoolServer("../bin/passenger-spawn-server"));
			pool = server->connect();
			pool2 = server->connect();
		}
	};
	
	DEFINE_TEST_GROUP(ApplicationPoolServer_ApplicationPoolTest);
	
	#define USE_TEMPLATE
	#include "ApplicationPoolTest.cpp"
}

