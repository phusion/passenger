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
#ifndef _PASSENGER_STATIC_STRING_H_
#define _PASSENGER_STATIC_STRING_H_

#include <oxt/macros.hpp>
#include <string>
#include <cstddef>
#include <iosfwd>


namespace Passenger {

using namespace std;

#define P_STATIC_STRING(x) Passenger::StaticString(x, sizeof(x) - 1)
#define P_STATIC_STRING_WITH_NULL(x) Passenger::StaticString(x, sizeof(x))

/**
 * An immutable, static byte buffer. This class never copies data:
 * it just holds a pointer to the data. So a StaticString will become unusable
 * once the data it refers to has been freed.
 *
 * StaticString never modifies the data.
 */
class StaticString {
private:
	const char *content;
	string::size_type len;

public:
	/** A hash function object for StaticString. */
	struct Hash {
		inline size_t operator()(const StaticString &str) const;
	};

	StaticString() noexcept: content(""), len(0) { };
	StaticString(const StaticString &b) noexcept = default;
	inline StaticString(const string &s);
	inline StaticString(const char *data);
	inline StaticString(const char *data, string::size_type _len);

	OXT_FORCE_INLINE
	bool empty() const noexcept {
		return len == 0;
	}

	OXT_FORCE_INLINE
	string::size_type size() const noexcept {
		return len;
	}

	OXT_FORCE_INLINE
	char operator[](string::size_type i) const noexcept {
		return content[i];
	}

	OXT_FORCE_INLINE
	char at(string::size_type i) const noexcept {
		return content[i];
	}

	OXT_FORCE_INLINE
	const char *c_str() const noexcept {
		return content;
	}

	OXT_FORCE_INLINE
	const char *data() const noexcept {
		return content;
	}

	inline string toString() const;

	inline string::size_type find(char c, string::size_type pos = 0) const;
	inline string::size_type find(const StaticString &s, string::size_type pos = 0) const;
	inline string::size_type find(const char *s, string::size_type pos, string::size_type n) const;
	inline string::size_type find_first_of(const StaticString &str, size_t pos = 0) const;

	inline StaticString substr(string::size_type pos = 0, string::size_type n = string::npos) const;

	inline void swap(StaticString &other) noexcept;

	StaticString &operator=(const StaticString &other) noexcept = default;

	inline bool operator==(const StaticString &other) const;
	inline bool operator==(const string &other) const;
	inline bool operator==(const char *other) const;

	inline bool operator!=(const StaticString &other) const;
	inline bool operator!=(const string &other) const;
	inline bool operator!=(const char *other) const;

	inline bool operator<(const StaticString &other) const;
	inline bool operator<(const char *other) const;

	inline string operator+(const char *other) const;
	inline string operator+(const string &other) const;
	inline string operator+(const StaticString &other) const;

	inline operator string() const;
};

inline string operator+(const char *lhs, const StaticString &rhs);
inline string operator+(const string &lhs, const StaticString &rhs);
inline ostream &operator<<(ostream &os, const StaticString &str);
inline bool operator==(const string &other, const StaticString &str);
inline bool operator==(const char *other, const StaticString &str);
inline bool operator!=(const string &other, const StaticString &str);
inline bool operator!=(const char *other, const StaticString &str);

} // namespace Passenger

#include <StaticString.tpp>

#endif /* _PASSENGER_STATIC_STRING_H_ */
