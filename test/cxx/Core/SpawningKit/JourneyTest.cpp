#include <TestSupport.h>
#include <Core/SpawningKit/Journey.h>

using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_JourneyTest: public TestBase {
	};

	DEFINE_TEST_GROUP(Core_SpawningKit_JourneyTest);

	TEST_METHOD(1) {
		set_test_name("Constructing a SPAWN_DIRECTLY journey results in"
			" the appropriate steps being defined in the journey");
		Journey journey(SPAWN_DIRECTLY, true);
		ensure("(1)", journey.hasStep(SPAWNING_KIT_PREPARATION));
		ensure("(2)", journey.hasStep(SUBPROCESS_EXEC_WRAPPER));
		ensure("(3)", !journey.hasStep(SPAWNING_KIT_CONNECT_TO_PRELOADER));
		ensure("(4)", !journey.hasStep(SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER));
	}

	TEST_METHOD(2) {
		set_test_name("Constructing a START_PRELOADER journey results in"
			" the appropriate steps being defined in the journey");
		Journey journey(START_PRELOADER, true);
		ensure("(1)", journey.hasStep(SPAWNING_KIT_PREPARATION));
		ensure("(2)", journey.hasStep(SUBPROCESS_EXEC_WRAPPER));
		ensure("(3)", !journey.hasStep(SPAWNING_KIT_CONNECT_TO_PRELOADER));
		ensure("(4)", !journey.hasStep(SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER));
	}

	TEST_METHOD(3) {
		set_test_name("Constructing a SPAWN_THROUGH_PRELOADER journey results in"
			" the appropriate steps being defined in the journey");
		Journey journey(SPAWN_THROUGH_PRELOADER, true);
		ensure("(1)", journey.hasStep(SPAWNING_KIT_PREPARATION));
		ensure("(2)", !journey.hasStep(SUBPROCESS_BEFORE_FIRST_EXEC));
		ensure("(3)", journey.hasStep(SPAWNING_KIT_CONNECT_TO_PRELOADER));
		ensure("(4)", journey.hasStep(SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER));
	}
}
