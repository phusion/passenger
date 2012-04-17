#include "TestSupport.h"
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
		ensure(!(StaticString("barr") == "bar"));
		ensure(!(StaticString("bar") == "barr"));
		
		ensure(StaticString("") == StaticString(""));
		ensure(StaticString("foo") == StaticString("foo"));
		ensure(!(StaticString("foo") == StaticString("bar")));
		ensure(!(StaticString("barr") == StaticString("bar")));
		ensure(!(StaticString("bar") == StaticString("barr")));
		
		ensure(StaticString("") == string(""));
		ensure(StaticString("foo") == string("foo"));
		ensure(!(StaticString("foo") == string("bar")));
		ensure(!(StaticString("barr") == string("bar")));
		ensure(!(StaticString("bar") == string("barr")));
	}
	
	TEST_METHOD(2) {
		// Test != operator
		ensure(!(StaticString("") != ""));
		ensure(!(StaticString("foo") != "foo"));
		ensure(StaticString("foo") != "bar");
		ensure(StaticString("barr") != "bar");
		ensure(StaticString("bar") != "barr");
		
		ensure(!(StaticString("") != StaticString("")));
		ensure(!(StaticString("foo") != StaticString("foo")));
		ensure(StaticString("foo") != StaticString("bar"));
		ensure(StaticString("barr") != StaticString("bar"));
		ensure(StaticString("bar") != StaticString("barr"));
		
		ensure(!(StaticString("") != string("")));
		ensure(!(StaticString("foo") != string("foo")));
		ensure(StaticString("foo") != string("bar"));
		ensure(StaticString("barr") != string("bar"));
		ensure(StaticString("bar") != string("barr"));
	}
	
	TEST_METHOD(3) {
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
	
	TEST_METHOD(4) {
		// Test find(char)
		ensure_equals("Assertion 1",
			StaticString("").find('c'),
			string::npos
		);
		ensure_equals("Assertion 2",
			StaticString("hello world").find('c'),
			string::npos
		);
		ensure_equals("Assertion 3",
			StaticString("hello world").find('h'),
			(string::size_type) 0
		);
		ensure_equals("Assertion 4",
			StaticString("hello world").find('o'),
			(string::size_type) 4
		);
		
		ensure_equals("Assertion 5",
			StaticString("hello world").find('h', 1),
			string::npos
		);
		ensure_equals("Assertion 6",
			StaticString("hello world").find('o', 1),
			(string::size_type) 4
		);
		ensure_equals("Assertion 7",
			StaticString("hello world").find('o', 5),
			(string::size_type) 7
		);
		
		ensure_equals("Assertion 8",
			StaticString("hello world").find('h', 12),
			string::npos
		);
		ensure_equals("Assertion 9",
			StaticString("hello world").find('h', 20),
			string::npos
		);
	}
	
	TEST_METHOD(5) {
		// Test find(str)
		ensure_equals("Assertion 1",
			StaticString("").find(""),
			(string::size_type) 0
		);
		ensure_equals("Assertion 2",
			StaticString("").find("c"),
			string::npos
		);
		ensure_equals("Assertion 3",
			StaticString("hello world").find("c"),
			string::npos
		);
		ensure_equals("Assertion 4",
			StaticString("hello world").find("h"),
			(string::size_type) 0
		);
		ensure_equals("Assertion 5",
			StaticString("hello world").find("o"),
			(string::size_type) 4
		);
		ensure_equals("Assertion 6",
			StaticString("hello world").find("ello"),
			(string::size_type) 1
		);
		ensure_equals("Assertion 7",
			StaticString("hello world").find("world"),
			(string::size_type) 6
		);
		ensure_equals("Assertion 8",
			StaticString("hello world").find("worldd"),
			string::npos
		);
		ensure_equals("Assertion 9",
			StaticString("hello world").find(""),
			(string::size_type) 0
		);
		
		ensure_equals("Assertion 10",
			StaticString("hello world").find("h", 1),
			string::npos
		);
		ensure_equals("Assertion 11",
			StaticString("hello hello").find("ll", 1),
			(string::size_type) 2
		);
		ensure_equals("Assertion 12",
			StaticString("hello hello").find("ll", 3),
			(string::size_type) 8
		);
		
		ensure_equals("Assertion 13",
			StaticString("hello world").find("he", 12),
			string::npos
		);
		ensure_equals("Assertion 14",
			StaticString("hello world").find("he", 20),
			string::npos
		);
	}
	
	TEST_METHOD(6) {
		// Test substr()
		ensure_equals("Assertion 1",
			StaticString("hello world").substr(),
			"hello world");
		ensure_equals("Assertion 2",
			StaticString("hello world").substr(1),
			"ello world");
		ensure_equals("Assertion 3",
			StaticString("hello world").substr(4),
			"o world");
		ensure_equals("Assertion 4",
			StaticString("hello world").substr(11),
			"");
		
		try {
			StaticString("hello world").substr(12);
			fail("out_of_range expected");
		} catch (out_of_range &) {
			// Success.
		}
		
		ensure_equals("Assertion 5",
			StaticString("hello world").substr(2, 3),
			"llo");
		ensure_equals("Assertion 6",
			StaticString("hello world").substr(6, 10),
			"world");
	}
}
