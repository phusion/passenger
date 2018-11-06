/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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

#include <boost/cstdint.hpp>
#include <cstddef>
#include <oxt/macros.hpp>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {

using namespace std;


// This file implements StrIntUtils functions that violate strict aliasing
// in order to gain more performance.

#if defined(__x86_64__) || defined(__x86__)
OXT_NO_SANITIZE("undefined")
void
convertLowerCase(const unsigned char * restrict data,
	unsigned char * restrict output,
	size_t len)
{
	/*
	 * Parts of this function is taken from stringencoders and modified to add
	 * 64-bit support. https://code.google.com/p/stringencoders/
	 *
	 * Copyright 2005, 2006, 2007
	 * Nick Galbreath -- nickg [at] modp [dot] com
	 * All rights reserved.
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
	 */
	static const boost::uint8_t gsToLowerMap[256] = {
		'\0', 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, '\t',
		'\n', 0x0b, 0x0c, '\r', 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
		0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
		0x1e, 0x1f,  ' ',  '!',  '"',  '#',  '$',  '%',  '&', '\'',
		 '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',  '0',  '1',
		 '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  ':',  ';',
		 '<',  '=',  '>',  '?',  '@',  'a',  'b',  'c',  'd',  'e',
		 'f',  'g',  'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
		 'p',  'q',  'r',  's',  't',  'u',  'v',  'w',  'x',  'y',
		 'z',  '[', '\\',  ']',  '^',  '_',  '`',  'a',  'b',  'c',
		 'd',  'e',  'f',  'g',  'h',  'i',  'j',  'k',  'l',  'm',
		 'n',  'o',  'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
		 'x',  'y',  'z',  '{',  '|',  '}',  '~', 0x7f, 0x80, 0x81,
		0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
		0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
		0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
		0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
		0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
		0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd,
		0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1,
		0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
		0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5,
		0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
		0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
		0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
	};

	#if defined(__x86_64__)
		size_t i;
		boost::uint64_t eax, ebx;
		const boost::uint8_t *ustr = (const boost::uint8_t *) data;
		const size_t leftover = len % 8;
		const size_t imax = len / 8;
		const boost::uint64_t *s = (const boost::uint64_t *) data;
		boost::uint64_t *d = (boost::uint64_t *) output;

		for (i = 0; i != imax; ++i) {
			eax = s[i];
			/*
			 * This is based on the algorithm by Paul Hsieh
			 * http://www.azillionmonkeys.com/qed/asmexample.html
			 */
			ebx = (0x7f7f7f7f7f7f7f7fu & eax) + 0x2525252525252525u;
			ebx = (0x7f7f7f7f7f7f7f7fu & ebx) + 0x1a1a1a1a1a1a1a1au;
			ebx = ((ebx & ~eax) >> 2)  & 0x2020202020202020u;
			*d++ = eax + ebx;
		}

		i = imax * 8;
		output = (unsigned char *) d;
		switch (leftover) {
		case 7: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 6: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 5: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 4: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 3: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 2: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 1: *output++ = (unsigned char) gsToLowerMap[ustr[i]]; /* Falls through. */
		case 0: break;
		}
	#elif defined(__x86__)
		size_t i;
		boost::uint32_t eax, ebx;
		const boost::uint8_t *ustr = (const boost::uint8_t *) data;
		const size_t leftover = len % 4;
		const size_t imax = len / 4;
		const boost::uint32_t *s = (const boost::uint32_t *) data;
		boost::uint32_t *d = (boost::uint32_t *) output;
		for (i = 0; i != imax; ++i) {
			eax = s[i];
			/*
			 * This is based on the algorithm by Paul Hsieh
			 * http://www.azillionmonkeys.com/qed/asmexample.html
			 */
			ebx = (0x7f7f7f7fu & eax) + 0x525252525u;
			ebx = (0x7f7f7f7fu & ebx) + 0x1a1a1a1au;
			ebx = ((ebx & ~eax) >> 2) & 0x20202020u;
			*d++ = eax + ebx;
		}

		i = imax * 4;
		output = (unsigned char *) d;
		switch (leftover) {
		case 3: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 2: *output++ = (unsigned char) gsToLowerMap[ustr[i++]]; /* Falls through. */
		case 1: *output++ = (unsigned char) gsToLowerMap[ustr[i]]; /* Falls through. */
		case 0: break;
		}
	#else
		#error "Unsupported architecture"
	#endif
}
#endif


} // namespace Passenger
