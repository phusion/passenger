#include <TestSupport.h>
#include <ApplicationPool2/Spawner.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool2;

namespace tut {
	struct ApplicationPool2_DirectSpawnerTest {
		BackgroundEventLoop bg;
		
		ApplicationPool2_DirectSpawnerTest() {
			bg.start();
		}
		
		shared_ptr<DirectSpawner> createSpawner(const Options &options) {
			return make_shared<DirectSpawner>(*resourceLocator);
		}
		
		Options createOptions() {
			Options options;
			options.spawnMethod = "direct";
			options.loadShellEnvvars = false;
			return options;
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool2_DirectSpawnerTest);
	
	#include "SpawnerTestCases.cpp"
}
