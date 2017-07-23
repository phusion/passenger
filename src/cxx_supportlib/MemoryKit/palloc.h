/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 * Copyright (c) 2013-2017 Phusion Holding B.V.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#ifndef _PASSENGER_MEMORY_KIT_PALLOC_H_INCLUDED_
#define _PASSENGER_MEMORY_KIT_PALLOC_H_INCLUDED_


#include <stddef.h>
#include <stdint.h>
#include <StaticString.h>

/** A pool allocator taken from Nginx. Modified to suit our needs.
 * The concept is also known as region-based memory management:
 * http://en.wikipedia.org/wiki/Region-based_memory_management
 *
 * The allocator allocates small objects in a pool (region) by bumping
 * a pointer, so allocating many small objects is extremely fast.
 * Objects that don't fit inside the pool are handled by the
 * "large memory allocator" and allocated directly using malloc().
 * Except for objects allocated by the large memory allocator,
 * objects can only be freed by freeing the entire pool.
 */

#define PSG_ALIGNMENT sizeof(unsigned long) /* platform word */

#define psg_align(d, a)     (((d) + (a - 1)) & ~(a - 1))
#define psg_align_ptr(p, a) \
	(char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))

/*
 * PSG_MAX_ALLOC_FROM_POOL should be (psg_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define psg_pagesize             4096
#define PSG_MAX_ALLOC_FROM_POOL  (psg_pagesize - 1)

#define PSG_DEFAULT_POOL_SIZE    (16 * 1024)

#define PSG_POOL_ALIGNMENT       16
#define PSG_MIN_POOL_SIZE                                                     \
	psg_align((sizeof(psg_pool_t) + 2 * sizeof(psg_pool_large_t)),            \
			  PSG_POOL_ALIGNMENT)


typedef struct psg_pool_s        psg_pool_t;
typedef struct psg_pool_large_s  psg_pool_large_t;

typedef struct psg_pool_large_s {
	psg_pool_large_s     *next;
	void                 *alloc;
} psg_pool_large_t;

typedef struct {
	char               *last;   /** Last allocated byte inside this block. */
	char               *end;    /** End of block memory. Read-only */
	psg_pool_t         *next;
	unsigned int        failed;
} psg_pool_data_t;

struct psg_pool_s {
	psg_pool_data_t       data;

	/*
	 * The following fields are only used for the first psg_pool_s,
	 * not for any subsequent psg_pool_s objects linked through
	 * `data.next`.
	 */
	size_t                max;      /* Read-only */
	psg_pool_t           *current;
	psg_pool_large_t     *large;
};


psg_pool_t *psg_create_pool(size_t size);
void psg_destroy_pool(psg_pool_t *pool);
bool psg_reset_pool(psg_pool_t *pool, size_t size);

/** Allocate `size` bytes from the pool, aligned on platform word size. */
void *psg_palloc(psg_pool_t *pool, size_t size);

/** Allocate `size` bytes from the pool, unaligned. **/
void *psg_pnalloc(psg_pool_t *pool, size_t size);

/** Allocate `size` bytes from the pool, unaligned. The allocated memory is zeroed. **/
void *psg_pcalloc(psg_pool_t *pool, size_t size);

/** Allocate `size` bytes from the pool, aligned on the given alignment. */
void *psg_pmemalign(psg_pool_t *pool, size_t size, size_t alignment);

/** Duplicate string by storing it inside the pool. Result is NULL terminated. */
Passenger::StaticString psg_pstrdup(psg_pool_t *pool, const Passenger::StaticString &str);

/** Attempt to free the given memory, which was allocated from the given pool.
 * If the memory was allocated using the pool's large memory allocator,
 * then the memory is freed. If not, then this function does nothing, because
 * there is no way to free the memory without freeing the entire pool.
 * Returns whether the memory was actually freed.
 */
bool  psg_pfree(psg_pool_t *pool, void *p);


#endif /* _PASSENGER_MEMORY_KIT_PALLOC_H_INCLUDED_ */
