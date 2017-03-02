#include <TestSupport.h>
#include <ConfigKit/Common.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct ConfigKit_ErrorTest {
	};

	DEFINE_TEST_GROUP(ConfigKit_ErrorTest);

	static string barKeyProcessor(const StaticString &key) {
		return "bar";
	}

	TEST_METHOD(1) {
		ConfigKit::Error error("Key {{foo}} is invalid");
		ensure_equals(error.getMessage(), "Key foo is invalid");
		ensure_equals(error.getMessage(barKeyProcessor), "Key bar is invalid");
	}
}
