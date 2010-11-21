/*-
 * Copyright 1997-2003 John-Mark Gurney.
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
 *
 *	$Id: fib.c,v 1.31 2003/01/14 10:11:30 jmg Exp $
 *
 */

#include "fib.h"
#include "fibpriv.h"

#include <limits.h>
#include <stdlib.h>

#define swap(type, a, b)		\
		do {			\
			type c;		\
			c = a;		\
			a = b;		\
			b = c;		\
		} while (0)		\

#define INT_BITS        (sizeof(int) * 8)
static inline int
ceillog2(unsigned int a)
{
	int oa;
	int i;
	int b;

	oa = a;
	b = INT_BITS / 2;
	i = 0;
	while (b) {
		i = (i << 1);
		if (a >= (unsigned int) (1 << b)) {
			a /= (1 << b);
			i = i | 1;
		} else
			a &= (1 << b) - 1;
		b /= 2;
	}
	if ((1 << i) == oa)
		return i;
	else
		return i + 1;
}

/*
 * Private Heap Functions
 */
static void
fh_deleteel(struct fibheap *h, struct fibheap_el *x)
{
	void *data;
	int key;

	data = x->fhe_data;
	key = x->fhe_key;

	if (!h->fh_keys)
		fh_replacedata(h, x, h->fh_neginf);
	else
		fh_replacekey(h, x, INT_MIN);
	if (fh_extractminel(h) != x) {
		/*
		 * XXX - This should never happen as fh_replace should set it
		 * to min.
		 */
		abort();
	}

	x->fhe_data = data;
	x->fhe_key = key;
}

void
fh_initheap(struct fibheap *h)
{
	h->fh_cmp_fnct = NULL;
	h->fh_neginf = NULL;
	h->fh_n = 0;
	h->fh_Dl = -1;
	h->fh_cons = NULL;
	h->fh_min = NULL;
	h->fh_root = NULL;
	h->fh_keys = 0;
#ifdef FH_STATS
	h->fh_maxn = 0;
	h->fh_ninserts = 0;
	h->fh_nextracts = 0;
#endif
}

void
fh_destroyheap(struct fibheap *h)
{
	h->fh_cmp_fnct = NULL;
	h->fh_neginf = NULL;
	if (h->fh_cons != NULL)
		free(h->fh_cons);
	h->fh_cons = NULL;
}

/*
 * Public Heap Functions
 */
struct fibheap *
fh_makekeyheap()
{
	struct fibheap *n;

	if ((n = (struct fibheap *) malloc(sizeof *n)) == NULL)
		return NULL;

	fh_initheap(n);
	n->fh_keys = 1;

	return n;
}

struct fibheap *
fh_makeheap()
{
	struct fibheap *n;

	if ((n = (struct fibheap *) malloc(sizeof *n)) == NULL)
		return NULL;

	fh_initheap(n);

	return n;
}

voidcmp
fh_setcmp(struct fibheap *h, voidcmp fnct)
{
	voidcmp oldfnct;
	
	oldfnct = h->fh_cmp_fnct;
	h->fh_cmp_fnct = fnct;

	return oldfnct;
}

void *
fh_setneginf(struct fibheap *h, void *data)
{
	void *old;

	old = h->fh_neginf;
	h->fh_neginf = data;

	return old;
}

struct fibheap *
fh_union(struct fibheap *ha, struct fibheap *hb)
{
	struct fibheap_el *x;

	if (ha->fh_root == NULL || hb->fh_root == NULL) {
		/* either one or both are empty */
		if (ha->fh_root == NULL) {
			fh_destroyheap(ha);
			free(ha);
			return hb;
		} else {
			fh_destroyheap(hb);
			free(hb);
			return ha;
		}
	}
	ha->fh_root->fhe_left->fhe_right = hb->fh_root;
	hb->fh_root->fhe_left->fhe_right = ha->fh_root;
	x = ha->fh_root->fhe_left;
	ha->fh_root->fhe_left = hb->fh_root->fhe_left;
	hb->fh_root->fhe_left = x;
	ha->fh_n += hb->fh_n;
	/*
	 * we probably should also keep stats on number of unions
	 */

	/* set fh_min if necessary */
	if (fh_compare(ha, hb->fh_min, ha->fh_min) < 0)
		ha->fh_min = hb->fh_min;

	fh_destroyheap(hb);
	free(hb);
	return ha;
}

