/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

#include "modp_b64_data.h"

/* public header */
#include "modp_b64.h"

#if !defined(__x86_64__) && !defined(__x86__)

size_t modp_b64_decode(char* dest, const char* src, size_t len)
{
	size_t i;
	if (len == 0) return 0;

#ifdef B64_DOPAD
	/* if padding is used, then the message must be at least
	   4 chars and be a multiple of 4.
	   there can be at most 2 pad chars at the end */
	if (len < 4 || (len % 4 != 0)) return -1;
	if (src[len-1] == B64_CHARPAD) {
		len--;
		if (src[len -1] == B64_CHARPAD) {
			len--;
		}
	}
#endif  /* B64_DOPAD */

	size_t leftover = len % 4;
	size_t chunks = (leftover == 0) ? len / 4 - 1 : len /4;

	modp_uint8_t* p = (modp_uint8_t*) dest;
	modp_uint32_t x = 0;
	const modp_uint8_t* srcInt = (modp_uint8_t*) src;
	modp_uint8_t y[4];

	y[3]=srcInt[0];
	y[2]=srcInt[1];
	y[1]=srcInt[2];
	y[0]=srcInt[3];
	srcInt+=4;

	for (i = 0; i < chunks; ++i) {
		x = d0[y[3]] | d1[y[2]] | d2[y[1]] | d3[y[0]];

		if (x >= B64_BADCHAR)  return -1;
		modp_uint32_t tmp_x = (x << 8);
		p[0] = (tmp_x >> 3*8) & 0xFF;
		p[1] = (tmp_x >> 2*8) & 0xFF;
		p[2] = (tmp_x >> 1*8) & 0xFF;
		p[3] = (tmp_x >> 0*8) & 0xFF;
		p += 3;

		y[3]=srcInt[0];
		y[2]=srcInt[1];
		y[1]=srcInt[2];
		y[0]=srcInt[3];
		srcInt+=4;
	}

	switch (leftover) {
	case 0:
		x = d0[y[3]] | d1[y[2]] | d2[y[1]] | d3[y[0]];
		if (x >= B64_BADCHAR)  return -1;
#ifdef BOOST_BIG_ENDIAN
		*p++ = ((modp_uint8_t*)&x)[1];
		*p++ = ((modp_uint8_t*)&x)[2];
		*p   = ((modp_uint8_t*)&x)[3];
#else
		*p++ = ((modp_uint8_t*)&x)[2];
		*p++ = ((modp_uint8_t*)&x)[1];
		*p   = ((modp_uint8_t*)&x)[0];
#endif
		return (chunks+1)*3;
#ifndef B64_DOPAD
	case 1:  /* with padding this is an impossible case */
		x = d3[y[3]];
		*p =  (modp_uint8_t)x;
		break;
#endif
	case 2:
		x = d3[y[3]] *64 + d3[y[2]];
		*p =  (modp_uint8_t)(x >> 4);
		break;
	default:  /* case 3 */
		x = (d3[y[3]] *64 + d3[y[2]])*64 + d3[y[1]];
		*p++ = (modp_uint8_t) (x >> 10);
		*p = (modp_uint8_t) (x >> 2);
		break;
	}

	if (x >= B64_BADCHAR) return -1;
	return 3*chunks + (6*leftover)/8;
}

#endif
