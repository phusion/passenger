/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2025 Asynchronous B.V.
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

#include <cstddef>
#include <cstring>
#include <utility>
#include <stdexcept>
#include <ostream>
#include <StaticString.h>


namespace Passenger {

using namespace std;


size_t
StaticString::Hash::operator()(const StaticString &str) const {
	const char *data = str.content;
	const char *end  = str.content + str.len;
	size_t result    = 0;

	#if defined(__i386__) || defined(__x86_64__)
		/* When on x86 or x86_64, process 4 or 8 bytes
		 * per iteration by treating the data as an
		 * array of longs. Luckily for us these
		 * architectures can read longs even on unaligned
		 * addresses.
		 */
		const char *last_long = str.content +
			str.len / sizeof(unsigned long) *
			sizeof(unsigned long);

		while (data < last_long) {
			unsigned long l = 0;
			memcpy(&l, data, sizeof(unsigned long));
			result = result * 33 + l;
			data += sizeof(unsigned long);
		}

		/* Process leftover data byte-by-byte. */
	#endif

	while (data < end) {
		result = result * 33 + *data;
		data++;
	}
	return result;
}


static const char *
memmem(const char *haystack, string::size_type haystackLen,
	const char *needle, string::size_type needleLen)
{
	if (needleLen == 0) {
		return haystack;
	}

	const char *lastPossible = haystack + haystackLen - needleLen;
	do {
		const char *result = (const char *) memchr(haystack, needle[0], haystackLen);
		if (result != NULL) {
			if (result > lastPossible) {
				return NULL;
			} else if (memcmp(result, needle, needleLen) == 0) {
				return result;
			} else {
				ssize_t newLen = ssize_t(haystackLen) - (result - haystack) - 1;
				if (newLen <= 0) {
					return NULL;
				} else {
					haystack = result + 1;
					haystackLen = newLen;
				}
			}
		} else {
			return NULL;
		}
	} while (true);
}


StaticString::StaticString(const string &s) {
	content = s.data();
	len = s.size();
}

StaticString::StaticString(const char *data)
	: content(data),
	  len(strlen(data))
{ }

StaticString::StaticString(const char *data, string::size_type _len)
	: content(data),
	  len(_len)
{ }

string
StaticString::toString() const {
	return string(content, len);
}

string::size_type
StaticString::find(char c, string::size_type pos) const {
	if (pos < len) {
		const char *result = (const char *) memchr(content + pos, c, len - pos);
		if (result == NULL) {
			return string::npos;
		} else {
			return result - content;
		}
	} else {
		return string::npos;
	}
}

string::size_type
StaticString::find(const StaticString &s, string::size_type pos) const {
	if (s.empty()) {
		return 0;
	} else if (pos < len) {
		const char *result = memmem(content + pos, len - pos, s.c_str(), s.size());
		if (result == NULL) {
			return string::npos;
		} else {
			return result - content;
		}
	} else {
		return string::npos;
	}
}

string::size_type
StaticString::find(const char *s, string::size_type pos, string::size_type n) const {
	return find(StaticString(s, n), pos);
}

string::size_type
StaticString::find_first_of(const StaticString &str, size_t pos) const {
	const char *current = content + pos;
	const char *end = content + len;
	const char *tokens = str.data();
	const char *tokensEnd = str.data() + str.size();

	while (current < end) {
		const char *currentToken = tokens;
		while (currentToken < tokensEnd) {
			if (*current == *currentToken) {
				return current - content;
			}
			currentToken++;
		}
		current++;
	}

	return string::npos;
}

StaticString
StaticString::substr(string::size_type pos, string::size_type n) const {
	if (pos > len) {
		throw out_of_range("Argument 'pos' out of range");
	} else {
		if (n > len - pos) {
			n = len - pos;
		}
		return StaticString(content + pos, n);
	}
}

void
StaticString::swap(StaticString &other) noexcept {
	std::swap(content, other.content);
	std::swap(len, other.len);
}

bool
StaticString::operator==(const StaticString &other) const {
	return len == other.len && memcmp(content, other.content, len) == 0;
}

bool
StaticString::operator==(const string &other) const {
	return len == other.size() && memcmp(content, other.data(), len) == 0;
}

bool
StaticString::operator==(const char *other) const {
	size_t otherLen = strlen(other);
	return len == otherLen && memcmp(content, other, otherLen) == 0;
}

bool
StaticString::operator!=(const StaticString &other) const {
	return len != other.len || memcmp(content, other.content, len) != 0;
}

bool
StaticString::operator!=(const string &other) const {
	return len != other.size() || memcmp(content, other.data(), len) != 0;
}

bool
StaticString::operator!=(const char *other) const {
	size_t otherLen = strlen(other);
	return len != otherLen || memcmp(content, other, otherLen) != 0;
}

bool
StaticString::operator<(const StaticString &other) const {
	size_t size = (len < other.size()) ? len : other.size();
	int result = memcmp(content, other.data(), size);
	if (result == 0) {
		return len < other.size();
	} else {
		return result < 0;
	}
}

bool
StaticString::operator<(const char *other) const {
	return *this < StaticString(other);
}

string
StaticString::operator+(const char *other) const {
	return string(content, len) + other;
}

string
StaticString::operator+(const string &other) const {
	return string(content, len) + other;
}

string
StaticString::operator+(const StaticString &other) const {
	string result(content, len);
	result.append(other.data(), other.size());
	return result;
}

StaticString::operator string() const {
	return string(content, len);
}


string
operator+(const char *lhs, const StaticString &rhs) {
	return StaticString(lhs) + rhs;
}

string
operator+(const string &lhs, const StaticString &rhs) {
	string result = lhs;
	result.append(rhs.data(), rhs.size());
	return result;
}

ostream &
operator<<(ostream &os, const StaticString &str) {
	os.write(str.data(), str.size());
	return os;
}

bool
operator==(const string &other, const StaticString &str) {
	return str == other;
}

bool
operator==(const char *other, const StaticString &str) {
	return str == other;
}

bool
operator!=(const string &other, const StaticString &str) {
	return str != other;
}

bool
operator!=(const char *other, const StaticString &str) {
	return str != other;
}

} // namespace Passenger
