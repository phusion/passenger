/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_STATIC_STRING_H_
#define _PASSENGER_STATIC_STRING_H_

#include <string>
#include <cstring>
#include <cstddef>
#include <ostream>
#include <stdexcept>

namespace Passenger {

using namespace std;

/**
 * An immutable, static byte buffer. This class will never copy data:
 * it just holds a pointer to the data. So a StaticString will become unusable
 * once the data it refers to has been freed.
 *
 * StaticString will never modify the data.
 */
class StaticString {
private:
	const char *content;
	string::size_type len;
	
	static const char *memmem(const char *haystack, string::size_type haystack_len,
		const char *needle, string::size_type needle_len)
	{
		if (needle_len == 0) {
			return haystack;
		}

		const char *last_possible = haystack + haystack_len - needle_len;
		do {
			const char *result = (const char *) memchr(haystack, needle[0], haystack_len);
			if (result != NULL) {
				if (result > last_possible) {
					return NULL;
				} else if (memcmp(result, needle, needle_len) == 0) {
					return result;
				} else {
					ssize_t new_len = ssize_t(haystack_len) - (result - haystack) - 1;
					if (new_len <= 0) {
						return NULL;
					} else {
						haystack = result + 1;
						haystack_len = new_len;
					}
				}
			} else {
				return NULL;
			}
		} while (true);
	}
	
public:
	/** A hash function object for StaticString. */
	struct Hash {
		size_t operator()(const StaticString &str) const {
			size_t result = 0;
			const char *data = str.content;
			const char *end  = data + str.len;
			while (data != end) {
				result = result * 33 + *data;
				data++;
			}
			return result;
		}
	};
	
	StaticString() {
		content = "";
		len = 0;
	}
	
	StaticString(const StaticString &b) {
		content = b.content;
		len = b.len;
	}
	
	StaticString(const string &s) {
		content = s.data();
		len = s.size();
	}
	
	StaticString(const char *data) {
		content = data;
		len = strlen(data);
	}
	
	StaticString(const char *data, string::size_type len) {
		content = data;
		this->len = len;
	}
	
	bool empty() const {
		return len == 0;
	}
	
	string::size_type size() const {
		return len;
	}
	
	char operator[](string::size_type i) const {
		return content[i];
	}
	
	char at(string::size_type i) const {
		return content[i];
	}
	
	const char *c_str() const {
		return content;
	}
	
	const char *data() const {
		return content;
	}
	
	string toString() const {
		return string(content, len);
	}
	
	bool equals(const StaticString &other) const {
		return len == other.len && memcmp(content, other.content, len) == 0;
	}
	
	bool equals(const string &other) const {
		return len == other.size() && memcmp(content, other.data(), len) == 0;
	}
	
	string::size_type find(char c, string::size_type pos = 0) const {
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
	
	string::size_type find(const StaticString &s, string::size_type pos = 0) const {
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
	
	string::size_type find(const char *s, string::size_type pos, string::size_type n) const {
		return find(StaticString(s, n), pos);
	}
	
	StaticString substr(string::size_type pos = 0, string::size_type n = string::npos) const {
		if (pos > len) {
			throw out_of_range("Argument 'pos' out of range");
		} else {
			if (n > len - pos) {
				n = len - pos;
			}
			return StaticString(content + pos, n);
		}
	}
	
	bool operator==(const StaticString &other) const {
		return len == other.len && memcmp(content, other.content, len) == 0;
	}
	
	bool operator==(const string &other) const {
		return len == other.size() && memcmp(content, other.data(), len) == 0;
	}
	
	bool operator==(const char *other) const {
		size_t other_len = strlen(other);
		return len == other_len && memcmp(content, other, other_len) == 0;
	}
	
	bool operator!=(const StaticString &other) const {
		return len != other.len || memcmp(content, other.content, len) != 0;
	}
	
	bool operator!=(const string &other) const {
		return len != other.size() || memcmp(content, other.data(), len) != 0;
	}
	
	bool operator!=(const char *other) const {
		size_t other_len = strlen(other);
		return len != other_len || memcmp(content, other, other_len) != 0;
	}
	
	bool operator<(const StaticString &other) const {
		size_t size = (len < other.size()) ? len : other.size();
		int result = memcmp(content, other.data(), size);
		if (result == 0) {
			return len < other.size();
		} else {
			return result < 0;
		}
	}
	
	bool operator<(const char *other) const {
		return *this < StaticString(other);
	}
	
	string operator+(const char *other) const {
		return string(content, len) + other;
	}
	
	string operator+(const string &other) const {
		return string(content, len) + other;
	}
	
	string operator+(const StaticString &other) const {
		string result(content, len);
		result.append(other.data(), other.size());
		return result;
	}
	
	operator string() const {
		return string(content, len);
	}
};

inline string
operator+(const char *lhs, const StaticString &rhs) {
	return StaticString(lhs) + rhs;
}

inline string
operator+(const string &lhs, const StaticString &rhs) {
	string result = lhs;
	result.append(rhs.data(), rhs.size());
	return result;
}

inline ostream &
operator<<(ostream &os, const StaticString &str) {
	os.write(str.data(), str.size());
	return os;
}

inline bool
operator==(const string &other, const StaticString &str) {
	return str == other;
}

inline bool
operator==(const char *other, const StaticString &str) {
	return str == other;
}

inline bool
operator!=(const string &other, const StaticString &str) {
	return str != other;
}

inline bool
operator!=(const char *other, const StaticString &str) {
	return str != other;
}

} // namespace Passenger

#endif /* _PASSENGER_STATIC_STRING_H_ */
