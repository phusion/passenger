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

		Options options;
		options.appRoot = appRoot;
		options.processTitle = processTitle;

		Options options2 = options.copyAndPersist();
		appRoot[0] = processTitle[0] = 'x';
		ensure_equals(options2.appRoot, "appRoot");
		ensure_equals(options2.processTitle, "processTitle");
	}
}
