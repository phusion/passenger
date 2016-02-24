#include <TestSupport.h>
#include <string>
#include <DataStructures/StringKeyTable.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct DataStructures_StringKeyTableTest {
		StringKeyTable<string> table;
		string *value;

		DataStructures_StringKeyTableTest() {
			value = NULL;
		}
	};

	DEFINE_TEST_GROUP(DataStructures_StringKeyTableTest);

	TEST_METHOD(1) {
		set_test_name("Initial state");
		ensure_equals(table.size(), 0u);
		ensure_equals(table.arraySize(), (unsigned int) StringKeyTable<string>::DEFAULT_SIZE);
	}

	TEST_METHOD(2) {
		set_test_name("On an empty StringKeyTable, iterators reach the end immediately");
		StringKeyTable<string>::Iterator it(table);
		ensure_equals<void *>(*it, NULL);
	}

	TEST_METHOD(3) {
		set_test_name("On an empty StringKeyTable, lookups return NULL");
		ensure(!table.lookup("hello", &value));
		ensure_equals<void *>(value, NULL);
		ensure(!table.lookup("?", &value));
		ensure_equals<void *>(value, NULL);
		ensure(!table.lookupRandom(NULL, &value));
		ensure_equals<void *>(value, NULL);
	}

	TEST_METHOD(4) {
		set_test_name("Insertions work");

		table.insert("Content-Length", "5");
		ensure_equals(table.size(), 1u);
		ensure(!table.lookup("hello", &value));
		ensure_equals<void *>("(1)", value, NULL);
		ensure(!table.lookup("Host", &value));
		ensure_equals<void *>("(2)", value, NULL);
		ensure(table.lookup("Content-Length", &value));
		ensure_equals("(3)", *value, "5");

		table.insert("Host", "foo.com");
		ensure_equals(table.size(), 2u);
		ensure(!table.lookup("hello", &value));
		ensure_equals<void *>("(5)", value, NULL);
		ensure(table.lookup("Host", &value));
		ensure_equals("(6)", *value, "foo.com");
		ensure(table.lookup("Content-Length", &value));
		ensure_equals("(7)", *value, "5");
	}

	TEST_METHOD(5) {
		set_test_name("Large amounts of insertions");

		table.insert("Host", "foo.com");
		table.insert("Content-Length", "5");
		table.insert("Accept", "text/html");
		table.insert("Accept-Encoding", "gzip");
		table.insert("Accept-Language", "nl");
		table.insert("User-Agent", "Mozilla");
		table.insert("Set-Cookie", "foo=bar");
		table.insert("Connection", "keep-alive");
		table.insert("Cache-Control", "no-cache");
		table.insert("Pragma", "no-cache");

		ensure(!table.lookup("MyHeader", &value));

		ensure(table.lookup("Host", &value));
		ensure_equals(*value, "foo.com");
		ensure(table.lookup("Content-Length", &value));
		ensure_equals(*value, "5");
		ensure(table.lookup("Accept", &value));
		ensure_equals(*value, "text/html");
		ensure(table.lookup("Accept-Encoding", &value));
		ensure_equals(*value, "gzip");
		ensure(table.lookup("Accept-Language", &value));
		ensure_equals(*value, "nl");
		ensure(table.lookup("User-Agent", &value));
		ensure_equals(*value, "Mozilla");
		ensure(table.lookup("Set-Cookie", &value));
		ensure_equals(*value, "foo=bar");
		ensure(table.lookup("Connection", &value));
		ensure_equals(*value, "keep-alive");
		ensure(table.lookup("Cache-Control", &value));
		ensure_equals(*value, "no-cache");
		ensure(table.lookup("Pragma", &value));
		ensure_equals(*value, "no-cache");
	}

	TEST_METHOD(6) {
		set_test_name("Iterators work");
		table.insert("Content-Length", "5");
		table.insert("Host", "foo.com");

		StringKeyTable<string>::Iterator it(table);
		ensure(*it != NULL);
		if (it.getKey() == "Content-Length") {
			ensure_equals(it.getKey(), "Content-Length");
			it.next();
			ensure_equals(it.getKey(), "Host");
		} else {
			ensure_equals(it.getKey(), "Host");
			it.next();
			ensure_equals(it.getKey(), "Content-Length");
		}

		it.next();
		ensure_equals<void *>(*it, NULL);
	}

	TEST_METHOD(7) {
		set_test_name("Dynamically growing the bucket on insert");
		table = StringKeyTable<string>(4);
		ensure_equals(table.size(), 0u);
		ensure_equals(table.arraySize(), 4u);

		table.insert("Host", "foo.com");
		table.insert("Content-Length", "5");
		ensure_equals(table.size(), 2u);
		ensure_equals(table.arraySize(), 4u);

		table.insert("Accept", "text/html");
		ensure_equals(table.size(), 3u);
		ensure_equals(table.arraySize(), 8u);

		ensure(!table.lookup("MyHeader", &value));
		ensure_equals<void *>(value, NULL);
		ensure(table.lookup("Host", &value));
		ensure_equals(*value, "foo.com");
		ensure(table.lookup("Content-Length", &value));
		ensure_equals(*value, "5");
		ensure(table.lookup("Accept", &value));
		ensure_equals(*value, "text/html");
	}

	TEST_METHOD(8) {
		set_test_name("Clearing");

		table.insert("Host", "foo.com");
		table.insert("Content-Length", "5");
		table.insert("Accept", "text/html");

		table.clear();
		ensure_equals(table.size(), 0u);
		ensure_equals(table.arraySize(), (unsigned int) StringKeyTable<string>::DEFAULT_SIZE);

		ensure(!table.lookup("Host", &value));
		ensure_equals<void *>(value, NULL);
		ensure(!table.lookup("Content-Length", &value));
		ensure_equals<void *>(value, NULL);
		ensure(!table.lookup("Accept", &value));
		ensure_equals<void *>(value, NULL);
	}

	TEST_METHOD(9) {
		set_test_name("lookupRandom() works");
		HashedStaticString key;

		table.insert("a", "1");
		ensure(table.lookupRandom(&key, &value));
		ensure_equals(key, "a");
		ensure_equals(*value, "1");

		table.insert("b", "2");
		ensure(table.lookupRandom(&key, &value));
		ensure_equals(key, "b");
		ensure_equals(*value, "2");

		table.insert("c", "3");
		ensure(table.lookupRandom(&key, &value));
		ensure_equals(key, "c");
		ensure_equals(*value, "3");

		ensure(table.erase("b"));
		ensure(table.lookupRandom(&key, &value));
		ensure_equals(key, "c");
		ensure_equals(*value, "3");

		ensure(table.erase("c"));
		ensure(table.lookupRandom(&key, &value));
		ensure_equals(key, "a");
		ensure_equals(*value, "1");

		ensure(table.erase("a"));
		ensure(!table.lookupRandom(&key, &value));
	}

	TEST_METHOD(10) {
		set_test_name("Initial size 0");
		StringKeyTable<string> t(0, 0);

		ensure_equals(t.lookupCopy("a"), "");
		t.insert("a", "b");
		ensure_equals(t.lookupCopy("a"), "b");
	}

	TEST_METHOD(11) {
		set_test_name("Move support");

		class Foo {
		private:
			BOOST_MOVABLE_BUT_NOT_COPYABLE(Foo)

		public:
			int value;

			Foo()
				: value(0)
				{ }

			Foo(int v)
				: value(v)
				{ }

			Foo(BOOST_RV_REF(Foo) other)
				: value(other.value)
			{
				other.value = -1;
			}

			Foo &operator=(BOOST_RV_REF(Foo) other) {
				value = other.value;
				other.value = -1;
				return *this;
			}
		};

		StringKeyTable<Foo, SKT_EnableMoveSupport> t(1);
		Foo *result;

		ensure_equals("Initial table array size is 1", t.arraySize(), 1u);

		t.insertByMoving("a", Foo(1));
		ensure("1: a is in the table", t.lookup("a", &result));
		ensure_equals("1: a's value is 1", result->value, 1);

		Foo f(2);
		t.insertByMoving("a", boost::move(f));
		ensure("2: a is in the table", t.lookup("a", &result));
		ensure_equals("2: a's value is 2", result->value, 2);
		ensure_equals("2: original variable's value is -1", f.value, -1);

		t.insertByMoving("b", Foo(3));
		ensure_equals("3: New table array size is 2", t.arraySize(), 4u);
		ensure("3: a is in the table", t.lookup("a", &result));
		ensure_equals("3: a's value is 2", result->value, 2);
		ensure("3: a is in the table", t.lookup("b", &result));
		ensure_equals("3: b's value is 3", result->value, 3);
	}
}
