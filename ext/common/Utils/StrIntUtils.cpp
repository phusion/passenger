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

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <Utils/utf8.h>
#include <Exceptions.h>
#include <Utils/StrIntUtils.h>

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

void
split(const string &str, char sep, vector<string> &output) {
	string::size_type start, pos;
	start = 0;
	output.clear();
	while ((pos = str.find(sep, start)) != string::npos) {
		output.push_back(str.substr(start, pos - start));
		start = pos + 1;
	}
	output.push_back(str.substr(start));
}

string toString(const vector<string> &vec) {
	vector<StaticString> vec2;
	vec2.reserve(vec.size());
	for (vector<string>::const_iterator it = vec.begin(); it != vec.end(); it++) {
		vec2.push_back(*it);
	}
	return toString(vec2);
}

string toString(const vector<StaticString> &vec) {
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
pointerToIntString(void *pointer) {
	// Use wierd union construction to avoid compiler warnings.
	if (sizeof(void *) == sizeof(unsigned int)) {
		union {
			void *pointer;
			unsigned int value;
		} u;
		u.pointer = pointer;
		return toString(u.value);
	} else if (sizeof(void *) == sizeof(unsigned long long)) {
		union {
			void *pointer;
			unsigned long long value;
		} u;
		u.pointer = pointer;
		return toString(u.value);
	} else {
		fprintf(stderr, "Pointer size unsupported...\n");
		abort();
	}
}

unsigned long long
stringToULL(const StaticString &str) {
	unsigned long long result = 0;
	string::size_type i = 0;
	const char *data = str.data();
	
	while (data[i] == ' ' && i < str.size()) {
		i++;
	}
	while (data[i] >= '0' && data[i] <= '9' && i < str.size()) {
		result *= 10;
		result += data[i] - '0';
		i++;
	}
	return result;
}

long long
stringToLL(const StaticString &str) {
	long long result = 0;
	string::size_type i = 0;
	const char *data = str.data();
	bool minus = false;
	
	while (data[i] == ' ' && i < str.size()) {
		i++;
	}
	if (data[i] == '-') {
		minus = true;
		i++;
	}
	while (data[i] >= '0' && data[i] <= '9' && i < str.size()) {
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

unsigned long long
hexToULL(const StaticString &hex) {
	unsigned long long result = 0;
	string::size_type i = 0;
	bool done = false;
	
	while (i < hex.size() && !done) {
		char c = hex[i];
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
		i++;
	}
	return result;
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

int
atoi(const string &s) {
	return ::atoi(s.c_str());
}

long
atol(const string &s) {
	return ::atol(s.c_str());
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
			result.append(1, c);
		} else {
			char buf[sizeof("\\xFF")];
			
			switch (c) {
			case '\0':
				// Explicitly in hex format in order to avoid confusion
				// with any '0' characters that come after this byte.
				result.append("\\x00");
				break;
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
				buf[0] = '\\';
				buf[1] = 'x';
				toHex(StaticString(current, 1), buf + 2, true);
				buf[4] = '\0';
				result.append(buf, sizeof(buf) - 1);
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
	result.reserve(input.size() * 1.25);
	
	const char *current = (const char *) input.c_str();
	const char *end     = current + input.size();
	
	while (current < end) {
		char ch = *current;
		if (ch & 128) {
			// Multibyte UTF-8 character.
			const char *prev = current;
			utf8::advance(current, 1, end);
			result.append(prev, current - prev);
			
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

} // namespace Passenger
