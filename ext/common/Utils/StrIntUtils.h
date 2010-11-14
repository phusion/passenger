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
#ifndef _PASSENGER_STR_INT_UTILS_H_
#define _PASSENGER_STR_INT_UTILS_H_

#include <string>
#include <vector>
#include <sstream>
#include "StaticString.h"

namespace Passenger {

using namespace std;

/**
 * Given a prefix string, a middle string and a postfix string, try to build a string
 * that looks like <tt>prefix + middle + postfix</tt>, with as many characters from
 * <tt>midle</tt> preserved as possible.
 *
 * If <tt>prefix + middle + postfix</tt> does not fit in <tt>max</tt> characters,
 * then <tt>middle</tt> will be truncated so that it fits. If <tt>max</tt> is too
 * small to contain even 1 character from <tt>middle</tt>, then an ArgumentException
 * will be thrown.
 *
 * @code
 *   fillInMiddle(18, "server.", "1234", ".socket");    // "server.1234.socket"
 *   fillInMiddle(16, "server.", "1234", ".socket");    // "server.12.socket"
 *   fillInMiddle(14, "server.", "1234", ".socket");    // ArgumentException
 * @endcode
 *
 * @returns The resulting string, with <tt>middle</tt> possibly truncated.
 * @throws ArgumentException <tt>max</tt> is too small to contain even 1
 *         character from <tt>middle</tt>.
 * @post result.size() <= max
 */
string fillInMiddle(unsigned int max, const string &prefix, const string &middle,
	const string &postfix = "");

/**
 * Checks whether <tt>str</tt> starts with <tt>substr</tt>.
 */
bool startsWith(const StaticString &str, const StaticString &substr);

/**
 * Split the given string using the given separator.
 *
 * @param str The string to split.
 * @param sep The separator to use.
 * @param output The vector to write the output to.
 */
void split(const string &str, char sep, vector<string> &output);

/**
 * Convert anything to a string.
 */
template<typename T> string
toString(T something) {
	stringstream s;
	s << something;
	return s.str();
}

string toString(const vector<string> &vec);
string toString(const vector<StaticString> &vec);

string pointerToIntString(void *pointer);

/**
 * Converts the given integer string to an unsigned long long integer.
 */
unsigned long long stringToULL(const StaticString &str);

/**
 * Converts the given integer string to a long long integer.
 */
long long stringToLL(const StaticString &str);

/**
 * Converts the given hexadecimal string to an unsigned long long integer.
 */
unsigned long long hexToULL(const StaticString &str);

/**
 * Converts the given hexatridecimal (base 36) string to an unsigned long long integer.
 */
unsigned long long hexatriToULL(const StaticString &str);

/**
 * Convert the given binary data to hexadecimal.
 */
string toHex(const StaticString &data);

/**
 * Convert the given binary data to hexadecimal. This form accepts an
 * output buffer which must be at least <tt>data.size() * 2</tt> bytes large.
 */
void toHex(const StaticString &data, char *output, bool upperCase = false);

/**
 * Convert the given integer to some other radix, placing
 * the result into the given output buffer. This buffer must be at
 * least <tt>2 * sizeof(IntegerType) + 1</tt> bytes. The output buffer
 * will be NULL terminated. Supported radices are 2-36.
 *
 * @return The size of the created string, excluding
 *         terminating NULL.
 */
template<typename IntegerType, int radix>
unsigned int
integerToOtherBase(IntegerType value, char *output) {
	static const char hex_chars[] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
		'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
		'u', 'v', 'w', 'x', 'y', 'z'
	};
	char buf[sizeof(value) * 2];
	IntegerType remainder = value;
	unsigned int size = 0;
	
	do {
		buf[size] = hex_chars[remainder % radix];
		remainder = remainder / radix;
		size++;
	} while (remainder != 0);
	
	for (unsigned int i = 0; i < size; i++) {
		output[size - i - 1] = buf[i];
	}
	output[size] = '\0';
	return size;
}

/**
 * Convert the given integer to hexadecimal, placing the result
 * into the given output buffer. This buffer must be at least
 * <tt>2 * sizeof(IntegerType) + 1</tt> bytes. The output buffer
 * will be NULL terminated.
 *
 * @return The size of the created hexadecimal string, excluding
 *         terminating NULL.
 */
template<typename IntegerType>
unsigned int
integerToHex(IntegerType value, char *output) {
	return integerToOtherBase<IntegerType, 16>(value, output);
}

/**
 * Convert the given integer to a hexadecimal string.
 */
string integerToHex(long long value);

/**
 * Convert the given integer to hexatridecimal (Base 36), placing the
 * result into the given output buffer. This buffer must be at least
 * <tt>2 * sizeof(IntegerType) + 1</tt> bytes. The output buffer
 * will be NULL terminated.
 *
 * @return The size of the created hexatridecimal string, excluding
 *         terminating NULL.
 */
template<typename IntegerType>
unsigned int
integerToHexatri(IntegerType value, char *output) {
	return integerToOtherBase<IntegerType, 36>(value, output);
}

/**
 * Convert the given integer to a hexatridecimal string.
 */
string integerToHexatri(long long value);

/**
 * Converts the given string to an integer.
 */
int atoi(const string &s);

/**
 * Converts the given string to a long integer.
 */
long atol(const string &s);

/**
 * Round <em>number</em> up to the nearest multiple of <em>multiple</em>.
 */
template<typename IntegerType>
IntegerType
roundUp(IntegerType number, IntegerType multiple) {
	return (number + multiple - 1) / multiple * multiple;
}

/**
 * Escape non-ASCII-printable characters in the given string with C-style escape sequences,
 * e.g. "foo\nbar\0" becomes "foo\\nbar\\0".
 */
string cEscapeString(const StaticString &input);

/**
 * Escapes HTML special characters the given input string, which is assumed to
 * contain UTF-8 data. Returns a UTF-8 encoded string.
 *
 * @throws utf8::exception A UTF-8 decoding error occurred.
 */
string escapeHTML(const StaticString &input);

} // namespace Passenger

#endif /* _PASSENGER_STR_INT_UTILS_H_ */
