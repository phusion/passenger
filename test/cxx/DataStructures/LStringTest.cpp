#include <TestSupport.h>
#include <DataStructures/LString.h>
#include <MemoryKit/palloc.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct DataStructures_LStringTest {
		LString str, str2;
		psg_pool_t *pool;

		DataStructures_LStringTest() {
			psg_lstr_init(&str);
			psg_lstr_init(&str2);
			pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		}

		~DataStructures_LStringTest() {
			psg_lstr_deinit(&str);
			psg_lstr_deinit(&str2);
			psg_destroy_pool(pool);
		}

		void reset() {
			psg_lstr_deinit(&str);
			psg_lstr_deinit(&str2);
			psg_lstr_init(&str);
			psg_lstr_init(&str2);
		}
	};

	DEFINE_TEST_GROUP(DataStructures_LStringTest);

	TEST_METHOD(1) {
		set_test_name("It is empty upon initialization");
		ensure_equals(str.size, 0u);
		ensure_equals<void *>(str.start, NULL);
		ensure_equals<void *>(str.end, NULL);
	}

	TEST_METHOD(2) {
		set_test_name("Appending updates the links and the size");

		psg_lstr_append(&str, pool, "ab");
		ensure_equals(str.size, 2u);
		ensure(str.start != NULL);
		ensure_equals<void *>(str.start, str.end);

		psg_lstr_append(&str, pool, "cde");
		ensure_equals(str.size, 5u);
		ensure(str.start != NULL);
		ensure(str.start != str.end);
	}

	TEST_METHOD(3) {
		set_test_name("Appending an empty string does nothing");
	}


	/***** Comparison with StaticString *****/

	TEST_METHOD(10) {
		set_test_name("Comparing an empty LString with an empty StaticString");
		ensure(psg_lstr_cmp(&str, ""));
	}

	TEST_METHOD(11) {
		set_test_name("Comparing an empty LString with a non-empty StaticString");
		ensure(!psg_lstr_cmp(&str, "foo"));
		ensure(!psg_lstr_cmp(&str, "bar"));
	}

	TEST_METHOD(12) {
		set_test_name("Comparing an single-part LString with an empty StaticString");
		psg_lstr_append(&str, pool, "hi");
		ensure(!psg_lstr_cmp(&str, ""));
	}

	TEST_METHOD(13) {
		set_test_name("Comparing an single-part LString with a non-empty StaticString");
		psg_lstr_append(&str, pool, "hi");
		ensure(psg_lstr_cmp(&str, "hi"));
		ensure(!psg_lstr_cmp(&str, "ho"));
	}

	TEST_METHOD(14) {
		set_test_name("Comparing an multi-part LString with an empty StaticString");
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "ho");
		ensure(!psg_lstr_cmp(&str, ""));
	}

	TEST_METHOD(15) {
		set_test_name("Comparing an multi-part LString with a non-empty StaticString");
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "ho");
		ensure(psg_lstr_cmp(&str, "hiho"));
		ensure(!psg_lstr_cmp(&str, "hiho!"));
		ensure(!psg_lstr_cmp(&str, "hihi"));
		ensure(!psg_lstr_cmp(&str, "hm"));
		ensure(!psg_lstr_cmp(&str, ""));
	}


	/***** Comparison with StaticString, with size argument *****/

	TEST_METHOD(20) {
		set_test_name("Comparing an empty LString with an empty StaticString, with size argument");
		ensure(psg_lstr_cmp(&str, "", 0));
		ensure(psg_lstr_cmp(&str, "", 1));
		ensure(psg_lstr_cmp(&str, "", 2));
	}

	TEST_METHOD(21) {
		set_test_name("Comparing an empty LString with a non-empty StaticString, with size argument");
		ensure(psg_lstr_cmp(&str, "hello", 0));
		ensure(!psg_lstr_cmp(&str, "hello", 1));
	}

	TEST_METHOD(22) {
		set_test_name("Comparing an single-part LString with an empty StaticString, with size argument");
		psg_lstr_append(&str, pool, "hi");
		ensure(psg_lstr_cmp(&str, "", 0));
		ensure(!psg_lstr_cmp(&str, "", 1));
	}

	TEST_METHOD(23) {
		set_test_name("Comparing an single-part LString with a non-empty StaticString, with size argument");
		psg_lstr_append(&str, pool, "hi");
		ensure(psg_lstr_cmp(&str, "hi!", 0));
		ensure(psg_lstr_cmp(&str, "hi!", 1));
		ensure(psg_lstr_cmp(&str, "hi!", 2));
		ensure(!psg_lstr_cmp(&str, "hi!", 3));
		ensure(!psg_lstr_cmp(&str, "ho", 2));
	}

	TEST_METHOD(24) {
		set_test_name("Comparing an multi-part LString with an empty StaticString, with size argument");
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "hoi");
		ensure(psg_lstr_cmp(&str, "", 0));
		ensure(!psg_lstr_cmp(&str, "", 1));
	}

	TEST_METHOD(25) {
		set_test_name("Comparing an multi-part LString with a non-empty StaticString, with size argument");
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "hoi");
		ensure(psg_lstr_cmp(&str, "", 0));
		ensure(psg_lstr_cmp(&str, "h", 1));
		ensure(psg_lstr_cmp(&str, "hi", 2));
		ensure(psg_lstr_cmp(&str, "hihoi", 0));
		ensure(psg_lstr_cmp(&str, "hihoi", 1));
		ensure(psg_lstr_cmp(&str, "hihoi", 2));
		ensure(psg_lstr_cmp(&str, "hihoi", 3));
		ensure(psg_lstr_cmp(&str, "hihoi", 5));
		ensure(psg_lstr_cmp(&str, "hihoi!", 5));
		ensure(!psg_lstr_cmp(&str, "hihoi!", 6));
		ensure(!psg_lstr_cmp(&str, "hihoo", 5));
		ensure(psg_lstr_cmp(&str, "hihoo", 4));
	}


	/***** Comparison with LString *****/

	TEST_METHOD(30) {
		set_test_name("Comparing an empty LString with an empty LString");
		ensure(psg_lstr_cmp(&str, &str2));
	}

	TEST_METHOD(31) {
		set_test_name("Comparing an empty LString with a one-part LString");
		psg_lstr_append(&str2, pool, "hi");
		ensure(!psg_lstr_cmp(&str, &str2));
	}

	TEST_METHOD(32) {
		set_test_name("Comparing an empty LString with a multi-part LString");
		psg_lstr_append(&str2, pool, "hi");
		psg_lstr_append(&str2, pool, "hoi");
		ensure(!psg_lstr_cmp(&str, &str2));
	}

	TEST_METHOD(33) {
		set_test_name("Comparing an one-part LString with a one-part LString");
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str2, pool, "hi");
		ensure(psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str2, pool, "ho");
		ensure(!psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str2, pool, "hii");
		ensure(!psg_lstr_cmp(&str, &str2));
	}

	TEST_METHOD(34) {
		set_test_name("Comparing an one-part LString with a multi-part LString");
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str2, pool, "h");
		psg_lstr_append(&str2, pool, "i");
		ensure(psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str2, pool, "h");
		psg_lstr_append(&str2, pool, "o");
		ensure(!psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str2, pool, "hi");
		psg_lstr_append(&str2, pool, "o");
		ensure(!psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str2, pool, "hi");
		psg_lstr_append(&str2, pool, "i");
		ensure(!psg_lstr_cmp(&str, &str2));
	}

	TEST_METHOD(35) {
		set_test_name("Comparing an multi-part LString with a multi-part LString");
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "ho!");
		psg_lstr_append(&str2, pool, "hi");
		psg_lstr_append(&str2, pool, "ho!");
		ensure(psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "ho!");
		psg_lstr_append(&str2, pool, "h");
		psg_lstr_append(&str2, pool, "iho!");
		ensure(psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "ho!");
		psg_lstr_append(&str2, pool, "h");
		psg_lstr_append(&str2, pool, "iho");
		ensure(!psg_lstr_cmp(&str, &str2));

		reset();
		psg_lstr_append(&str, pool, "hi");
		psg_lstr_append(&str, pool, "ho!");
		psg_lstr_append(&str2, pool, "h");
		psg_lstr_append(&str2, pool, "i");
		psg_lstr_append(&str2, pool, "h");
		psg_lstr_append(&str2, pool, "o!");
		ensure(psg_lstr_cmp(&str, &str2));
	}


	/***** psg_lstr_make_contiguous *****/

	TEST_METHOD(40) {
		const LString *cstr;

		psg_lstr_append(&str, pool, "hey");
		psg_lstr_append(&str, pool, "my");
		psg_lstr_append(&str, pool, "world");

		cstr = psg_lstr_make_contiguous(&str, pool);
		ensure_equals(cstr->size, strlen("heymyworld"));
		ensure_equals<void *>(cstr->start->next, NULL);
		ensure_equals(StaticString(cstr->start->data, cstr->size), "heymyworld");
	}
}
