#include "tut.h"
#include "Utils.cpp"
#include <unistd.h>
#include <limits.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct UtilsTest {
		vector<string> output;
		string oldPath;
		
		UtilsTest() {
			oldPath = getenv("PATH");
		}
		
		~UtilsTest() {
			setenv("PATH", oldPath.c_str(), 1);
		}
	};

	DEFINE_TEST_GROUP(UtilsTest);

	/***** Test split() *****/

	TEST_METHOD(1) {
		split("", ':', output);
		ensure_equals(output.size(), 1u);
		ensure_equals(output[0], "");
	}
	
	TEST_METHOD(2) {
		split("hello world", ':', output);
		ensure_equals(output.size(), 1u);
		ensure_equals(output[0], "hello world");
	}
	
	TEST_METHOD(3) {
		split("hello world:foo bar", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "hello world");
		ensure_equals(output[1], "foo bar");
	}
	
	TEST_METHOD(4) {
		split("hello world:", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "hello world");
		ensure_equals(output[1], "");
	}
	
	TEST_METHOD(5) {
		split(":hello world", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "");
		ensure_equals(output[1], "hello world");
	}
	
	TEST_METHOD(6) {
		split("abc:def::ghi", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc");
		ensure_equals(output[1], "def");
		ensure_equals(output[2], "");
		ensure_equals(output[3], "ghi");
	}
	
	TEST_METHOD(7) {
		split("abc:::def", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc");
		ensure_equals(output[1], "");
		ensure_equals(output[2], "");
		ensure_equals(output[3], "def");
	}
	
	
	/**** Test findSpawnServer() ****/
	
	TEST_METHOD(8) {
		// If $PATH is empty, it should not find anything.
		setenv("PATH", "", 1);
		ensure_equals(findSpawnServer(), "");
	}
	
	TEST_METHOD(9) {
		// It should ignore relative paths.
		setenv("PATH", "../bin", 1);
		ensure_equals(findSpawnServer(), "");
	}
	
	TEST_METHOD(10) {
		char cwd[PATH_MAX];
		string binpath(getcwd(cwd, sizeof(cwd)));
		binpath.append("/../bin");
		setenv("PATH", binpath.c_str(), 1);
		ensure("Spawn server is found.", !findSpawnServer().empty());
	}
}
