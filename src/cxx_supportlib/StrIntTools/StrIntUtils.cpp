/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cassert>
#include <utf8.h>
#include <algorithm>
#include <Exceptions.h>
#include <SystemTools/SystemTime.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {

string
fillInMiddle(unsigned int max, const string &prefix, const string &middle, const string &postfix) {
	if (max <= prefix.size() + postfix.size()) {
		throw ArgumentException("Impossible to build string with the given size constraint.");
	}

	unsigned int fillSize = max - (prefix.size() + postfix.size());
	if (fillSize > middle.size()) {
		return prefix + middle + postfix;
	} else {
		return prefix + middle.substr(0, fillSize) + postfix;
	}
}

bool
startsWith(const StaticString &str, const StaticString &substr) {
	if (str.size() >= substr.size()) {
		return memcmp(str.c_str(), substr.c_str(), substr.size()) == 0;
	} else {
		return false;
	}
}

template<typename OutputString>
static void
_split(const StaticString &str, char sep, vector<OutputString> &output) {
	output.clear();
	if (!str.empty()) {
		string::size_type start, pos;
		start = 0;
		while ((pos = str.find(sep, start)) != string::npos) {
			output.push_back(str.substr(start, pos - start));
			start = pos + 1;
		}
		output.push_back(str.substr(start));
	}
}

void
split(const StaticString &str, char sep, vector<string> &output) {
	_split(str, sep, output);
}

void
split(const StaticString &str, char sep, vector<StaticString> &output) {
	_split(str, sep, output);
}

template<typename OutputString>
static void
_splitIncludeSep(const StaticString &str, char sep, vector<OutputString> &output) {
	output.clear();
	if (!str.empty()) {
		string::size_type start, pos;
		start = 0;
		while ((pos = str.find(sep, start)) != string::npos) {
			output.push_back(str.substr(start, pos - start + 1));
			start = pos + 1;
		}
		if (start != str.size()) {
			output.push_back(str.substr(start));
		}
	}
}

void
splitIncludeSep(const StaticString &str, char sep, vector<string> &output) {
	_splitIncludeSep(str, sep, output);
}

void
splitIncludeSep(const StaticString &str, char sep, vector<StaticString> &output) {
	_splitIncludeSep(str, sep, output);
}

void
truncateBeforeTokens(const char *str, const StaticString &tokens, int maxBetweenTokens, ostream &sstream) {
	StaticString source(str);

	if (source.empty()) {
		return;
	}

	string::size_type copyStart, findStart, pos;
	copyStart = 0; // for copying including the last found token
	findStart = 0;
	while ((pos = source.find_first_of(tokens, findStart)) != string::npos) {
		// Determine & limit how many chars between two tokens (or start and first token)
		int copyLen = pos - findStart;
		if (copyLen > maxBetweenTokens) {
			copyLen = maxBetweenTokens;
		}
		// Include token from the previous find (first iteration has no previous)
		if (findStart > 0) {
			copyLen++;
		}
		sstream << source.substr(copyStart, copyLen);
		copyStart = pos;
		findStart = pos + 1;
	}

	// Copy anything remaining (e.g. when no tokens at all)
	if (copyStart < source.size()) {
		sstream << source.substr(copyStart);
	}
}

string
replaceString(const StaticString &str, const StaticString &toFind, const StaticString &replaceWith) {
	string::size_type pos = str.find(toFind);
	if (pos == string::npos) {
		return str;
	} else {
		string result(str.data(), str.size());
		return result.replace(pos, toFind.size(), replaceWith);
	}
}

string
replaceAll(const StaticString &str, const StaticString &toFind, const StaticString &replaceWith) {
	string result = str;
	while (result.find(toFind) != string::npos) {
		result = replaceString(result, toFind, replaceWith);
	}
	return result;
}

string
strip(const StaticString &str) {
	const char *data = str.data();
	const char *end = str.data() + str.size();
	while (data < end && (*data == ' ' || *data == '\n' || *data == '\t')) {
		data++;
	}
	while (end > data && (end[-1] == ' ' || end[-1] == '\n' || end[-1] == '\t')) {
		end--;
	}
	return string(data, end - data);
}

