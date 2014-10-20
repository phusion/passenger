/*
 * twemproxy - A fast and lightweight proxy for memcached protocol.
 * Copyright (C) 2011 Twitter, Inc.
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

#include <cstdlib>
#include <cstring>
#include <oxt/macros.hpp>
#include <oxt/thread.hpp>
#include <cstring>
#include <MemoryKit/mbuf.h>

namespace Passenger {
namespace MemoryKit {

//#define MBUF_DEBUG


static struct mbuf_block *
_mbuf_block_get(struct mbuf_pool *pool)
{
	struct mbuf_block *mbuf_block;
	char *buf;

	if (!STAILQ_EMPTY(&pool->free_mbuf_blockq)) {
		assert(pool->nfree_mbuf_blockq > 0);

		mbuf_block = STAILQ_FIRST(&pool->free_mbuf_blockq);
		pool->nfree_mbuf_blockq--;
		STAILQ_REMOVE_HEAD(&pool->free_mbuf_blockq, next);

		assert(mbuf_block->magic == MBUF_BLOCK_MAGIC);
		goto done;
	}

	buf = (char *) malloc(pool->mbuf_block_chunk_size);
	if (OXT_UNLIKELY(buf == NULL)) {
		return NULL;
	}

	/*
	 * mbuf_block header is at the tail end of the mbuf_block. This enables us to catch
	 * buffer overrun early by asserting on the magic value during get or
	 * put operations
	 *
	 *   <------------- mbuf_block_chunk_size ------------------->
	 *   +-------------------------------------------------------+
	 *   |       mbuf_block data          |  mbuf_block header   |
	 *   |     (mbuf_block_offset)        | (struct mbuf_block)  |
	 *   +-------------------------------------------------------+
	 *   ^           ^          ^         ^^
	 *   |           |          |         ||
	 *   \           |          |         |\
	 * block->start  \          |         | block->end (one byte past valid bound)
	 *              block->pos  |         \
	 *                          \         block
	 *                          block->last (one byte past valid byte)
	 *
	 */
	mbuf_block = (struct mbuf_block *)(buf + pool->mbuf_block_offset);
	mbuf_block->magic = MBUF_BLOCK_MAGIC;
	mbuf_block->pool  = pool;
	mbuf_block->refcount = 1;

done:
	STAILQ_NEXT(mbuf_block, next) = NULL;
	#ifdef MBUF_ENABLE_DEBUGGING
		TAILQ_INSERT_HEAD(&pool->active_mbuf_blockq, mbuf_block, active_q);
	#endif
	#ifdef MBUF_ENABLE_BACKTRACES
		mbuf_block->backtrace = strdup(oxt::thread::current_backtrace().c_str());
	#endif
	pool->nactive_mbuf_blockq++;
	return mbuf_block;
}

struct mbuf_block *
mbuf_block_get(struct mbuf_pool *pool)
{
	struct mbuf_block *mbuf_block;
	char *buf;

	mbuf_block = _mbuf_block_get(pool);
	if (OXT_UNLIKELY(mbuf_block == NULL)) {
		return NULL;
	}

	buf = (char *)mbuf_block - pool->mbuf_block_offset;
	mbuf_block->start = buf;
	mbuf_block->end = buf + pool->mbuf_block_offset;

	assert(mbuf_block->end - mbuf_block->start == (int)pool->mbuf_block_offset);
	assert(mbuf_block->start < mbuf_block->end);

	mbuf_block->pos = mbuf_block->start;
	mbuf_block->last = mbuf_block->start;

	#ifdef MBUF_DEBUG
		printf("[%p] mbuf_block get %p\n", oxt::thread_signature, mbuf_block);
	#endif

	return mbuf_block;
}

static void
mbuf_block_free(struct mbuf_pool *pool, struct mbuf_block *mbuf_block)
{
	char *buf;

	//log_debug(LOG_VVERB, "put mbuf_block %p len %d", mbuf_block, mbuf_block->last - mbuf_block->pos);

	assert(STAILQ_NEXT(mbuf_block, next) == NULL);
	assert(mbuf_block->magic == MBUF_BLOCK_MAGIC);

	#ifdef MBUF_ENABLE_DEBUGGING
		TAILQ_REMOVE(&pool->active_mbuf_blockq, mbuf_block, active_q);
	#endif
	#ifdef MBUF_ENABLE_BACKTRACES
		free(mbuf_block->backtrace);
	#endif

	buf = (char *) mbuf_block - pool->mbuf_block_offset;
	free(buf);
}

