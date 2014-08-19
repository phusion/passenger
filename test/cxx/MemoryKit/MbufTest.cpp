#include <TestSupport.h>
#include <boost/move/move.hpp>
#include <Constants.h>
#include <MemoryKit/mbuf.h>

using namespace Passenger;
using namespace Passenger::MemoryKit;
using namespace std;

namespace tut {
	struct MemoryKit_MbufTest {
		struct mbuf_pool pool;

		MemoryKit_MbufTest() {
			pool.mbuf_block_chunk_size = DEFAULT_MBUF_CHUNK_SIZE;
			mbuf_pool_init(&pool);
		}

		~MemoryKit_MbufTest() {
			mbuf_pool_deinit(&pool);
		}
	};

	DEFINE_TEST_GROUP(MemoryKit_MbufTest);

	TEST_METHOD(1) {
		set_test_name("Initial pool state");
		ensure_equals(pool.nfree_mbuf_blockq, 0u);
		ensure_equals(pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(2) {
		set_test_name("mbuf_block_get() and mbuf_block_put()");

		struct mbuf_block *block = mbuf_block_get(&pool);
		ensure_equals("(1)", block->refcount, 1u);
		ensure_equals("(2)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(3)", pool.nactive_mbuf_blockq, 1u);

		struct mbuf_block *block2 = mbuf_block_get(&pool);
		ensure_equals("(4)", block->refcount, 1u);
		ensure_equals("(5)", block2->refcount, 1u);
		ensure_equals("(6)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(7)", pool.nactive_mbuf_blockq, 2u);

		block->refcount = 0;
		mbuf_block_put(block);
		ensure_equals("(8)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(9)", pool.nactive_mbuf_blockq, 1u);

		block2->refcount = 0;
		mbuf_block_put(block2);
		ensure_equals("(10)", pool.nfree_mbuf_blockq, 2u);
		ensure_equals("(11)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(3) {
		set_test_name("mbuf_block reference counting");
		struct mbuf_block *block = mbuf_block_get(&pool);

		mbuf_block_ref(block);
		ensure_equals("(1)", block->refcount, 2u);
		ensure_equals("(2)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(3)", pool.nactive_mbuf_blockq, 1u);

		mbuf_block_unref(block);
		ensure_equals("(4)", block->refcount, 1u);
		ensure_equals("(5)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(6)", pool.nactive_mbuf_blockq, 1u);

		mbuf_block_unref(block);
		ensure_equals("(7)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(8)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(4) {
		set_test_name("mbuf_block freelist reuse");
		struct mbuf_block *block = mbuf_block_get(&pool);
		struct mbuf_block *block2 = mbuf_block_get(&pool);
		mbuf_block_unref(block);
		block = mbuf_block_get(&pool);

		ensure_equals("(1)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(2)", pool.nactive_mbuf_blockq, 2u);
		mbuf_block_unref(block);
		mbuf_block_unref(block2);
	}

	TEST_METHOD(5) {
		set_test_name("mbuf class");
		mbuf buffer(mbuf_get(&pool));
		ensure_equals("(1)", buffer.mbuf_block->refcount, 1u);
		ensure_equals("(2)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(3)", pool.nactive_mbuf_blockq, 1u);

		buffer = mbuf();
		ensure_equals("(2)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(3)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(6) {
		set_test_name("mbuf class copy constructor");
		mbuf buffer(mbuf_get(&pool));

		{
			mbuf buffer2(buffer);
			ensure_equals("(1)", buffer.mbuf_block, buffer2.mbuf_block);
			ensure_equals("(2)", buffer.mbuf_block->refcount, 2u);
			ensure_equals("(3)", pool.nfree_mbuf_blockq, 0u);
			ensure_equals("(4)", pool.nactive_mbuf_blockq, 1u);
		}

		ensure_equals("(5)", buffer.mbuf_block->refcount, 1u);
		ensure_equals("(6)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(7)", pool.nactive_mbuf_blockq, 1u);

		buffer = mbuf();
		ensure_equals("(8)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(9)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(7) {
		set_test_name("mbuf class move constructor");
		mbuf buffer(mbuf_get(&pool));

		{
			mbuf buffer2(boost::move(buffer));
			ensure_equals<void *>("(1)", buffer.mbuf_block, NULL);
			ensure_equals<void *>("(2)", buffer.start, NULL);
			ensure_equals<void *>("(3)", buffer.end, NULL);
			ensure_equals("(4)", buffer2.mbuf_block->refcount, 1u);
			ensure_equals("(5)", pool.nfree_mbuf_blockq, 0u);
			ensure_equals("(6)", pool.nactive_mbuf_blockq, 1u);
		}

		ensure_equals("(8)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(9)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(8) {
		set_test_name("mbuf class copy assignment");
		mbuf buffer(mbuf_get(&pool));

		{
			mbuf buffer2;
			buffer2 = buffer;
			ensure_equals("(1)", buffer.mbuf_block, buffer2.mbuf_block);
			ensure_equals("(2)", buffer.mbuf_block->refcount, 2u);
			ensure_equals("(3)", pool.nfree_mbuf_blockq, 0u);
			ensure_equals("(4)", pool.nactive_mbuf_blockq, 1u);
		}

		ensure_equals("(5)", buffer.mbuf_block->refcount, 1u);
		ensure_equals("(6)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(7)", pool.nactive_mbuf_blockq, 1u);

		buffer = mbuf();
		ensure_equals("(8)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(9)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(9) {
		set_test_name("mbuf class move assignment");
		mbuf buffer(mbuf_get(&pool));

		{
			mbuf buffer2;
			buffer2 = boost::move(buffer);
			ensure_equals<void *>("(1)", buffer.mbuf_block, NULL);
			ensure_equals<void *>("(2)", buffer.start, NULL);
			ensure_equals<void *>("(3)", buffer.end, NULL);
			ensure_equals("(4)", buffer2.mbuf_block->refcount, 1u);
			ensure_equals("(5)", pool.nfree_mbuf_blockq, 0u);
			ensure_equals("(6)", pool.nactive_mbuf_blockq, 1u);
		}

		ensure_equals("(8)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(9)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(10) {
		set_test_name("mbuf class slicing");
		mbuf buffer(mbuf_get(&pool));

		{
			mbuf buffer2(buffer, 1, 2);
			ensure_equals("(1)", buffer.mbuf_block, buffer2.mbuf_block);
			ensure_equals("(2)", buffer.mbuf_block->refcount, 2u);
			ensure_equals("(3)", buffer2.start, buffer.start + 1);
			ensure_equals("(4)", buffer2.end, buffer.start + 3);
			ensure_equals("(5)", pool.nfree_mbuf_blockq, 0u);
			ensure_equals("(6)", pool.nactive_mbuf_blockq, 1u);
		}

		ensure_equals("(7)", buffer.mbuf_block->refcount, 1u);
		ensure_equals("(8)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(9)", pool.nactive_mbuf_blockq, 1u);

		buffer = mbuf();
		ensure_equals("(10)", pool.nfree_mbuf_blockq, 1u);
		ensure_equals("(11)", pool.nactive_mbuf_blockq, 0u);
	}

	TEST_METHOD(11) {
		set_test_name("mbuf class freelist reuse");
		mbuf buffer(mbuf_get(&pool));
		mbuf buffer2(mbuf_get(&pool));
		buffer = mbuf();
		buffer = mbuf_get(&pool);
		ensure_equals("(1)", pool.nfree_mbuf_blockq, 0u);
		ensure_equals("(2)", pool.nactive_mbuf_blockq, 2u);
	}
}
