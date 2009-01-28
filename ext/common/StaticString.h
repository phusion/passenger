/*
 * Copyright (c) 2009 Phusion
 * Do not redistribute.
 */
#ifndef _PASSENGER_STATIC_STRING_H_
#define _PASSENGER_STATIC_STRING_H_

#include <string>

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
	
	const char *c_str() const {
		return content;
	}
	
	const char *data() const {
		return content;
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

} // namespace Passenger

#endif /* _PASSENGER_STATIC_STRING_H_ */
