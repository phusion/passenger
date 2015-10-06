/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_SERVER_KIT_COOKIE_UTILS_H_
#define _PASSENGER_SERVER_KIT_COOKIE_UTILS_H_

#include <cstring>
#include <cassert>
#include <MemoryKit/palloc.h>
#include <DataStructures/LString.h>

namespace Passenger {
namespace ServerKit {


inline bool findCookieNameValueSeparator(const LString::Part *part, size_t index,
	const LString::Part **separatorPart, size_t *separatorIndex);
inline bool findCookieEnd(const LString::Part *separatorPart, size_t separatorIndex,
	const LString::Part **endPart, size_t *endIndex);
inline bool matchCookieName(psg_pool_t *pool, const LString::Part *part, size_t index,
	const LString::Part *separatorPart, size_t separatorIndex,
	const LString *name);
inline LString *extractCookieValue(psg_pool_t *pool,
	const LString::Part *separatorPart, size_t separatorIndex,
	const LString::Part *endPart, size_t endIndex);


/**
 * Given the value of an HTTP cookie header, returns the value of the cookie
 * of the given name, or NULL if not found.
 */
inline LString *
findCookie(psg_pool_t *pool, const LString *cookieHeaderValue, const LString *name) {
	const LString::Part *part = cookieHeaderValue->start;
	const LString::Part *separatorPart, *endPart;
	size_t index = 0, separatorIndex, endIndex;
	bool done = part == NULL;
	LString *result = NULL;

	while (!done) {
		if (findCookieNameValueSeparator(part, index, &separatorPart, &separatorIndex)) {
			if (!findCookieEnd(separatorPart, separatorIndex, &endPart, &endIndex)) {
				done = true;
			} else if (matchCookieName(pool, part, index, separatorPart, separatorIndex, name)) {
				result = extractCookieValue(pool, separatorPart, separatorIndex, endPart, endIndex);
				done   = true;
			} else {
				part  = endPart;
				index = endIndex;
				done  = endIndex >= endPart->size;
			}
		} else {
			done = true;
		}
	}

	return result;
}

inline bool
findCookieNameValueSeparator(const LString::Part *part, size_t index,
	const LString::Part **separatorPart, size_t *separatorIndex)
{
	const char *pos;
	bool result = false;
	bool done = part == NULL;

	while (!done) {
		pos = (const char *) memchr(part->data + index, '=', part->size - index);
		if (pos == NULL) {
			part  = part->next;
			index = 0;
			done  = part == NULL;
		} else {
			*separatorPart  = part;
			*separatorIndex = pos - part->data;
			result = true;
			done   = true;
		}
	}

	return result;
}

inline bool
findCookieEnd(const LString::Part *separatorPart, size_t separatorIndex,
	const LString::Part **endPart, size_t *endIndex)
{
	const LString::Part *part = separatorPart;
	size_t index = separatorIndex;
	const char *pos;
	bool result = false;
	bool done = part == NULL;

	while (!done) {
		pos = (const char *) memchr(part->data + index, ';', part->size - index);
		if (pos == NULL) {
			if (part->next == NULL) {
				// Semicolon not found in entire LString. Return end-of-LString
				// as cookie end.
				*endPart = part;
				*endIndex = part->size;
				result = true;
				done   = true;
			} else {
				part  = part->next;
				index = 0;
				done  = part == NULL;
			}
		} else {
			// Semicolon found.
			*endPart = part;
			*endIndex = pos - part->data;
			result = true;
			done   = true;
		}
	}

	return result;
}

inline void
_matchCookieName_skipWhitespace(LString *str) {
	LString::Part *part = str->start;
	size_t pos = 0;
	bool done = false;

	while (!done) {
		while (part->data[pos] == ' ' || part->data[pos] == ';') {
			pos++;
		}

		if (pos == part->size) {
			str->start = part->next;
			str->size -= part->size;
			part = part->next;
			if (part == NULL) {
				assert(str->size == 0);
				done = true;
				str->end = NULL;
			}
		} else {
			part->data += pos;
			part->size -= pos;
			str->size  -= pos;
			done = true;
		}
	}
}

inline bool
matchCookieName(psg_pool_t *pool, const LString::Part *part, size_t index,
	const LString::Part *separatorPart, size_t separatorIndex,
	const LString *name)
{
	LString *str = (LString *) psg_palloc(pool, sizeof(LString));
	psg_lstr_init(str);

	if (part == separatorPart) {
		assert(index < separatorIndex);
		psg_lstr_append(str, pool,
			part->data + index,
			separatorIndex - index);
	} else {
		psg_lstr_append(str, pool,
			part->data + index,
			part->size - index);

		part = part->next;
		while (part != separatorPart) {
			psg_lstr_append(str, pool, part->data, part->size);
			part = part->next;
		}

		if (separatorIndex != 0) {
			psg_lstr_append(str, pool, separatorPart->data, separatorIndex);
		}
	}

	_matchCookieName_skipWhitespace(str);

	bool result = psg_lstr_cmp(str, name);
	psg_lstr_deinit(str);
	return result;
}

inline LString *
extractCookieValue(psg_pool_t *pool,
	const LString::Part *separatorPart, size_t separatorIndex,
	const LString::Part *endPart, size_t endIndex)
{
	LString *str = (LString *) psg_palloc(pool, sizeof(LString));
	psg_lstr_init(str);

	if (separatorPart == endPart) {
		assert(separatorIndex < endIndex);
		psg_lstr_append(str, pool,
			separatorPart->data + separatorIndex + 1,
			endIndex - separatorIndex - 1);
	} else {
		if (separatorIndex < separatorPart->size - 1) {
			psg_lstr_append(str, pool,
				separatorPart->data + separatorIndex + 1,
				separatorPart->size - separatorIndex - 1);
		}

		const LString::Part *part = separatorPart->next;
		while (part != endPart) {
			psg_lstr_append(str, pool, part->data, part->size);
			part = part->next;
		}

		if (endIndex != 0) {
			psg_lstr_append(str, pool, endPart->data, endIndex);
		}
	}

	return str;
}


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_COOKIE_UTILS_H_ */
