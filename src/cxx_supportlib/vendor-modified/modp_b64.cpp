/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4 -*- */
/* vi: set expandtab shiftwidth=4 tabstop=4: */
/**
 * \file modp_b64.c
 * <PRE>
 * MODP_B64 - High performance base64 encoder/decoder
 * http://code.google.com/p/stringencoders/
 *
 * Copyright &copy; 2005, 2006, 2007  Nick Galbreath -- nickg [at] modp [dot] com
 * All rights reserved.
 *
 * Modified for inclusion in Phusion Passenger.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 *   Neither the name of the modp.com nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This is the standard "new" BSD license:
 * http://www.opensource.org/licenses/bsd-license.php
 * </PRE>
 */

#include "modp_b64_data.h"

/* public header */
#include "modp_b64.h"

size_t modp_b64_encode(char* dest, const char* str, size_t len)
{
	size_t i = 0;
	const modp_uint8_t* s = (const modp_uint8_t*) str;
	modp_uint8_t* p = (modp_uint8_t*) dest;

	/* unsigned here is important! */
	/* modp_uint8_t is fastest on G4, amd */
	/* modp_uint32_t is fastest on Intel */
	modp_uint32_t t1, t2, t3;

	if (len > 2) {
		for (i = 0; i < len - 2; i += 3) {
			t1 = s[i]; t2 = s[i+1]; t3 = s[i+2];
			*p++ = e0[t1];
			*p++ = e1[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)];
			*p++ = e1[((t2 & 0x0F) << 2) | ((t3 >> 6) & 0x03)];
			*p++ = e2[t3];
		}
	}

	switch (len - i) {
	case 0:
		break;
	case 1:
		t1 = s[i];
		*p++ = e0[t1];
		*p++ = e1[(t1 & 0x03) << 4];
		*p++ = B64_CHARPAD;
		*p++ = B64_CHARPAD;
		break;
	default: /* case 2 */
		t1 = s[i]; t2 = s[i+1];
		*p++ = e0[t1];
		*p++ = e1[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)];
		*p++ = e2[(t2 & 0x0F) << 2];
		*p++ = B64_CHARPAD;
	}

	*p = '\0';
	return (size_t)(p - (modp_uint8_t*)dest);
}

#if defined(__x86_64__) || defined(__x86__)
/* INTEL & AMD */

#if defined(__has_feature)
	#if __has_feature(address_sanitizer)
		#define MODP_NO_SANITIZE(args) __attribute__((no_sanitize(args)))
	#endif
#endif
#ifndef MODP_NO_SANITIZE
	#define MODP_NO_SANITIZE(args)
#endif

MODP_NO_SANITIZE("undefined")
size_t modp_b64_decode(char* dest, const char* src, size_t len)
{
	size_t i;
	size_t leftover;
	size_t chunks;

	modp_uint8_t* p;
	modp_uint32_t x;
	modp_uint32_t* destInt;
	const modp_uint32_t* srcInt = (const modp_uint32_t*) src;
	modp_uint32_t y = *srcInt++;

	if (len == 0) return 0;

#ifdef B64_DOPAD
	/*
	 * if padding is used, then the message must be at least
	 * 4 chars and be a multiple of 4
	 */
	if (len < 4 || (len % 4 != 0)) {
		return (size_t)-1; /* error */
	}
	/* there can be at most 2 pad chars at the end */
	if (src[len-1] == B64_CHARPAD) {
		len--;
		if (src[len -1] == B64_CHARPAD) {
			len--;
		}
	}
#endif

	leftover = len % 4;
	chunks = (leftover == 0) ? len / 4 - 1 : len /4;

	p = (modp_uint8_t*) dest;
	x = 0;
	destInt = (modp_uint32_t*) p;
	srcInt = (const modp_uint32_t*) src;
	y = *srcInt++;
	for (i = 0; i < chunks; ++i) {
		x = d0[y & 0xff] |
			d1[(y >> 8) & 0xff] |
			d2[(y >> 16) & 0xff] |
			d3[(y >> 24) & 0xff];

		if (x >= B64_BADCHAR) {
			return (size_t)-1;
		}
		*destInt = x ;
		p += 3;
		destInt = (modp_uint32_t*)p;
		y = *srcInt++;}


	switch (leftover) {
	case 0:
		x = d0[y & 0xff] |
			d1[(y >> 8) & 0xff] |
			d2[(y >> 16) & 0xff] |
			d3[(y >> 24) & 0xff];

		if (x >= B64_BADCHAR) {
			return (size_t)-1;
		}
		*p++ =  ((modp_uint8_t*)(&x))[0];
		*p++ =  ((modp_uint8_t*)(&x))[1];
		*p =    ((modp_uint8_t*)(&x))[2];
		return (chunks+1)*3;
#ifndef B64_DOPAD
	case 1:  /* with padding this is an impossible case */
		x = d0[y & 0xff];
		*p = *((modp_uint8_t*)(&x)); /* i.e. first char/byte in int */
		break;
#endif
	case 2: /* case 2, 1  output byte */
		x = d0[y & 0xff] | d1[y >> 8 & 0xff];
		*p = *((modp_uint8_t*)(&x)); /* i.e. first char */
		break;
	default: /* case 3, 2 output bytes */
		x = d0[y & 0xff] |
			d1[y >> 8 & 0xff ] |
			d2[y >> 16 & 0xff];  /* 0x3c */
		*p++ =  ((modp_uint8_t*)(&x))[0];
		*p =  ((modp_uint8_t*)(&x))[1];
		break;
	}

	if (x >= B64_BADCHAR) {
		return (size_t)-1;
	}

	return 3*chunks + (6*leftover)/8;
}

#endif
