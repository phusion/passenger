/*
 * twemproxy - A fast and lightweight proxy for memcached protocol.
 * Copyright (C) 2011 Twitter, Inc.
 * Copyright (C) 2014-2017 Phusion Holding B.V.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _PSG_MBUF_BLOCK_H_
#define _PSG_MBUF_BLOCK_H_

#include <psg_sysqueue.h>
#include <algorithm>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <oxt/macros.hpp>
#include <boost/cstdint.hpp>
#include <boost/move/core.hpp>

/** A memory buffer allocator system taken from twemproxy and modified to
 * suit our needs.
 *
 * In twemproxy, mbufs enables zero-copy because the same buffer on which a
 * request was received from the client is used for forwarding it to the
 * server. Similarly the same mbuf_block on which a response was received from
 * the server is used for forwarding it to the client.
 *
 * Furthermore, memory for mbufs is managed using a reuse pool. This means
 * that once mbuf_block is allocated, it is not deallocated, but just put back
 * into the reuse pool. By default each mbuf_block chunk is set to 16K bytes in
 * size. There is a trade-off between the mbuf_block size and number of concurrent
 * connections twemproxy can support. A large mbuf_block size reduces the number
 * of read syscalls made by twemproxy when reading requests or responses.
 * However, with large mbuf_block size, every active connection would use up 16K
 * bytes of buffer which might be an issue when twemproxy is handling large
 * number of concurrent connections from clients. When twemproxy is meant
 * to handle a large number of concurrent client connections, you should
 * set chunk size to a small value like 512 bytes.
 *
 * In Phusion Passenger, we modified the mbuf system so that we can take subsets
 * of an mbuf. The actual mbuf (now called mbuf_block in this modification) is
 * not actually put back on the freelist until all subsets are destroyed too.
 * This approach is similar to how Node.js manages buffer slices.
 * We also got rid of the global variables, and put them in an mbuf_pool
 * struct, which acts like a context structure.
 */

//#define MBUF_ENABLE_DEBUGGING
//#define MBUF_ENABLE_BACKTRACES

namespace Passenger {
namespace MemoryKit {

using namespace std;
using namespace boost;


struct mbuf_block;
struct mhdr;

typedef void (*mbuf_block_copy_t)(struct mbuf_block *, void *);

/* See _mbuf_block_init() for format description */
struct mbuf_block {
	boost::uint32_t    magic;     /* mbuf_block magic (const) */
	STAILQ_ENTRY(struct mbuf_block) next;         /* next free mbuf_block */
	#ifdef MBUF_ENABLE_DEBUGGING
		TAILQ_ENTRY(struct mbuf_block) active_q;  /* prev and next active mbuf_block */
	#endif
	#ifdef MBUF_ENABLE_BACKTRACES
		char *backtrace;
	#endif
	char              *start;     /* start of buffer (const) */
	char              *end;       /* end of buffer (const) */
	struct mbuf_pool  *pool;      /* containing pool (const) */
	boost::uint32_t    refcount;  /* number of references by mbuf subsets */
	boost::uint32_t    offset;    /* standalone mbuf_block data size */
};

STAILQ_HEAD(mhdr, struct mbuf_block);
#ifdef MBUF_ENABLE_DEBUGGING
	TAILQ_HEAD(active_mbuf_block_list, struct mbuf_block);
#endif

struct mbuf_pool {
	boost::uint32_t nfree_mbuf_blockq;   /* # free mbuf_block */
	boost::uint32_t nactive_mbuf_blockq; /* # active (non-free) mbuf_block */
	struct mhdr free_mbuf_blockq; /* free mbuf_block q */
	#ifdef MBUF_ENABLE_DEBUGGING
		struct active_mbuf_block_list active_mbuf_blockq; /* active mbuf_block q */
	#endif

	size_t mbuf_block_chunk_size; /* mbuf_block chunk size - header + data (const) */
	size_t mbuf_block_offset;     /* mbuf_block offset in chunk (const) */
};

#define MBUF_BLOCK_MAGIC      0xdeadbeef
#define MBUF_BLOCK_MIN_SIZE   512
#define MBUF_BLOCK_MAX_SIZE   16777216
#define MBUF_BLOCK_SIZE       16384
#define MBUF_BLOCK_HSIZE      sizeof(struct mbuf_block)

#define MBUF_BLOCK_EMPTY(mbuf_block) ((mbuf_block)->pos  == (mbuf_block)->last)
#define MBUF_BLOCK_FULL(mbuf_block)  ((mbuf_block)->last == (mbuf_block)->end)

void mbuf_pool_init(struct mbuf_pool *pool);
void mbuf_pool_deinit(struct mbuf_pool *pool);
size_t mbuf_pool_data_size(struct mbuf_pool *pool);
unsigned int mbuf_pool_compact(struct mbuf_pool *pool);

struct mbuf_block *mbuf_block_get(struct mbuf_pool *pool);
void mbuf_block_put(struct mbuf_block *mbuf_block);

void mbuf_block_ref(struct mbuf_block *mbuf_block);
void mbuf_block_unref(struct mbuf_block *mbuf_block);

void _mbuf_block_assert_refcount_at_least_two(struct mbuf_block *mbuf_block);


/* A subset of an mbuf_block. */
class mbuf {
private:
	BOOST_COPYABLE_AND_MOVABLE(mbuf)

