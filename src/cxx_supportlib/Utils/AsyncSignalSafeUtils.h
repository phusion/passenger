/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_ASYNC_SIGNAL_SAFE_UTILS_H_
#define _PASSENGER_ASYNC_SIGNAL_SAFE_UTILS_H_

#include <cstdio>
#include <cerrno>
#include <unistd.h>

// We reimplement libc and some of our own utility functions here
// in an async signal-safe manner.

namespace Passenger {
namespace AsyncSignalSafeUtils {

using namespace std;


inline size_t
strlen(const char *str) {
	size_t size = 0;
	while (*str != '\0') {
		str++;
		size++;
	}
	return size;
}

// Just like the normal memcpy(), dest and src may not overlap.
inline void *
memcpy(void *dest, const void *src, size_t n) {
	for (size_t i = 0; i < n; i++) {
		((char *) dest)[i] = ((const char *) src)[i];
	}
	return dest;
}

// In an async-signal-safe environment, there's nothing we can do if we fail to
// write to stderr, so we ignore its return value and we ignore compiler
// warnings about ignoring that.
inline void
writeNoWarn(int fd, const void *buf, size_t n) {
	ssize_t ret = ::write(fd, buf, n);
	(void) ret;
}

inline void
printError(const char *message, size_t len = (size_t) -1) {
	if (len == (size_t) -1) {
		len = strlen(message);
	}
	writeNoWarn(STDERR_FILENO, message, len);
}

inline void
reverseString(char *str, size_t len) {
	char *p1, *p2;
	if (len > 0 && *str == '\0') {
		return;
	}
	for (p1 = str, p2 = str + len - 1; p2 > p1; ++p1, --p2) {
		*p1 ^= *p2;
		*p2 ^= *p1;
		*p1 ^= *p2;
	}
}

/**
 * Convert the given integer to some other radix, placing
 * the result into the given output buffer. The output buffer
 * will be NULL terminated. Supported radices are 2-36.
 *
 * @param outputSize The size of the output buffer, including space for
 *                   the terminating NULL.
 * @return The size of the created string excluding terminating NULL,
 *         or 0 if the output buffer is not large enough.
 */
template<typename IntegerType, int radix>
size_t
integerToOtherBase(IntegerType value, char *output, size_t outputSize) {
	static const char chars[] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
		'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
		'u', 'v', 'w', 'x', 'y', 'z'
	};
	IntegerType remainder = value;
	unsigned int size = 0;

	do {
		output[size] = chars[remainder % radix];
		remainder = remainder / radix;
		size++;
	} while (remainder != 0 && size < outputSize - 1);

	if (remainder == 0) {
		reverseString(output, size);
		output[size] = '\0';
		return size;
	} else {
		return 0;
	}
}

inline char *
appendData(char *pos, const char *end, const char *data, size_t size = (size_t) -1) {
	size_t maxToCopy;
	if (size == (size_t) -1) {
		size = strlen(data);
	}
	if (size_t(end - pos) < size) {
		maxToCopy = end - pos;
	} else {
		maxToCopy = size;
	}
	memcpy(pos, data, maxToCopy);
	return pos + size;
}

template<typename IntegerType, int radix>
inline char *
appendInteger(char *pos, const char *end, IntegerType value) {
	return pos + integerToOtherBase<IntegerType, radix>(value, pos, end - pos);
}

/**
 * Like `strerror()`, but only supports a limited number of errno codes.
 * If the errno code is not supported then it returns `defaultResult`.
 */
inline const char *
limitedStrerror(int e, const char *defaultResult = "Unknown error") {
	switch (e) {
	case E2BIG:
		return "Argument list too long";
	case EACCES:
		return "Permission denied";
	case EFAULT:
		return "Bad address";
	case EINVAL:
		return "Invalid argument";
	case EIO:
		return "Input/output error";
	case EISDIR:
		return "Is a directory";
	#ifdef ELIBBAD
		case ELIBBAD:
			return "Accessing a corrupted shared library";
	#endif
	case ELOOP:
		return "Too many levels of symbolic links";
	case EMFILE:
		return "Too many open files";
	case ENAMETOOLONG:
		return "File name too long";
	case ENFILE:
		return "Too many open files in system";
	case ENOENT:
		return "No such file or directory";
	case ENOEXEC:
		return "Exec format error";
	case ENOMEM:
		return "Cannot allocate memory";
	case ENOTDIR:
		return "Not a directory";
	case EPERM:
		return "Operation not permitted";
	case ETXTBSY:
		return "Text file busy";
	default:
		return defaultResult;
	}
}


} // namespace AsyncSignalSafeUtils
} // namespace Passenger

#endif /* _PASSENGER_ASYNC_SIGNAL_SAFE_UTILS_H_ */
