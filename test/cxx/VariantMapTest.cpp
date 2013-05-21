#include "TestSupport.h"
#include "Utils/VariantMap.h"

using namespace Passenger;

namespace tut {
	struct VariantMapTest {
		VariantMap map;
	};
	
	DEFINE_TEST_GROUP(VariantMapTest);
	
	TEST_METHOD(1) {
		// Test empty map.
		ensure_equals(map.size(), 0u);
		ensure(!map.has("hello"));
		ensure(!map.has("foo"));
	}
	
	TEST_METHOD(2) {
		// Test setting and getting string values.
		map.set("hello", "world");
		map.set("abcd", "efgh");
		map.set("", "bar");
		ensure_equals("(1)", map.get("hello"), "world");
		ensure_equals("(2)", map.get("abcd"), "efgh");
		ensure_equals("(3)", map.get(""), "bar");
		ensure_equals("(4)", map.size(), 3u);
		ensure("(5)", map.has("hello"));
		ensure("(6)", map.has("abcd"));
		ensure("(7)", map.has(""));
		ensure("(8)", !map.has("xyz"));
	}
	
	TEST_METHOD(3) {
		// Test setting and getting non-string values.
		map.set("str", "1234");
		map.setInt("int", 5678);
		map.setULL("ull", 18446744073709551615ull);
		map.setPid("pid", 47326);
		map.setUid("uid", (uid_t) 500);
		map.setGid("gid", (gid_t) 510);
		map.setUid("negative_uid", (uid_t) -1);
		map.setGid("negative_gid", (gid_t) -2);
		map.setBool("true", true);
		map.setBool("false", false);
		ensure_equals(map.size(), 10u);
		ensure(map.has("str"));
		ensure(map.has("int"));
		ensure(map.has("ull"));
		ensure(map.has("pid"));
		ensure(map.has("uid"));
		ensure(map.has("gid"));
		ensure(map.has("negative_uid"));
		ensure(map.has("negative_gid"));
		ensure(map.has("true"));
		ensure(map.has("false"));
		ensure(!map.has("foo"));
		
		ensure_equals(map.get("str"), "1234");
		ensure_equals(map.get("int"), "5678");
		ensure_equals(map.get("ull"), "18446744073709551615");
		ensure_equals(map.get("pid"), "47326");
		ensure_equals(map.get("uid"), "500");
		ensure_equals(map.get("gid"), "510");
		// No idea how negative_uid and negative_gid are casted to string;
		// depends on whether the platform defines them as signed or unsigned.
		ensure_equals(map.get("true"), "true");
		ensure_equals(map.get("false"), "false");
		
		ensure_equals(map.getInt("str"), 1234);
		ensure_equals(map.getInt("int"), 5678);
		ensure_equals(map.getInt("pid"), 47326);
		ensure_equals(map.getInt("uid"), 500);
		ensure_equals(map.getInt("gid"), 510);
		if (sizeof(uid_t) == sizeof(int)) {
			ensure_equals(map.getInt("negative_uid"), -1);
		}
		if (sizeof(gid_t) == sizeof(int)) {
			ensure_equals(map.getInt("negative_gid"), -2);
		}
		
		ensure_equals(map.getULL("ull"), 18446744073709551615ull);
		ensure_equals(map.getPid("pid"), (pid_t) 47326);
		ensure_equals(map.getUid("uid"), (uid_t) 500);
		ensure_equals(map.getGid("gid"), (gid_t) 510);
		ensure_equals(map.getUid("negative_uid"), (uid_t) -1);
		ensure_equals(map.getGid("negative_gid"), (gid_t) -2);
		ensure_equals(map.getBool("true"), true);
		ensure_equals(map.getBool("false"), false);
	}
	
	TEST_METHOD(4) {
		// get() throws MissingKeyException if the key doesn't
		// exist and 'required' is true (which it is by default).
		try {
			map.get("str");
			fail("MissingKeyException expected (str)");
		} catch (const VariantMap::MissingKeyException &e) {
			ensure_equals(e.getKey(), "str");
		}
		
		try {
			map.getInt("int");
			fail("MissingKeyException expected (int)");
		} catch (const VariantMap::MissingKeyException &e) {
			ensure_equals(e.getKey(), "int");
		}
		
		try {
			map.getULL("ull");
			fail("MissingKeyException expected (ull)");
		} catch (const VariantMap::MissingKeyException &e) {
			ensure_equals(e.getKey(), "ull");
		}
		
		try {
			map.getPid("pid");
			fail("MissingKeyException expected (pid)");
		} catch (const VariantMap::MissingKeyException &e) {
			ensure_equals(e.getKey(), "pid");
		}
		
		try {
			map.getUid("uid");
			fail("MissingKeyException expected (uid)");
		} catch (const VariantMap::MissingKeyException &e) {
			ensure_equals(e.getKey(), "uid");
		}
		
		try {
			map.getGid("gid");
			fail("MissingKeyException expected (gid)");
		} catch (const VariantMap::MissingKeyException &e) {
			ensure_equals(e.getKey(), "gid");
		}
		
		try {
			map.getBool("bool");
			fail("MissingKeyException expected (bool)");
		} catch (const VariantMap::MissingKeyException &e) {
			ensure_equals(e.getKey(), "bool");
		}
	}
	
	TEST_METHOD(5) {
		// get() returns the default value if 'required' is false.
		ensure_equals(map.get("foo", false, "1234"), "1234");
		ensure_equals(map.getInt("foo", false, 1234), 1234);
		ensure_equals(map.getULL("foo", false, 18446744073709551615ull), 18446744073709551615ull);
		ensure_equals(map.getPid("foo", false, 1234), (pid_t) 1234);
		ensure_equals(map.getUid("foo", false, 1234), (gid_t) 1234);
		ensure_equals(map.getGid("foo", false, 1234), (uid_t) 1234);
		ensure_equals(map.getBool("foo", false, true), true);
		ensure_equals(map.getBool("foo", false, false), false);
	}
	
	TEST_METHOD(6) {
		// Test populating from array.
		const char *ary[] = {
			"foo", "1234",
			"bar", "5678"
		};
		
		try {
			map.readFrom(ary, 3);
			fail("ArgumentException expected");
		} catch (const ArgumentException &) {
			// Pass.
		}
		
		map.readFrom(ary, 4);
		ensure_equals(map.get("foo"), "1234");
		ensure_equals(map.get("bar"), "5678");
	}

	TEST_METHOD(7) {
		// Setting an empty value result in the deletion of the key.
		map.set("a", "a");
		map.set("b", "b");
		map.set("b", "");
		try {
			map.get("b");
			fail("MissingKeyException expected");
		} catch (const VariantMap::MissingKeyException &e) {
			// Pass.
		}
		ensure(!map.has("foo"));
		ensure_equals(map.size(), 1u);
	}
}
