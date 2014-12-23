#include <TestSupport.h>
#include <ServerKit/CookieUtils.h>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace std;

namespace tut {
	struct ServerKit_CookieUtilsTest {
		psg_pool_t *pool;
		LString name;
		LString value;
		LString header;
		LString *result;

		ServerKit_CookieUtilsTest() {
			pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
			psg_lstr_init(&name);
			psg_lstr_append(&name, pool, "foo");
			psg_lstr_init(&value);
			psg_lstr_append(&value, pool, "bar");
			psg_lstr_init(&header);
			result = NULL;
		}

		~ServerKit_CookieUtilsTest() {
			psg_lstr_deinit(&name);
			psg_lstr_deinit(&value);
			psg_lstr_deinit(&header);
			if (result != NULL) {
				psg_lstr_deinit(result);
			}
			psg_destroy_pool(pool);
		}
	};

	DEFINE_TEST_GROUP(ServerKit_CookieUtilsTest);

	TEST_METHOD(1) {
		set_test_name("1 cookie in 1 part");
		psg_lstr_append(&header, pool, "foo=bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}


	TEST_METHOD(2) {
		set_test_name("1 cookie in multiple parts (1)");
		psg_lstr_append(&header, pool, "fo");
		psg_lstr_append(&header, pool, "o=");
		psg_lstr_append(&header, pool, "bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(3) {
		set_test_name("1 cookie in multiple parts (2)");
		psg_lstr_append(&header, pool, "foo");
		psg_lstr_append(&header, pool, "=");
		psg_lstr_append(&header, pool, "bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(4) {
		set_test_name("1 cookie in multiple parts (3)");
		psg_lstr_append(&header, pool, "foo=");
		psg_lstr_append(&header, pool, "bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(5) {
		set_test_name("1 cookie in multiple parts (4)");
		psg_lstr_append(&header, pool, "foo=b");
		psg_lstr_append(&header, pool, "ar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}


	TEST_METHOD(10) {
		set_test_name("Multiple cookies in 1 part (1)");
		psg_lstr_append(&header, pool, "foo=bar; hello=world");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(11) {
		set_test_name("Multiple cookies in 1 part (2)");
		psg_lstr_append(&header, pool, "hello=world; foo=bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(12) {
		set_test_name("Multiple cookies in 1 part (3)");
		psg_lstr_append(&header, pool, "hello=world; foo=bar; a=b");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}


	TEST_METHOD(15) {
		set_test_name("Multiple cookies in multiple parts (1)");
		psg_lstr_append(&header, pool, "fo");
		psg_lstr_append(&header, pool, "o=bar; hello=world");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(16) {
		set_test_name("Multiple cookies in multiple parts (2)");
		psg_lstr_append(&header, pool, "foo");
		psg_lstr_append(&header, pool, "=");
		psg_lstr_append(&header, pool, "bar; hello=world");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(17) {
		set_test_name("Multiple cookies in multiple parts (3)");
		psg_lstr_append(&header, pool, "foo");
		psg_lstr_append(&header, pool, "=bar; hello=world");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(18) {
		set_test_name("Multiple cookies in multiple parts (4)");
		psg_lstr_append(&header, pool, "foo=");
		psg_lstr_append(&header, pool, "bar; hello=world");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(19) {
		set_test_name("Multiple cookies in multiple parts (5)");
		psg_lstr_append(&header, pool, "foo=b");
		psg_lstr_append(&header, pool, "ar; hello=world");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}


	TEST_METHOD(20) {
		set_test_name("Multiple cookies in multiple parts (6)");
		psg_lstr_append(&header, pool, "hello=world; fo");
		psg_lstr_append(&header, pool, "o=bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(21) {
		set_test_name("Multiple cookies in multiple parts (7)");
		psg_lstr_append(&header, pool, "hello=world; foo");
		psg_lstr_append(&header, pool, "=");
		psg_lstr_append(&header, pool, "bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(22) {
		set_test_name("Multiple cookies in multiple parts (8)");
		psg_lstr_append(&header, pool, "hello=world; foo");
		psg_lstr_append(&header, pool, "=bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(23) {
		set_test_name("Multiple cookies in multiple parts (9)");
		psg_lstr_append(&header, pool, "hello=world; foo=");
		psg_lstr_append(&header, pool, "bar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(24) {
		set_test_name("Multiple cookies in multiple parts (10)");
		psg_lstr_append(&header, pool, "hello=world; foo=b");
		psg_lstr_append(&header, pool, "ar");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}


	TEST_METHOD(30) {
		set_test_name("Multiple cookies in multiple parts (11)");
		psg_lstr_append(&header, pool, "hello=world; fo");
		psg_lstr_append(&header, pool, "o=bar; a=b");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(31) {
		set_test_name("Multiple cookies in multiple parts (12)");
		psg_lstr_append(&header, pool, "hello=world; foo");
		psg_lstr_append(&header, pool, "=");
		psg_lstr_append(&header, pool, "bar; a=b");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(32) {
		set_test_name("Multiple cookies in multiple parts (13)");
		psg_lstr_append(&header, pool, "hello=world; foo");
		psg_lstr_append(&header, pool, "=bar; a=b");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(33) {
		set_test_name("Multiple cookies in multiple parts (14)");
		psg_lstr_append(&header, pool, "hello=world; foo=");
		psg_lstr_append(&header, pool, "bar; a=b");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}

	TEST_METHOD(34) {
		set_test_name("Multiple cookies in multiple parts (15)");
		psg_lstr_append(&header, pool, "hello=world; foo=b");
		psg_lstr_append(&header, pool, "ar; a=b");

		result = findCookie(pool, &header, &name);
		ensure("(1)", result != NULL);
		ensure("(2)", psg_lstr_cmp(result, &value));
	}
}