void
mbuf_block_put(struct mbuf_block *mbuf_block)
{
	#ifdef MBUF_DEBUG
		printf("[%p] mbuf_block put %p\n", oxt::thread_signature, mbuf_block);
	#endif

	assert(STAILQ_NEXT(mbuf_block, next) == NULL);
	assert(mbuf_block->magic == MBUF_BLOCK_MAGIC);
	assert(mbuf_block->refcount == 0);
	assert(mbuf_block->pool->nactive_mbuf_blockq > 0);

	mbuf_block->refcount = 1;
	mbuf_block->pool->nfree_mbuf_blockq++;
	mbuf_block->pool->nactive_mbuf_blockq--;
	STAILQ_INSERT_HEAD(&mbuf_block->pool->free_mbuf_blockq, mbuf_block, next);
}

/*
 * Rewind the mbuf_block by discarding any of the read or unread data that it
 * might hold.
 */
void
mbuf_block_rewind(struct mbuf_block *mbuf_block)
{
	mbuf_block->pos = mbuf_block->start;
	mbuf_block->last = mbuf_block->start;
}

/*
 * Return the length of data in mbuf_block. Mbuf cannot contain more than
 * 2^32 bytes (4G).
 */
uint32_t
mbuf_block_length(struct mbuf_block *mbuf_block)
{
	assert(mbuf_block->last >= mbuf_block->pos);

	return (uint32_t)(mbuf_block->last - mbuf_block->pos);
}

/*
 * Return the remaining space size for any new data in mbuf_block. Mbuf cannot
 * contain more than 2^32 bytes (4G).
 */
uint32_t
mbuf_block_size(struct mbuf_block *mbuf_block)
{
	assert(mbuf_block->end >= mbuf_block->last);

	return (uint32_t)(mbuf_block->end - mbuf_block->last);
}

/*
 * Insert mbuf_block at the tail of the mhdr Q
 */
void
mbuf_block_insert(struct mhdr *mhdr, struct mbuf_block *mbuf_block)
{
	STAILQ_INSERT_TAIL(mhdr, mbuf_block, next);
	//log_debug(LOG_VVERB, "insert mbuf_block %p len %d", mbuf_block, mbuf_block->last - mbuf_block->pos);
}

/*
 * Remove mbuf_block from the mhdr Q
 */
void
mbuf_block_remove(struct mhdr *mhdr, struct mbuf_block *mbuf_block)
{
	//log_debug(LOG_VVERB, "remove mbuf_block %p len %d", mbuf_block, mbuf_block->last - mbuf_block->pos);

	STAILQ_REMOVE(mhdr, mbuf_block, struct mbuf_block, next);
	STAILQ_NEXT(mbuf_block, next) = NULL;
}

/*
 * Copy n bytes from memory area pos to mbuf_block.
 *
 * The memory areas should not overlap and the mbuf_block should have
 * enough space for n bytes.
 */
void
mbuf_block_copy(struct mbuf_block *mbuf_block, char *pos, size_t n)
{
	if (n == 0) {
		return;
	}

	/* mbuf_block has space for n bytes */
	assert(!MBUF_BLOCK_FULL(mbuf_block) && n <= mbuf_block_size(mbuf_block));

	/* no overlapping copy */
	assert(pos < mbuf_block->start || pos >= mbuf_block->end);

	memcpy(mbuf_block->last, pos, n);
	mbuf_block->last += n;
}

/*
 * Split mbuf_block h into h and t by copying data from h to t. Before
 * the copy, we invoke a precopy handler cb that will copy a predefined
 * string to the head of t.
 *
 * Return new mbuf_block t, if the split was successful.
 */
struct mbuf_block *
mbuf_block_split(struct mbuf_pool *pool, struct mhdr *h, char *pos,
	mbuf_block_copy_t cb, void *cbarg)
{
	struct mbuf_block *mbuf_block, *nbuf;
	size_t size;

	assert(!STAILQ_EMPTY(h));

	mbuf_block = STAILQ_LAST(h, struct mbuf_block, next);
	assert(pos >= mbuf_block->pos && pos <= mbuf_block->last);

	nbuf = mbuf_block_get(pool);
	if (nbuf == NULL) {
		return NULL;
	}

	if (cb != NULL) {
		/* precopy nbuf */
		cb(nbuf, cbarg);
	}

	/* copy data from mbuf_block to nbuf */
	size = (size_t)(mbuf_block->last - pos);
	mbuf_block_copy(nbuf, pos, size);

	/* adjust mbuf_block */
	mbuf_block->last = pos;

	//log_debug(LOG_VVERB, "split into mbuf_block %p len %"PRIu32" and nbuf %p len "
	//          "%"PRIu32" copied %zu bytes", mbuf_block, mbuf_block_length(mbuf_block), nbuf,
	//          mbuf_block_length(nbuf), size);

	return nbuf;
}