void
fh_deleteheap(struct fibheap *h)
{
	/*
	 * We could do this even faster by walking each binomial tree, but
	 * this is simpler to code.
	 */
	while (h->fh_min != NULL)
		fhe_destroy(fh_extractminel(h));

	fh_destroyheap(h);
	free(h);
}

/*
 * Public Key Heap Functions
 */
struct fibheap_el *
fh_insertkey(struct fibheap *h, int key, void *data)
{
	struct fibheap_el *x;

	if ((x = fhe_newelem()) == NULL)
		return NULL;

	/* just insert on root list, and make sure it's not the new min */
	x->fhe_data = data;
	x->fhe_key = key;

	fh_insertel(h, x);

	return x;
}

int
fh_minkey(struct fibheap *h)
{
	if (h->fh_min == NULL)
		return INT_MIN;
	return h->fh_min->fhe_key;
}

int
fh_replacekey(struct fibheap *h, struct fibheap_el *x, int key)
{
	int ret;

	ret = x->fhe_key;
	(void)fh_replacekeydata(h, x, key, x->fhe_data);

	return ret;
}

void *
fh_replacekeydata(struct fibheap *h, struct fibheap_el *x, int key, void *data)
{
	void *odata;
	int okey;
	struct fibheap_el *y;
	int r;

	odata = x->fhe_data;
	okey = x->fhe_key;

	/*
	 * we can increase a key by deleting and reinserting, that
	 * requires O(lgn) time.
	 */
	if ((r = fh_comparedata(h, key, data, x)) > 0) {
		/* XXX - bad code! */
		abort();
		fh_deleteel(h, x);

		x->fhe_data = data;
		x->fhe_key = key;

		fh_insertel(h, x);

		return odata;
	}

	x->fhe_data = data;
	x->fhe_key = key;

	/* because they are equal, we don't have to do anything */
	if (r == 0)
		return odata;

	y = x->fhe_p;

	if (h->fh_keys && okey == key)
		return odata;

	if (y != NULL && fh_compare(h, x, y) <= 0) {
		fh_cut(h, x, y);
		fh_cascading_cut(h, y);
	}

	/*
	 * the = is so that the call from fh_delete will delete the proper
	 * element.
	 */
	if (fh_compare(h, x, h->fh_min) <= 0)
		h->fh_min = x;

	return odata;
}

/*
 * Public void * Heap Functions
 */
/*
 * this will return these values:
 *	NULL	failed for some reason
 *	ptr	token to use for manipulation of data
 */
struct fibheap_el *
fh_insert(struct fibheap *h, void *data)
{
	struct fibheap_el *x;

	if ((x = fhe_newelem()) == NULL)
		return NULL;

	/* just insert on root list, and make sure it's not the new min */
	x->fhe_data = data;

	fh_insertel(h, x);

	return x;
}

void *
fh_min(struct fibheap *h)
{
	if (h->fh_min == NULL)
		return NULL;
	return h->fh_min->fhe_data;
}

void *
fh_extractmin(struct fibheap *h)
{
	struct fibheap_el *z;
	void *ret;

	ret = NULL;

	if (h->fh_min != NULL) {
		z = fh_extractminel(h);
		ret = z->fhe_data;
#ifndef NO_FREE
		fhe_destroy(z);
#endif

	}

	return ret;
}

void *
fh_replacedata(struct fibheap *h, struct fibheap_el *x, void *data)
{
	return fh_replacekeydata(h, x, x->fhe_key, data);
}

