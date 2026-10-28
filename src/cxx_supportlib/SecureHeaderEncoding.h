/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2026 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#ifndef _PASSENGER_SECURE_HEADER_ENCODING_H_
#define _PASSENGER_SECURE_HEADER_ENCODING_H_

/*
 * Encoding of secure header values ("!~" headers) that web server modules send
 * to the Core. See doc/DesignAspects/SecureHeaders.md for why this exists.
 *
 * Values are percent-encoded: '%' and all control characters (bytes 0x00-0x1F
 * and 0x7F) become "%XX". This guarantees that an encoded value never contains
 * CR or LF, so it cannot end its own header line and inject other secure
 * headers, no matter where the value came from. Ordinary values contain none
 * of these bytes, so encoding usually leaves them unchanged.
 *
 * This header is plain C so that the Nginx module can use it too.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


static inline int
psg_secure_header_byte_needs_encoding(unsigned char ch) {
	return ch == '%' || ch < 0x20 || ch == 0x7F;
}

/* Returns the length that `data` has after encoding. */
static inline size_t
psg_secure_header_value_encoded_len(const char *data, size_t len) {
	size_t result = len;
	size_t i;

	for (i = 0; i < len; i++) {
		if (psg_secure_header_byte_needs_encoding((unsigned char) data[i])) {
			result += 2;
		}
	}
	return result;
}

/*
 * Encodes `data` into `dst`, which must have room for
 * psg_secure_header_value_encoded_len() bytes. Returns the end of the
 * written data.
 */
static inline char *
psg_secure_header_value_encode(char *dst, const char *data, size_t len) {
	static const char hex[] = "0123456789ABCDEF";
	size_t i;

	for (i = 0; i < len; i++) {
		unsigned char ch = (unsigned char) data[i];
		if (psg_secure_header_byte_needs_encoding(ch)) {
			*dst++ = '%';
			*dst++ = hex[ch >> 4];
			*dst++ = hex[ch & 0xF];
		} else {
			*dst++ = (char) ch;
		}
	}
	return dst;
}

static inline int
psg_secure_header_parse_hex_digit(char ch) {
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	} else if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	} else if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	} else {
		return -1;
	}
}

/*
 * Decodes `data` into `dst`, which must have room for `len` bytes.
 * Decoding is strict: every '%' must be followed by two hex digits.
 * Returns the decoded length, or (size_t) -1 if the encoding is invalid.
 */
static inline size_t
psg_secure_header_value_decode(char *dst, const char *data, size_t len) {
	char *pos = dst;
	size_t i = 0;

	while (i < len) {
		if (data[i] == '%') {
			int high, low;

			if (len - i < 3) {
				return (size_t) -1;
			}
			high = psg_secure_header_parse_hex_digit(data[i + 1]);
			low = psg_secure_header_parse_hex_digit(data[i + 2]);
			if (high < 0 || low < 0) {
				return (size_t) -1;
			}
			*pos++ = (char) ((high << 4) | low);
			i += 3;
		} else {
			*pos++ = data[i];
			i++;
		}
	}
	return pos - dst;
}


#ifdef __cplusplus
}
#endif

#endif /* _PASSENGER_SECURE_HEADER_ENCODING_H_ */
