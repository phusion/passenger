#include <TestSupport.h>
#include <ServerKit/HeaderTable.h>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace std;

namespace tut {
	struct ServerKit_HeaderTableTest {
		psg_pool_t *pool;
		HeaderTable table;

		ServerKit_HeaderTableTest() {
			pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		}

		~ServerKit_HeaderTableTest() {
			psg_destroy_pool(pool);
		}

		Header *createHeader(const HashedStaticString &key, const StaticString &val) {
			Header *header = (Header *) psg_palloc(pool, sizeof(Header));
			psg_lstr_init(&header->key);
			psg_lstr_init(&header->val);
			psg_lstr_append(&header->key, pool, key.data(), key.size());
			psg_lstr_append(&header->val, pool, val.data(), val.size());
			header->hash = key.hash();
			return header;
		}

		void insertHeader(Header *header, psg_pool_t *pool) {
			table.insert(&header, pool);
		}
	};

	DEFINE_TEST_GROUP(ServerKit_HeaderTableTest);

	TEST_METHOD(1) {
		set_test_name("Initial state");
		ensure_equals(table.size(), 0u);
		ensure_equals(table.arraySize(), (unsigned int) HeaderTable::DEFAULT_SIZE);
	}

	TEST_METHOD(2) {
		set_test_name("On an empty HeaderTable, iterators reach the end immediately");
		HeaderTable::Iterator it(table);
		ensure_equals<void *>(*it, NULL);
	}

	TEST_METHOD(3) {
		set_test_name("On an empty HeaderTable, lookups return NULL");
		ensure_equals<void *>(table.lookup("hello"), NULL);
		ensure_equals<void *>(table.lookup("?"), NULL);
	}

	TEST_METHOD(4) {
		set_test_name("Insertions work");
		Header *header = createHeader("Content-Length", "5");
		Header *header2 = createHeader("Host", "foo.com");

		insertHeader(header, pool);
		ensure_equals(table.size(), 1u);
		ensure_equals<void *>("(1)", table.lookup("hello"), NULL);
		ensure_equals<void *>("(2)", table.lookup("Host"), NULL);
		ensure("(3)", table.lookup("Content-Length") != NULL);
		ensure("(4)", psg_lstr_cmp(table.lookup("Content-Length"), "5"));

		insertHeader(header2, pool);
		ensure_equals(table.size(), 2u);
		ensure_equals<void *>("(5)", table.lookup("hello"), NULL);
		ensure("(6)", table.lookup("Host") != NULL);
		ensure("(7)", table.lookup("Content-Length") != NULL);
		ensure("(8)", psg_lstr_cmp(table.lookup("Host"), "foo.com"));
		ensure("(9)", psg_lstr_cmp(table.lookup("Content-Length"), "5"));
	}

	TEST_METHOD(5) {
		set_test_name("Large amounts of insertions");

		insertHeader(createHeader("Host", "foo.com"), pool);
		insertHeader(createHeader("Content-Length", "5"), pool);
		insertHeader(createHeader("Accept", "text/html"), pool);
		insertHeader(createHeader("Accept-Encoding", "gzip"), pool);
		insertHeader(createHeader("Accept-Language", "nl"), pool);
		insertHeader(createHeader("User-Agent", "Mozilla"), pool);
		insertHeader(createHeader("Set-Cookie", "foo=bar"), pool);
		insertHeader(createHeader("Connection", "keep-alive"), pool);
		insertHeader(createHeader("Cache-Control", "no-cache"), pool);
		insertHeader(createHeader("Pragma", "no-cache"), pool);

		ensure_equals<void *>(table.lookup("MyHeader"), NULL);
		ensure(psg_lstr_cmp(table.lookup("Host"), "foo.com"));
		ensure(psg_lstr_cmp(table.lookup("Content-Length"), "5"));
		ensure(psg_lstr_cmp(table.lookup("Accept"), "text/html"));
		ensure(psg_lstr_cmp(table.lookup("Accept-Encoding"), "gzip"));
		ensure(psg_lstr_cmp(table.lookup("Accept-Language"), "nl"));
		ensure(psg_lstr_cmp(table.lookup("User-Agent"), "Mozilla"));
		ensure(psg_lstr_cmp(table.lookup("Set-Cookie"), "foo=bar"));
		ensure(psg_lstr_cmp(table.lookup("Connection"), "keep-alive"));
		ensure(psg_lstr_cmp(table.lookup("Cache-Control"), "no-cache"));
		ensure(psg_lstr_cmp(table.lookup("Pragma"), "no-cache"));
	}

