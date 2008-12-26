#include "tut.h"
#include "StaticString.h"

using namespace Passenger;
using namespace std;

namespace tut {
	struct StaticStringTest {
	};
	
	DEFINE_TEST_GROUP(StaticStringTest);

	TEST_METHOD(1) {
		// Test == operator.
		ensure(StaticString("") == "");
		ensure(StaticString("foo") == "foo");
		ensure(!(StaticString("foo") == "bar"));
	}
	
	TEST_METHOD(2) {
		// Test < operator.
		ensure_equals("Assertion 1",
			StaticString("") < "",
			string("") < string("")
		);
		ensure_equals("Assertion 2",
			StaticString("abc") < "abc",
			string("abc") < string("abc")
		);
		ensure_equals("Assertion 3",
			StaticString("foo") < "bar",
			string("foo") < string("bar")
		);
		ensure_equals("Assertion 4",
			StaticString("foo") < "bar!",
			string("foo") < string("bar!")
		);
		ensure_equals("Assertion 5",
			StaticString("bar!") < "foo",
			string("bar!") < string("foo")
		);
		ensure_equals("Assertion 6",
			StaticString("hello") < "hello world",
			string("hello") < string("hello world")
		);
		ensure_equals("Assertion 7",
			StaticString("hello world") < "hello",
			string("hello world") < string("hello")
		);
	}
}