void *
fh_delete(struct fibheap *h, struct fibheap_el *x)
{
	void *k;

	k = x->fhe_data;
	if (!h->fh_keys)
		fh_replacedata(h, x, h->fh_neginf);
	else
		fh_replacekey(h, x, INT_MIN);
	fh_extractmin(h);

	return k;
}

/*
 * Statistics Functions
 */
#ifdef FH_STATS
int
fh_maxn(struct fibheap *h)
{
	return h->fh_maxn;
}

int
fh_ninserts(struct fibheap *h)
{
	return h->fh_ninserts;
}

int
fh_nextracts(struct fibheap *h)
{
	return h->fh_nextracts;
}
#endif

/*
 * begin of private element fuctions
 */
static struct fibheap_el *
fh_extractminel(struct fibheap *h)
{
	struct fibheap_el *ret;
	struct fibheap_el *x, *y, *orig;

	ret = h->fh_min;

	orig = NULL;
	/* put all the children on the root list */
	/* for true consistancy, we should use fhe_remove */
	for(x = ret->fhe_child; x != orig && x != NULL;) {
		if (orig == NULL)
			orig = x;
		y = x->fhe_right;
		x->fhe_p = NULL;
		fh_insertrootlist(h, x);
		x = y;
	}
	/* remove minimum from root list */
	fh_removerootlist(h, ret);
	h->fh_n--;

	/* if we aren't empty, consolidate the heap */
	if (h->fh_n == 0)
		h->fh_min = NULL;
	else {
		h->fh_min = ret->fhe_right;
		fh_consolidate(h);
	}

#ifdef FH_STATS
	h->fh_nextracts++;
#endif

	return ret;
}

static void
fh_insertrootlist(struct fibheap *h, struct fibheap_el *x)
{
	if (h->fh_root == NULL) {
		h->fh_root = x;
		x->fhe_left = x;
		x->fhe_right = x;
		return;
	}

	fhe_insertafter(h->fh_root, x);
}

static void
fh_removerootlist(struct fibheap *h, struct fibheap_el *x)
{
	if (x->fhe_left == x)
		h->fh_root = NULL;
	else
		h->fh_root = fhe_remove(x);
}

static void
fh_consolidate(struct fibheap *h)
{
	struct fibheap_el **a;
	struct fibheap_el *w;
	struct fibheap_el *y;
	struct fibheap_el *x;
	int i;
	int d;
	int D;

	fh_checkcons(h);

	/* assign a the value of h->fh_cons so I don't have to rewrite code */
	D = h->fh_Dl + 1;
	a = h->fh_cons;

	for (i = 0; i < D; i++)
		a[i] = NULL;

	while ((w = h->fh_root) != NULL) {
		x = w;
		fh_removerootlist(h, w);
		d = x->fhe_degree;
		/* XXX - assert that d < D */
		while(a[d] != NULL) {
			y = a[d];
			if (fh_compare(h, x, y) > 0)
				swap(struct fibheap_el *, x, y);
			fh_heaplink(h, y, x);
			a[d] = NULL;
			d++;
		}
		a[d] = x;
	}
	h->fh_min = NULL;
	for (i = 0; i < D; i++)
		if (a[i] != NULL) {
			fh_insertrootlist(h, a[i]);
			if (h->fh_min == NULL || fh_compare(h, a[i],
			    h->fh_min) < 0)
				h->fh_min = a[i];
		}
}

static void
fh_heaplink(struct fibheap *h, struct fibheap_el *y, struct fibheap_el *x)
{
	/* make y a child of x */
	if (x->fhe_child == NULL)
		x->fhe_child = y;
	else
		fhe_insertbefore(x->fhe_child, y);
	y->fhe_p = x;
	x->fhe_degree++;
	y->fhe_mark = 0;
}

static void
fh_cut(struct fibheap *h, struct fibheap_el *x, struct fibheap_el *y)
{
	fhe_remove(x);
	y->fhe_degree--;
	fh_insertrootlist(h, x);
	x->fhe_p = NULL;
	x->fhe_mark = 0;
}