	TEST_METHOD(6) {
		set_test_name("Iterators work");
		Header *header = createHeader("Content-Length", "5");
		Header *header2 = createHeader("Host", "foo.com");
		insertHeader(header, pool);
		insertHeader(header2, pool);

		HeaderTable::Iterator it(table);
		ensure(*it != NULL);
		if (psg_lstr_cmp(&it->header->key, "Content-Length")) {
			it.next();
			ensure(psg_lstr_cmp(&it->header->key, "Host"));
		} else {
			ensure(psg_lstr_cmp(&it->header->key, "Host"));
			it.next();
			ensure(psg_lstr_cmp(&it->header->key, "Content-Length"));
		}

		it.next();
		ensure_equals<void *>(*it, NULL);
	}

	TEST_METHOD(7) {
		set_test_name("Dynamically growing the bucket on insert");
		table = HeaderTable(4);
		ensure_equals(table.size(), 0u);
		ensure_equals(table.arraySize(), 4u);

		insertHeader(createHeader("Host", "foo.com"), pool);
		insertHeader(createHeader("Content-Length", "5"), pool);
		ensure_equals(table.size(), 2u);
		ensure_equals(table.arraySize(), 4u);

		insertHeader(createHeader("Accept", "text/html"), pool);
		ensure_equals(table.size(), 3u);
		ensure_equals(table.arraySize(), 8u);

		ensure_equals<void *>(table.lookup("MyHeader"), NULL);
		ensure(psg_lstr_cmp(table.lookup("Host"), "foo.com"));
		ensure(psg_lstr_cmp(table.lookup("Content-Length"), "5"));
		ensure(psg_lstr_cmp(table.lookup("Accept"), "text/html"));
	}

	TEST_METHOD(8) {
		set_test_name("Clearing");

		insertHeader(createHeader("Host", "foo.com"), pool);
		insertHeader(createHeader("Content-Length", "5"), pool);
		insertHeader(createHeader("Accept", "text/html"), pool);

		table.clear();
		ensure_equals(table.size(), 0u);
		ensure_equals(table.arraySize(), (unsigned int) HeaderTable::DEFAULT_SIZE);

		ensure_equals<void *>(table.lookup("Host"), NULL);
		ensure_equals<void *>(table.lookup("Content-Length"), NULL);
		ensure_equals<void *>(table.lookup("Accept"), NULL);
	}

	TEST_METHOD(9) {
		set_test_name("Duplicate header merging");

		insertHeader(createHeader("X-Forwarded-For", "foo.com"), pool);
		insertHeader(createHeader("X-Forwarded-For", "bar.com"), pool);
		insertHeader(createHeader("Cache-Control", "must-invalidate"), pool);
		insertHeader(createHeader("Cache-Control", "private"), pool);
		insertHeader(createHeader("cookie", "a"), pool);
		insertHeader(createHeader("cookie", "b"), pool);
		insertHeader(createHeader("set-cookie", "c=123"), pool);
		insertHeader(createHeader("set-cookie", "d=456"), pool);

		ensure_equals(table.size(), 4u);
		ensure("(1)", psg_lstr_cmp(table.lookup("X-Forwarded-For"), "foo.com,bar.com"));
		ensure("(2)", psg_lstr_cmp(table.lookup("Cache-Control"), "must-invalidate,private"));
		ensure("(3)", psg_lstr_cmp(table.lookup("cookie"), "a;b"));
		ensure("(4)", psg_lstr_cmp(table.lookup("set-cookie"), "c=123\nd=456"));
	}
}
