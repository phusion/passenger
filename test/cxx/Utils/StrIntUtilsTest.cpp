#include <TestSupport.h>
#include <Utils/StrIntUtils.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct StrIntUtilsTest {
	};

	DEFINE_TEST_GROUP(StrIntUtilsTest);


	/***** Test truncateBeforeTokens() *****/

	void testTruncate(const char* str, const char *tokens, int maxBetweenTokens, const char* expected) {
		std::stringstream sstream;
		truncateBeforeTokens(str, tokens, maxBetweenTokens, sstream);
		ensure_equals(sstream.str(), expected);
	}

	TEST_METHOD(1) {
		set_test_name("no change should occur");
		testTruncate("", "", 0, "");
		testTruncate("testwithout/tokens", "", 2, "testwithout/tokens");
		testTruncate("", "/", 2, "");
		testTruncate("/", "", 2, "/");
		testTruncate("/", "/", 2, "/");
		testTruncate("hello", "/", 2, "hello");
		testTruncate("/hello", "/", 3, "/hello");
	}

	TEST_METHOD(2) {
		set_test_name("truncation must not touch begin/end token");
		testTruncate("hello/", "/", 3, "hel/");
		testTruncate("/hello/", "/", 3, "/hel/");
	}

	TEST_METHOD(3) {
		set_test_name("exact truncation and multiple split tokens");
		testTruncate("hello/world/Main.cpp", "/", 2, "he/wo/Main.cpp");
		testTruncate("hello/world\\Main.cpp", "/\\", 1, "h/w\\Main.cpp");
		testTruncate("hello/world\\Main.cpp", "/", 1, "h/world\\Main.cpp");
		testTruncate("/he/llo/worl/", "/", 3, "/he/llo/wor/");
	} TEST_METHOD(4) {
		set_test_name("should ignore non-UTF characters in escapeHTML");
		char s[10];
		snprintf(s, 10, "h\xeallo"); // hêllo
		string result = escapeHTML(s);
		ensure_equals(result, "h?llo");
	}

	/***** Test roundUpD() *****/

	TEST_METHOD(5) {
		set_test_name("roundUpD");
		ensure_equals("(1)", roundUpD(0, 5), 0);
		ensure_equals("(2)", roundUpD(0.5, 5), 5.0);
		ensure_equals("(3)", roundUpD(1, 5), 5.0);
		ensure_equals("(4)", roundUpD(4, 5), 5.0);
		ensure_equals("(5)", roundUpD(4.5, 5), 5.0);
		ensure_equals("(6)", roundUpD(5, 5), 5.0);
		ensure_equals("(7)", roundUpD(6, 5), 10.0);
		ensure_equals("(8)", roundUpD(6.5, 5), 10.0);
		ensure_equals("(9)", roundUpD(7, 5), 10.0);
		ensure_equals("(10)", roundUpD(9, 5), 10.0);
		ensure_equals("(11)", roundUpD(9.5, 5), 10.0);
		ensure_equals("(12)", roundUpD(10, 5), 10.0);
	}
}