static void
fh_cascading_cut(struct fibheap *h, struct fibheap_el *y)
{
	struct fibheap_el *z;

	while ((z = y->fhe_p) != NULL) {
		if (y->fhe_mark == 0) {
			y->fhe_mark = 1;
			return;
		} else {
			fh_cut(h, y, z);
			y = z;
		}
	}
}

/*
 * begining of handling elements of fibheap
 */
static struct fibheap_el *
fhe_newelem()
{
	struct fibheap_el *e;

	if ((e = (struct fibheap_el *) malloc(sizeof *e)) == NULL)
		return NULL;

	fhe_initelem(e);

	return e;
}

static void
fhe_initelem(struct fibheap_el *e)
{
	e->fhe_degree = 0;
	e->fhe_mark = 0;
	e->fhe_p = NULL;
	e->fhe_child = NULL;
	e->fhe_left = e;
	e->fhe_right = e;
	e->fhe_data = NULL;
}

static void
fhe_insertafter(struct fibheap_el *a, struct fibheap_el *b)
{
	if (a == a->fhe_right) {
		a->fhe_right = b;
		a->fhe_left = b;
		b->fhe_right = a;
		b->fhe_left = a;
	} else {
		b->fhe_right = a->fhe_right;
		a->fhe_right->fhe_left = b;
		a->fhe_right = b;
		b->fhe_left = a;
	}
}

static inline void
fhe_insertbefore(struct fibheap_el *a, struct fibheap_el *b)
{
	fhe_insertafter(a->fhe_left, b);
}

static struct fibheap_el *
fhe_remove(struct fibheap_el *x)
{
	struct fibheap_el *ret;

	if (x == x->fhe_left)
		ret = NULL;
	else
		ret = x->fhe_left;

	/* fix the parent pointer */
	if (x->fhe_p != NULL && x->fhe_p->fhe_child == x)
		x->fhe_p->fhe_child = ret;

	x->fhe_right->fhe_left = x->fhe_left;
	x->fhe_left->fhe_right = x->fhe_right;

	/* clear out hanging pointers */
	x->fhe_p = NULL;
	x->fhe_left = x;
	x->fhe_right = x;

	return ret;
}

static void
fh_checkcons(struct fibheap *h)
{
	int oDl;

	/* make sure we have enough memory allocated to "reorganize" */
	if (h->fh_Dl == -1 || h->fh_n > (1 << h->fh_Dl)) {
		oDl = h->fh_Dl;
		if ((h->fh_Dl = ceillog2(h->fh_n) + 1) < 8)
			h->fh_Dl = 8;
		if (oDl != h->fh_Dl)
			h->fh_cons = (struct fibheap_el **)realloc(h->fh_cons,
			    sizeof *h->fh_cons * (h->fh_Dl + 1));
		if (h->fh_cons == NULL)
			abort();
	}
}

static int
fh_compare(struct fibheap *h, struct fibheap_el *a, struct fibheap_el *b)
{
	if (h->fh_keys) {
		if (a->fhe_key < b->fhe_key)
			return -1;
		if (a->fhe_key == b->fhe_key)
			return 0;
		return 1;
	} else
		return h->fh_cmp_fnct(a->fhe_data, b->fhe_data);
}

static int
fh_comparedata(struct fibheap *h, int key, void *data, struct fibheap_el *b)
{
	struct fibheap_el a;

	a.fhe_key = key;
	a.fhe_data = data;

	return fh_compare(h, &a, b);
}

static void
fh_insertel(struct fibheap *h, struct fibheap_el *x)
{
	fh_insertrootlist(h, x);

	if (h->fh_min == NULL || (h->fh_keys ? x->fhe_key < h->fh_min->fhe_key
	    : h->fh_cmp_fnct(x->fhe_data, h->fh_min->fhe_data) < 0))
		h->fh_min = x;

	h->fh_n++;

#ifdef FH_STATS
	if (h->fh_n > h->fh_maxn)
		h->fh_maxn = h->fh_n;
	h->fh_ninserts++;
#endif

}
