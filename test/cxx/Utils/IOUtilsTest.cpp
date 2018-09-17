#include <TestSupport.h>
#include <Utils.h>
#include <Utils/IOUtils.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct Utils_IOUtilsTest: public TestBase {
	};

	DEFINE_TEST_GROUP(Utils_IOUtilsTest);

	/***** Test readAll() *****/

	TEST_METHOD(1) {
		set_test_name("readAll() with unlimited maxSize");
		Pipe p = createPipe(__FILE__, __LINE__);
		writeExact(p[1], "hello world");
		p[1].close();
		pair<string, bool> result = readAll(p[0],
			std::numeric_limits<size_t>::max());
		ensure_equals(result.first, "hello world");
		ensure(result.second);
	}

	TEST_METHOD(2) {
		set_test_name("readAll() with size smaller than actual data");
		Pipe p = createPipe(__FILE__, __LINE__);
		writeExact(p[1], "hello world");
		p[1].close();
		pair<string, bool> result = readAll(p[0], 5);
		ensure_equals(result.first, "hello");
		ensure(!result.second);
	}
}