void
mbuf_pool_init(struct mbuf_pool *pool)
{
	pool->nfree_mbuf_blockq = 0;
	pool->nactive_mbuf_blockq = 0;
	STAILQ_INIT(&pool->free_mbuf_blockq);

	#ifdef MBUF_ENABLE_DEBUGGING
		TAILQ_INIT(&pool->active_mbuf_blockq);
	#endif

	pool->mbuf_block_offset = pool->mbuf_block_chunk_size - MBUF_BLOCK_HSIZE;
}

void
mbuf_pool_deinit(struct mbuf_pool *pool)
{
	mbuf_pool_compact(pool);
}

/*
 * Return the maximum available space size for data in any mbuf_block. Mbuf cannot
 * contain more than 2^32 bytes (4G).
 */
size_t
mbuf_pool_data_size(struct mbuf_pool *pool)
{
	return pool->mbuf_block_offset;
}

unsigned int
mbuf_pool_compact(struct mbuf_pool *pool)
{
	unsigned int count = pool->nfree_mbuf_blockq;

	while (!STAILQ_EMPTY(&pool->free_mbuf_blockq)) {
		struct mbuf_block *mbuf_block = STAILQ_FIRST(&pool->free_mbuf_blockq);
		mbuf_block_remove(&pool->free_mbuf_blockq, mbuf_block);
		mbuf_block_free(pool, mbuf_block);
		pool->nfree_mbuf_blockq--;
	}
	assert(pool->nfree_mbuf_blockq == 0);

	return count;
}


void
mbuf_block_ref(struct mbuf_block *mbuf_block)
{
	#ifdef MBUF_DEBUG
		printf("[%p] mbuf_block ref %p: %u -> %u\n",
			oxt::thread_signature, mbuf_block,
			mbuf_block->refcount, mbuf_block->refcount + 1);
	#endif
	#ifdef MBUF_ENABLE_BACKTRACES
		mbuf_block->backtrace = strdup(oxt::thread::current_backtrace().c_str());
	#endif
	mbuf_block->refcount++;
}

void
mbuf_block_unref(struct mbuf_block *mbuf_block)
{
	#ifdef MBUF_DEBUG
		printf("[%p] mbuf_block unref %p: %u -> %u\n",
			oxt::thread_signature, mbuf_block,
			mbuf_block->refcount, mbuf_block->refcount - 1);
	#endif
	assert(mbuf_block->refcount > 0);
	mbuf_block->refcount--;
	if (mbuf_block->refcount == 0) {
		mbuf_block_put(mbuf_block);
	}
}

mbuf
mbuf_block_subset(struct mbuf_block *mbuf_block, unsigned int start, unsigned int len)
{
	return mbuf(mbuf_block, start, len);
}

mbuf
mbuf_get(struct mbuf_pool *pool)
{
	struct mbuf_block *block = mbuf_block_get(pool);
	if (OXT_UNLIKELY(block == NULL)) {
		return mbuf();
	}

	assert(block->refcount == 1);
	block->refcount--;
	return mbuf(block, 0, block->end - block->start);
}


template<typename Address>
static Address clamp(Address value, Address min, Address max) {
	return std::max(std::min(value, max), min);
}

void
mbuf::initialize_with_block(unsigned int start, unsigned int len) {
	this->start = clamp<char *>(
		mbuf_block->start + start,
		mbuf_block->start,
		mbuf_block->end);
	this->end = clamp<char *>(
		mbuf_block->start + start + len,
		mbuf_block->start,
		mbuf_block->end);
	if (mbuf_block != NULL) {
		mbuf_block_ref(mbuf_block);
	}
}

void
mbuf::initialize_with_mbuf(const mbuf &mbuf, unsigned int start, unsigned int len) {
	mbuf_block = mbuf.mbuf_block;
	this->start = clamp<char *>(
		mbuf.start + start,
		mbuf.start,
		mbuf.end);
	this->end = clamp<char *>(
		mbuf.start + start + len,
		mbuf.start,
		mbuf.end);
	if (mbuf.mbuf_block != NULL) {
		mbuf_block_ref(mbuf.mbuf_block);
	}
}


} // namespace MemoryKit
} // namespace Passenger
