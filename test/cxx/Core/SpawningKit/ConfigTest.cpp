#include <TestSupport.h>
#include <Core/SpawningKit/Config.h>
#include <cstdlib>
#include <cstring>

using namespace std;
using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_ConfigTest: public TestBase {
		SpawningKit::Config config;
	};

	DEFINE_TEST_GROUP(Core_SpawningKit_ConfigTest);

	TEST_METHOD(1) {
		set_test_name("internStrings() internalizes all strings into the object");

		char *str1 = (char *) malloc(32);
		strncpy(str1, "hello", 32);
		config.appType = str1;

		char *str2 = (char *) malloc(32);
		strncpy(str2, "world", 32);
		config.appRoot = str2;

		config.internStrings();

		strncpy(str1, "olleh", 32);
		strncpy(str2, "dlrow", 32);
		free(str1);
		free(str2);
		ensure_equals(config.appType, P_STATIC_STRING("hello"));
		ensure_equals(config.appRoot, P_STATIC_STRING("world"));
	}

	TEST_METHOD(2) {
		set_test_name("internStrings() works when called twice");

		config.appType = "hello";
		config.appRoot = "world";
		config.internStrings();
		config.internStrings();

		ensure_equals(config.appType, P_STATIC_STRING("hello"));
		ensure_equals(config.appRoot, P_STATIC_STRING("world"));
	}

	TEST_METHOD(3) {
		set_test_name("validate() works");
		vector<StaticString> errors;
		unsigned int nErrors;

		ensure("Validation fails", !config.validate(errors));
		ensure("There are errors", !errors.empty());
		nErrors = errors.size();

		config.appRoot = "/foo";
		errors.clear();
		ensure("Validation fails again", !config.validate(errors));
		ensure_equals("There are fewer errors",
			(unsigned int) errors.size(), nErrors - 1);
	}
}