string
toString(const vector<string> &vec) {
	vector<StaticString> vec2;
	vec2.reserve(vec.size());
	for (vector<string>::const_iterator it = vec.begin(); it != vec.end(); it++) {
		vec2.push_back(*it);
	}
	return toString(vec2);
}

string
toString(const vector<StaticString> &vec) {
	string result = "[";
	vector<StaticString>::const_iterator it;
	unsigned int i;
	for (it = vec.begin(), i = 0; it != vec.end(); it++, i++) {
		result.append("'");
		result.append(it->data(), it->size());
		if (i == vec.size() - 1) {
			result.append("'");
		} else {
			result.append("', ");
		}
	}
	result.append("]");
	return result;
}

string
doubleToString(double value) {
	char buf[64];
	int ret = snprintf(buf, sizeof(buf), "%f", value);
	return string(buf, std::min<size_t>(ret, sizeof(buf) - 1));
}

string
pointerToIntString(void *pointer) {
	return toString((boost::uintptr_t) pointer);
}

template<typename Numeric>
static Numeric
stringToUnsignedNumeric(const StaticString &str) {
	Numeric result = 0;
	string::size_type i = 0;
	const char *data = str.data();

	while (i < str.size() && data[i] == ' ') {
		i++;
	}
	while (i < str.size() && data[i] >= '0' && data[i] <= '9') {
		result *= 10;
		result += data[i] - '0';
		i++;
	}
	return result;
}

unsigned long long
stringToULL(const StaticString &str) {
	return stringToUnsignedNumeric<unsigned long long>(str);
}

unsigned int
stringToUint(const StaticString &str) {
	return stringToUnsignedNumeric<unsigned int>(str);
}

template<typename Numeric>
static Numeric
stringToSignedNumeric(const StaticString &str) {
	Numeric result = 0;
	string::size_type i = 0;
	const char *data = str.data();
	bool minus = false;

	while (i < str.size() && data[i] == ' ') {
		i++;
	}
	if (data[i] == '-') {
		minus = true;
		i++;
	}
	while (i < str.size() && data[i] >= '0' && data[i] <= '9') {
		result *= 10;
		result += data[i] - '0';
		i++;
	}
	if (minus) {
		return -result;
	} else {
		return result;
	}
}

long long
stringToLL(const StaticString &str) {
	return stringToSignedNumeric<long long>(str);
}

int
stringToInt(const StaticString &str) {
	return stringToSignedNumeric<int>(str);
}

template<typename Numeric>
static Numeric
hexToUnsignedNumeric(const StaticString &hex) {
	const char *pos = hex.data();
	const char *end = hex.data() + hex.size();
	Numeric result = 0;
	bool done = false;

	while (pos < end && !done) {
		char c = *pos;
		if (c >= '0' && c <= '9') {
			result *= 16;
			result += c - '0';
		} else if (c >= 'a' && c <= 'f') {
			result *= 16;
			result += 10 + (c - 'a');
		} else if (c >= 'A' && c <= 'F') {
			result *= 16;
			result += 10 + (c - 'A');
		} else {
			done = true;
		}
		pos++;
	}
	return result;
}

unsigned long long
hexToULL(const StaticString &hex) {
	return hexToUnsignedNumeric<unsigned long long>(hex);
}

unsigned int
hexToUint(const StaticString &hex) {
	return hexToUnsignedNumeric<unsigned int>(hex);
}

unsigned long long
hexatriToULL(const StaticString &str) {
	unsigned long long result = 0;
	string::size_type i = 0;
	bool done = false;

	while (i < str.size() && !done) {
		char c = str[i];
		if (c >= '0' && c <= '9') {
			result *= 36;
			result += c - '0';
		} else if (c >= 'a' && c <= 'z') {
			result *= 36;
			result += 10 + (c - 'a');
		} else if (c >= 'A' && c <= 'Z') {
			result *= 36;
			result += 10 + (c - 'A');
		} else {
			done = true;
		}
		i++;
	}
	return result;
}

