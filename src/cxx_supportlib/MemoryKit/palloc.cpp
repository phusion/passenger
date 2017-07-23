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

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <oxt/macros.hpp>
#include <MemoryKit/palloc.h>


static void psg_deinit_pool(psg_pool_t *pool);
static void psg_init_pool(psg_pool_t *pool, size_t size);
static void *psg_palloc_block(psg_pool_t *pool, size_t size);
static void *psg_palloc_large(psg_pool_t *pool, size_t size);


static void *
call_memalign(size_t alignment, size_t size) {
	void *ptr;
	int ret;

	ret = posix_memalign(&ptr, alignment, size);
	if (ret == 0) {
		return ptr;
	} else {
		errno = ret;
		return NULL;
	}
}


psg_pool_t *
psg_create_pool(size_t size)
{
	psg_pool_t  *p;

	p = (psg_pool_t *) call_memalign(PSG_POOL_ALIGNMENT, size);
	if (p == NULL) {
		return NULL;
	}

	psg_init_pool(p, size);

	return p;
}


void
psg_destroy_pool(psg_pool_t *pool)
{
	psg_deinit_pool(pool);
	free(pool);
}


static void
psg_init_pool(psg_pool_t *pool, size_t size)
{
	pool->data.last = (char *) pool + sizeof(psg_pool_t);
	pool->data.end  = (char *) pool + size;
	pool->data.next = NULL;
	pool->data.failed = 0;

	size = size - sizeof(psg_pool_t);
	pool->max = (size < PSG_MAX_ALLOC_FROM_POOL) ? size : PSG_MAX_ALLOC_FROM_POOL;

	pool->current = pool;
	pool->large = NULL;
}


static void
psg_deinit_pool(psg_pool_t *pool)
{
	psg_pool_t          *p;
	psg_pool_large_t    *l;

	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
			l->alloc = NULL;
		}
	}

	p = pool->data.next;
	while (p != NULL) {
		psg_pool_t *next = p->data.next;
		free(p);
		p = next;
	}
}


bool
psg_reset_pool(psg_pool_t *pool, size_t size)
{
	psg_pool_t        *p;
	psg_pool_large_t  *l;

	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
		}
	}

	if (pool->data.next == NULL) {
		psg_init_pool(pool, size);
		return true;
	} else {
		pool->current = pool;
		pool->large = NULL;

		for (p = pool; p; p = p->data.next) {
			char *m = (char *) p;
			if (p == pool) {
				m += sizeof(psg_pool_t);
			} else {
				m += sizeof(psg_pool_data_t);
			}
			m = psg_align_ptr(m, PSG_ALIGNMENT);
			p->data.last = m;

			p->data.failed = 0;
		}

		return false;
	}
}


void *
psg_palloc(psg_pool_t *pool, size_t size)
{
	char        *m;
	psg_pool_t  *p;

	if (OXT_LIKELY(size <= pool->max)) {
		p = pool->current;

		do {
			m = psg_align_ptr(p->data.last, PSG_ALIGNMENT);

			if ((size_t) (p->data.end - m) >= size) {
				p->data.last = m + size;
				return m;
			}

			p = p->data.next;
		} while (p);

		return psg_palloc_block(pool, size);
	}

	return psg_palloc_large(pool, size);
}


void *
psg_pnalloc(psg_pool_t *pool, size_t size)
{
	char        *m;
	psg_pool_t  *p;

	if (size <= pool->max) {
		p = pool->current;

		do {
			m = p->data.last;

			if ((size_t) (p->data.end - m) >= size) {
				p->data.last = m + size;
				return m;
			}

			p = p->data.next;
		} while (p);

		return psg_palloc_block(pool, size);
	}

	return psg_palloc_large(pool, size);
}


static void *
psg_palloc_block(psg_pool_t *pool, size_t size)
{
	char *m;
	size_t       psize;
	psg_pool_t  *p, *new_p, *current;

	psize = (size_t) (pool->data.end - (char *) pool);

	m = (char *) call_memalign(PSG_POOL_ALIGNMENT, psize);
	if (m == NULL) {
		return NULL;
	}

	new_p = (psg_pool_t *) m;

	new_p->data.end = m + psize;
	new_p->data.next = NULL;
	new_p->data.failed = 0;

	// We increment by sizeof(psg_pool_data_t) here, NOT
	// sizeof(psg_pool_t). This is because all fields after `data`
	// are only used in the first psg_pool_s object, not in any
	// subsequently linked ones.
	m += sizeof(psg_pool_data_t);
	m = psg_align_ptr(m, PSG_ALIGNMENT);
	new_p->data.last = m + size;

	current = pool->current;

	for (p = current; p->data.next; p = p->data.next) {
		if (p->data.failed++ > 4) {
			current = p->data.next;
		}
	}

	p->data.next = new_p;

	pool->current = current ? current : new_p;

	return m;
}


static void *
psg_palloc_large(psg_pool_t *pool, size_t size)
{
	void              *p;
	unsigned int       n;
	psg_pool_large_t  *large;

	p = malloc(size);
	if (p == NULL) {
		return NULL;
	}

	n = 0;

	for (large = pool->large; large; large = large->next) {
		if (large->alloc == NULL) {
			large->alloc = p;
			return p;
		}

		if (n++ > 3) {
			break;
		}
	}

	large = (psg_pool_large_t *) psg_palloc(pool, sizeof(psg_pool_large_t));
	if (large == NULL) {
		free(p);
		return NULL;
	}

	large->alloc = p;
	large->next = pool->large;
	pool->large = large;

	return p;
}


void *
psg_pmemalign(psg_pool_t *pool, size_t size, size_t alignment)
{
	void              *p;
	psg_pool_large_t  *large;

	p = call_memalign(alignment, size);
	if (p == NULL) {
		return NULL;
	}

	large = (psg_pool_large_t *) psg_palloc(pool, sizeof(psg_pool_large_t));
	if (large == NULL) {
		free(p);
		return NULL;
	}

	large->alloc = p;
	large->next = pool->large;
	pool->large = large;

	return p;
}


Passenger::StaticString
psg_pstrdup(psg_pool_t *pool, const Passenger::StaticString &str)
{
	char *newstr = (char *) psg_pnalloc(pool, str.size() + 1);
	memcpy(newstr, str.data(), str.size());
	newstr[str.size()] = '\0';
	return Passenger::StaticString(newstr, str.size());
}


bool
psg_pfree(psg_pool_t *pool, void *p)
{
	psg_pool_large_t  *l, *prev;

	prev = NULL;

	for (l = pool->large; l; l = l->next) {
		if (p == l->alloc) {
			free(l->alloc);
			l->alloc = NULL;
			if (prev != NULL) {
				prev->next = l->next;
			} else {
				pool->large = l->next;
			}
			return true;
		} else {
			prev = l;
		}
	}

	return false;
}


void *
psg_pcalloc(psg_pool_t *pool, size_t size)
{
	void *p;

	p = psg_palloc(pool, size);
	if (p) {
		memset(p, 0, size);
	}

	return p;
}
