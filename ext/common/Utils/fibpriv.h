/*-
 * Copyright 1997, 1999-2003 John-Mark Gurney.
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
 *	$Id: fibpriv.h,v 1.12 2003/01/14 10:11:30 jmg Exp $
 *
 */

#ifndef _FIBPRIV_H_
#define _FIBPRIV_H_

/*
 * global heap operations
 */
static void fh_insertrootlist(struct fibheap *, struct fibheap_el *);
static void fh_removerootlist(struct fibheap *, struct fibheap_el *);
static void fh_consolidate(struct fibheap *);
static void fh_heaplink(struct fibheap *h, struct fibheap_el *y,
			struct fibheap_el *x);
static void fh_cut(struct fibheap *, struct fibheap_el *, struct fibheap_el *);
static void fh_cascading_cut(struct fibheap *, struct fibheap_el *);
static struct fibheap_el *fh_extractminel(struct fibheap *);
static void fh_checkcons(struct fibheap *h);
static int fh_compare(struct fibheap *h, struct fibheap_el *a,
			struct fibheap_el *b);
static int fh_comparedata(struct fibheap *h, int key, void *data,
			struct fibheap_el *b);
static void fh_insertel(struct fibheap *h, struct fibheap_el *x);
static void fh_deleteel(struct fibheap *h, struct fibheap_el *x);

/*
 * specific node operations
 */
static struct fibheap_el *fhe_newelem(void);
static void fhe_initelem(struct fibheap_el *);
static void fhe_insertafter(struct fibheap_el *a, struct fibheap_el *b);
static inline void fhe_insertbefore(struct fibheap_el *a, struct fibheap_el *b);
static struct fibheap_el *fhe_remove(struct fibheap_el *a);
#define	fhe_destroy(x)	free((x))

/*
 * general functions
 */
static inline int ceillog2(unsigned int a);

#endif /* _FIBPRIV_H_ */