string
toHex(const StaticString &data) {
	string result(data.size() * 2, '\0');
	toHex(data, (char *) result.data());
	return result;
}

void
reverseString(char *str, unsigned int size) {
	char *end = str + size - 1;
	char aux;
	while (str < end) {
		aux = *end;
		*end = *str;
		*str = aux;
		end--;
		str++;
	}
}

static const char hex_chars[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z'
};

static const char upcase_hex_chars[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
	'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z'
};

void
toHex(const StaticString &data, char *output, bool upperCase) {
	const char *data_buf = data.c_str();
	string::size_type i;

	if (upperCase) {
		for (i = 0; i < data.size(); i++) {
			output[i * 2] = upcase_hex_chars[(unsigned char) data_buf[i] / 16];
			output[i * 2 + 1] = upcase_hex_chars[(unsigned char) data_buf[i] % 16];
		}
	} else {
		for (i = 0; i < data.size(); i++) {
			output[i * 2] = hex_chars[(unsigned char) data_buf[i] / 16];
			output[i * 2 + 1] = hex_chars[(unsigned char) data_buf[i] % 16];
		}
	}
}

unsigned int
uintSizeAsString(unsigned int value) {
	return integerSizeInOtherBase<unsigned int, 10>(value);
}

unsigned int
uintToString(unsigned int value, char *output, unsigned int outputSize) {
	return integerToOtherBase<unsigned int, 10>(value, output, outputSize);
}

string
integerToHex(long long value) {
	char buf[sizeof(long long) * 2 + 1];
	integerToHex(value, buf);
	return string(buf);
}

string
integerToHexatri(long long value) {
	char buf[sizeof(long long) * 2 + 1];
	integerToHexatri(value, buf);
	return string(buf);
}

bool
looksLikePositiveNumber(const StaticString &str) {
	if (str.empty()) {
		return false;
	} else {
		bool result = true;
		const char *data = str.data();
		const char *end = str.data() + str.size();
		while (result && data != end) {
			result = result && (*data >= '0' && *data <= '9');
			data++;
		}
		return result;
	}
}

int
atoi(const string &s) {
	return ::atoi(s.c_str());
}

long
atol(const string &s) {
	return ::atol(s.c_str());
}

#if !defined(__x86_64__) && !defined(__x86__)
	// x86 and x86_64 optimized version is implemented in StrIntUtilsNoStrictAliasing.cpp.
	void
	convertLowerCase(const unsigned char * restrict data,
		unsigned char * restrict output,
		size_t len)
	{
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

		const unsigned char *end = data + len;
		const size_t imax = len / 4;
		size_t i;

		for (i = 0; i < imax; i++, data += 4, output += 4) {
			output[0] = (unsigned char) gsToLowerMap[data[0]];
			output[1] = (unsigned char) gsToLowerMap[data[1]];
			output[2] = (unsigned char) gsToLowerMap[data[2]];
			output[3] = (unsigned char) gsToLowerMap[data[3]];
		}

		while (data < end) {
			*output = (unsigned char) gsToLowerMap[*data];
			data++;
			output++;
		}
	}
#endif

bool
constantTimeCompare(const StaticString &a, const StaticString &b) {
	// http://blog.jasonmooberry.com/2010/10/constant-time-string-comparison/
	// See also ActiveSupport::MessageVerifier#secure_compare.
	if (a.size() != b.size()) {
		return false;
	} else {
		const char *x = a.data();
		const char *y = b.data();
		const char *end = a.data() + a.size();
		int result = 0;

		while (x < end) {
			result |= *x ^ *y;
			x++;
			y++;
		}

		return result == 0;
	}
}

