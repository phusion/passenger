#include "tut.h"
#include "support/Support.h"
#include "ApplicationPool/Pool.h"
#include "Utils.h"

using namespace Passenger;
using namespace Test;

namespace tut {
	struct ApplicationPool_PoolTest {
		ApplicationPool::Ptr pool, pool2;
		
		ApplicationPool_PoolTest() {
			pool = ptr(new ApplicationPool::Pool("../bin/passenger-spawn-server"));
			pool2 = pool;
		}
		
		ApplicationPool::Ptr newPoolConnection() {
			return pool;
		}
		
		void reinitializeWithSpawnManager(AbstractSpawnManagerPtr spawnManager) {
			pool = ptr(new ApplicationPool::Pool(spawnManager));
			pool2 = pool;
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool_PoolTest);

	#define USE_TEMPLATE
	#include "ApplicationPool_PoolTestCases.cpp"
}
