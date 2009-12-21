/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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
	
	bool operator==(const StaticString &other) const {
		return len == other.len && memcmp(content, other.content, len) == 0;
	}
	
	bool operator==(const char *other) const {
		return memcmp(content, other, strlen(other)) == 0;
	}
	
	bool operator!=(const StaticString &other) const {
		return len == other.len && memcmp(content, other.content, len) != 0;
	}
	
	bool operator!=(const char *other) const {
		return memcmp(content, other, strlen(other)) != 0;
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

} // namespace Passenger

#endif /* _PASSENGER_STATIC_STRING_H_ */
