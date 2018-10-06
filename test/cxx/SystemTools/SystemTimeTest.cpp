#include <TestSupport.h>
#include <SystemTools/SystemTime.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct SystemTools_SystemTimeTest: public TestBase {
		~SystemTools_SystemTimeTest() {
			SystemTime::release();
		}
	};

	DEFINE_TEST_GROUP(SystemTools_SystemTimeTest);

	TEST_METHOD(1) {
		time_t begin = SystemTime::get();

		SystemTime::force(1);
		ensure_equals(SystemTime::get(), (time_t) 1);
		SystemTime::release();

		time_t now = SystemTime::get();
		ensure(now >= begin && now <= begin + 2);
	}

	TEST_METHOD(2) {
		time_t begin = SystemTime::get();

		SystemTime::force(1);
		ensure_equals(SystemTime::get(), (time_t) 1);
		SystemTime::release();

		time_t now = SystemTime::get();
		ensure(now >= begin && now <= begin + 2);
	}
}
