#include <TestSupport.h>
#include <ApplicationPool2/Process.h>

using namespace Passenger;
using namespace Passenger::ApplicationPool2;
using namespace std;

namespace tut {
	struct ApplicationPool2_OptionsTest {
		ApplicationPool2_OptionsTest() {
		}
	};

	DEFINE_TEST_GROUP(ApplicationPool2_OptionsTest);

	TEST_METHOD(1) {
		// Test persist().
		char appRoot[] = "appRoot";
		char processTitle[] = "processTitle";
		char fooKey[] = "PASSENGER_FOO";
		char fooValue[] = "foo";
		char barKey[] = "PASSENGER_BAR";
		char barValue[] = "bar";

		Options options;
		options.appRoot = appRoot;
		options.processTitle = processTitle;
		options.environmentVariables.push_back(make_pair(fooKey, fooValue));
		options.environmentVariables.push_back(make_pair(barKey, barValue));

		Options options2 = options.copyAndPersist();
		appRoot[0] = processTitle[0] = 'x';
		fooKey[0]  = fooValue[0]     = 'x';
		barKey[0]  = barValue[0]     = 'x';

		ensure_equals(options2.appRoot, "appRoot");
		ensure_equals(options2.processTitle, "processTitle");
		ensure_equals(options2.environmentVariables.size(), 2u);
		ensure_equals(options2.environmentVariables[0].first, "PASSENGER_FOO");
		ensure_equals(options2.environmentVariables[0].second, "foo");
		ensure_equals(options2.environmentVariables[1].first, "PASSENGER_BAR");
		ensure_equals(options2.environmentVariables[1].second, "bar");
	}
}
