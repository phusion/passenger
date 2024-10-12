#include <TestSupport.h>
#include <StrIntTools/StrIntUtils.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct StrIntTools_StrIntUtilsTest: public TestBase {
	};

	void testTruncate(const char* str, const char *tokens, int maxBetweenTokens, const char* expected) {
		std::stringstream sstream;
		truncateBeforeTokens(str, tokens, maxBetweenTokens, sstream);
		ensure("got [" +  sstream.str() + "], expected [" + expected + "]", sstream.str() == expected);
	}

	DEFINE_TEST_GROUP(StrIntTools_StrIntUtilsTest);

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
	}

	TEST_METHOD(4) {
		set_test_name("should ignore non-UTF characters in escapeHTML");
		char s[10];
		snprintf(s, 10, "h\xeallo"); // hêllo
		string result = escapeHTML(s);
		ensure_equals(result, "h?llo");
	}

	TEST_METHOD(5) {
		set_test_name("endsWith works");
		const char *str1 = "abcdefghijklmnopqrstuvwxyz";
		ensure(endsWith(str1, "xyz"));
		ensure(endsWith("xyz", "xyz"));
		ensure(!endsWith(str1, "zzz"));
		ensure(!endsWith("xyz", "zzz"));
	}
}
