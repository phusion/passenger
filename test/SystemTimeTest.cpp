#include "tut.h"
#include "SystemTime.h"

using namespace Passenger;
using namespace std;

namespace tut {
	struct SystemTimeTest {
		~SystemTimeTest() {
			passenger_system_time_release_forced_value();
		}
	};
	
	DEFINE_TEST_GROUP(SystemTimeTest);
	
	TEST_METHOD(1) {
		time_t begin = passenger_system_time_get();
		
		passenger_system_time_force_value(1);
		ensure_equals(passenger_system_time_get(), (time_t) 1);
		passenger_system_time_release_forced_value();
		
		time_t now = passenger_system_time_get();
		ensure(now >= begin && now <= begin + 2);
	}
	
	TEST_METHOD(2) {
		time_t begin = SystemTime::get();
		
		passenger_system_time_force_value(1);
		ensure_equals(passenger_system_time_get(), (time_t) 1);
		passenger_system_time_release_forced_value();
		
		time_t now = SystemTime::get();
		ensure(now >= begin && now <= begin + 2);
	}
}
