/*-
 * Copyright 1997, 1998-2003 John-Mark Gurney.
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
 *	$Id: fib.h,v 1.10 2003/01/14 10:11:30 jmg Exp $
 *
 */

#ifndef _FIB_H_
#define _FIB_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct fibheap_el;
typedef int (*voidcmp)(void *, void *);

struct fibheap {
	int	(*fh_cmp_fnct)(void *, void *);
	int	fh_n;
	int	fh_Dl;
	struct	fibheap_el **fh_cons;
	struct	fibheap_el *fh_min;
	struct	fibheap_el *fh_root;
	void	*fh_neginf;
	int	fh_keys		: 1;
#ifdef FH_STATS
	int	fh_maxn;
	int	fh_ninserts;
	int	fh_nextracts;
#endif
};

struct fibheap_el {
	int	fhe_degree;
	int	fhe_mark;
	struct	fibheap_el *fhe_p;
	struct	fibheap_el *fhe_child;
	struct	fibheap_el *fhe_left;
	struct	fibheap_el *fhe_right;
	int	fhe_key;
	void	*fhe_data;
};

/* functions for key heaps */
struct fibheap *fh_makekeyheap(void);
void fh_initheap(struct fibheap *);
struct fibheap_el *fh_insertkey(struct fibheap *, int, void *);
int fh_minkey(struct fibheap *);
int fh_replacekey(struct fibheap *, struct fibheap_el *, int);
void *fh_replacekeydata(struct fibheap *, struct fibheap_el *, int, void *);

/* functions for void * heaps */
struct fibheap *fh_makeheap(void);
voidcmp fh_setcmp(struct fibheap *, voidcmp);
void *fh_setneginf(struct fibheap *, void *);
struct fibheap_el *fh_insert(struct fibheap *, void *);

/* shared functions */
void *fh_extractmin(struct fibheap *);
void *fh_min(struct fibheap *);
void *fh_replacedata(struct fibheap *, struct fibheap_el *, void *);
void *fh_delete(struct fibheap *, struct fibheap_el *);
void fh_deleteheap(struct fibheap *);
void fh_destroyheap(struct fibheap *h);
/* this assumes both heaps are allocated on the heap */
struct fibheap *fh_union(struct fibheap *, struct fibheap *);

#ifdef FH_STATS
int fh_maxn(struct fibheap *);
int fh_ninserts(struct fibheap *);
int fh_nextracts(struct fibheap *);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _FIB_H_ */