string
distanceOfTimeInWords(time_t fromTime, time_t toTime) {
	time_t seconds;
	stringstream result;
	if (toTime == 0) {
		toTime = SystemTime::get();
	}
	if (fromTime < toTime) {
		seconds = toTime - fromTime;
	} else {
		seconds = fromTime - toTime;
	}

	if (seconds >= 60) {
		time_t minutes = seconds / 60;
		if (minutes >= 60) {
			time_t hours = minutes / 60;
			if (hours >= 24) {
				time_t days = hours / 24;
				hours = hours % 24;
				result << days << "d ";
			}

			minutes = minutes % 60;
			result << hours << "h ";
		}

		seconds = seconds % 60;
		result << minutes << "m ";
	}
	result << seconds << "s";
	return result.str();
}

unsigned long long
timeToNextMultipleULL(unsigned long long multiple, unsigned long long now) {
	if (now == 0) {
		now = SystemTime::getUsec();
	}
	return multiple - (now % multiple);
}

double
timeToNextMultipleD(unsigned int multiple, double now) {
	assert(multiple != 0);
	return multiple - fmod(now, (double) multiple);
}

char *
appendData(char *pos, const char *end, const char *data, size_t size) {
	size_t maxToCopy = std::min<size_t>(end - pos, size);
	memcpy(pos, data, maxToCopy);
	return pos + size;
}

char *
appendData(char *pos, const char *end, const StaticString &data) {
	return appendData(pos, end, data.data(), data.size());
}

string
cEscapeString(const StaticString &input) {
	string result;
	const char *current = input.c_str();
	const char *end = current + input.size();

	result.reserve(input.size());
	while (current < end) {
		char c = *current;
		if (c >= 32 && c <= 126) {
			// Printable ASCII.
			if (c == '"') {
				result.append("\"");
			} else {
				result.append(1, c);
			}
		} else {
			char buf[sizeof("000")];
			unsigned int size;

			switch (c) {
			case '\t':
				result.append("\\t");
				break;
			case '\n':
				result.append("\\n");
				break;
			case '\r':
				result.append("\\r");
				break;
			case '\e':
				result.append("\\e");
				break;
			default:
				size = integerToOtherBase<unsigned char, 8>(
					*current, buf, sizeof(buf));
				result.append("\\", 1);
				result.append(3 - size, '0');
				result.append(buf, size);
				break;
			}
		}
		current++;
	}
	return result;
}

string
escapeHTML(const StaticString &input) {
	string result;
	result.reserve((int) ceil(input.size() * 1.25));

	const char *current = (const char *) input.c_str();
	const char *end     = current + input.size();

	while (current < end) {
		char ch = *current;
		if (ch & 128) {
			// Multibyte UTF-8 character.
			const char *prev = current;
			try {
				utf8::advance(current, 1, end);
				result.append(prev, current - prev);
			} catch (const utf8::invalid_utf8 &e) {
				result.append("?"); // Oops, not UTF-8 after all, don't parse it.
				current++;
			}
		} else {
			// ASCII character <= 127.
			if (ch == '<') {
				result.append("&lt;");
			} else if (ch == '>') {
				result.append("&gt;");
			} else if (ch == '&') {
				result.append("&amp;");
			} else if (ch == '"') {
				result.append("&quot;");
			} else if (ch == '\'') {
				result.append("&apos;");
			} else if (ch >= 0x21 || ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
				result.append(1, ch);
			} else {
				result.append("&#");
				result.append(toString((int) ((unsigned char) ch)));
				result.append(";");
			}
			current++;
		}
	}
	return result;
}

string
urldecode(const StaticString &url) {
	const char *pos = url.data();
	const char *end = url.data() + url.size();
	string result;

	result.reserve(url.size());

	while (pos < end) {
		switch (*pos) {
		case '%':
			if (end - pos >= 3) {
				unsigned int ch = hexToUint(StaticString(pos + 1, 2));
				result.append(1, ch);
				pos += 3;
			} else {
				throw SyntaxError("Invalid URL encoded string");
			}
			break;
		case '+':
			result.append(1, ' ');
			pos++;
			break;
		default:
			result.append(1, *pos);
			pos++;
			break;
		}
	}

	return result;
}

} // namespace Passenger
