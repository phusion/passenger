#include "TestSupport.h"
#include "ApplicationPool/Pool.h"
#include "Utils.h"

using namespace Passenger;

namespace tut {
	struct ApplicationPool_PoolTest {
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		ApplicationPool::Ptr pool, pool2;
		
		ApplicationPool_PoolTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			pool = ptr(new ApplicationPool::Pool("../helper-scripts/passenger-spawn-server", generation));
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
