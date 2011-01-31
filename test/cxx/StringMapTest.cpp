#include "TestSupport.h"
#include "Utils/StringMap.h"
#include <string>

using namespace Passenger;
using namespace std;

namespace tut {
	struct StringMapTest {
	};
	
	DEFINE_TEST_GROUP(StringMapTest);
	
	TEST_METHOD(1) {
		// get()ing a nonexistant key returns the default value.
		StringMap<string> m;
		ensure_equals(m.get("hello"), "");
	}
	
	TEST_METHOD(2) {
		// set() works.
		StringMap<string> m;
		m.set("hello", "world");
		m.set("foo", "bar");
		ensure_equals(m.get("hello"), "world");
		ensure_equals(m.get("foo"), "bar");
		ensure_equals(m.get("something"), "");
	}
	
	TEST_METHOD(3) {
		// set() overwrites old value.
		StringMap<string> m;
		m.set("hello", "world");
		m.set("foo", "bar");
		m.set("hello", "new world");
		ensure_equals(m.get("hello"), "new world");
		ensure_equals(m.get("foo"), "bar");
	}
	
	TEST_METHOD(4) {
		// The key is interned so changing the original has no effect.
		StringMap<string> m;
		char key1[] = "hello";
		char key2[] = "world";
		
		m.set(key1, "xxx");
		m.set(key2, "yyy");
		
		key1[4] = 'p';
		strcpy(key2, "zzzzz");
		
		ensure_equals(m.get("hello"), "xxx");
		ensure_equals(m.get("hellp"), "");
		
		ensure_equals(m.get("world"), "yyy");
		ensure_equals(m.get("zzzzz"), "");
	}
	
	TEST_METHOD(5) {
		// remove() works.
		StringMap<string> m;
		m.set("hello", "world");
		m.set("foo", "bar");
		
		ensure(m.remove("hello"));
		ensure_equals(m.get("hello"), "");
		ensure_equals(m.get("foo"), "bar");
		ensure(!m.remove("hello"));
	}
}