	void initialize_with_block(unsigned int start, unsigned int len);
	void initialize_with_block_just_created(unsigned int start, unsigned int len);
	void initialize_with_mbuf(const mbuf &other, unsigned int start, unsigned int len);

public:
	struct just_created_t { };

	struct mbuf_block *mbuf_block; /* container block */
	char              *start;      /* start of subset (const) */
	char              *end;        /* end of subset (const) */

	mbuf()
		: mbuf_block(NULL),
		  start(NULL),
		  end(NULL)
		{ }

	explicit mbuf(struct mbuf_block *block, unsigned int start = 0)
		: mbuf_block(block)
	{
		initialize_with_block(start, block->end - block->start);
	}

	explicit mbuf(struct mbuf_block *block, unsigned int start, unsigned int len)
		: mbuf_block(block)
	{
		initialize_with_block(start, len);
	}

	explicit mbuf(struct mbuf_block *block, unsigned int start, unsigned int len,
		const just_created_t &t)
		: mbuf_block(block)
	{
		initialize_with_block_just_created(start, len);
	}

	// Create an mbuf as a dumb wrapper around a memory buffer.
	explicit mbuf(const char *data, unsigned int len)
		: mbuf_block(NULL),
		  start(const_cast<char *>(data)),
		  end(const_cast<char *>(data) + len)
		{ }

	explicit mbuf(const char *data)
		: mbuf_block(NULL),
		  start(const_cast<char *>(data)),
		  end(const_cast<char *>(data) + strlen(data))
		{ }

	// Copy constructor.
	mbuf(const mbuf &mbuf, unsigned int start = 0) {
		initialize_with_mbuf(mbuf, start, mbuf.end - mbuf.start);
	}

	// Take a subset of another mbuf.
	mbuf(const mbuf &mbuf, unsigned int start, unsigned int len) {
		initialize_with_mbuf(mbuf, start, len);
	}

	// Move constructor.
	explicit
	mbuf(BOOST_RV_REF(mbuf) mbuf)
		: mbuf_block(mbuf.mbuf_block),
		  start(mbuf.start),
		  end(mbuf.end)
	{
		mbuf.mbuf_block = NULL;
		mbuf.start = NULL;
		mbuf.end   = NULL;
	}

	~mbuf() {
		if (mbuf_block != NULL) {
			mbuf_block_unref(mbuf_block);
		}
	}

	// Copy assignment.
	mbuf &operator=(BOOST_COPY_ASSIGN_REF(mbuf) other) {
		if (&other != this) {
			#ifndef NDEBUG
				if (mbuf_block != NULL && mbuf_block == other.mbuf_block) {
					_mbuf_block_assert_refcount_at_least_two(mbuf_block);
				}
			#endif

			if (mbuf_block != NULL) {
				mbuf_block_unref(mbuf_block);
			}

			mbuf_block = other.mbuf_block;
			start      = other.start;
			end        = other.end;

			// We reference 'other.mbuf_block' instead of 'this->mbuf_block' as
			// a micro-optimization. This should decrease the number of data
			// dependencies and allow the CPU to reorder the instructions better.
			if (other.mbuf_block != NULL) {
				mbuf_block_ref(other.mbuf_block);
			}
		}
		return *this;
	}

	// Move assignment.
	mbuf &operator=(BOOST_RV_REF(mbuf) other) {
		if (&other != this) {
			#ifndef NDEBUG
				if (mbuf_block != NULL && mbuf_block == other.mbuf_block) {
					_mbuf_block_assert_refcount_at_least_two(mbuf_block);
				}
			#endif

			if (mbuf_block != NULL) {
				mbuf_block_unref(mbuf_block);
			}

			mbuf_block = other.mbuf_block;
			start      = other.start;
			end        = other.end;

			other.mbuf_block = NULL;
			other.start = NULL;
			other.end   = NULL;
		}
		return *this;
	}

	OXT_FORCE_INLINE
	size_t size() const {
		return end - start;
	}

	OXT_FORCE_INLINE
	bool empty() const {
		return start == end;
	}

	OXT_FORCE_INLINE
	bool is_null() const {
		return start == NULL;
	}
};

mbuf mbuf_block_subset(struct mbuf_block *mbuf_block, unsigned int start, unsigned int len);
mbuf mbuf_get(struct mbuf_pool *pool);
mbuf mbuf_get_with_size(struct mbuf_pool *pool, size_t size);


} // namespace MemoryKit
} // namespace Passenger

#endif /* _PSG_MBUF_BLOCK_H_ */
