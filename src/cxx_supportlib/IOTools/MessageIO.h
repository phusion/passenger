/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_IOTOOLS_MESSAGE_IO_H_
#define _PASSENGER_IOTOOLS_MESSAGE_IO_H_

/**
 * This file contains functions for reading and writing structured messages over
 * I/O channels. Supported message types are as follows.
 *
 * == 16-bit and 32-bit integers
 * Their raw formats are binary, in big endian.
 *
 * == Array of strings (array messages)
 * Each string may contain arbitrary data except for the NUL byte.
 * Its raw format consists of a 16-bit big endian size header
 * and a body containing all the strings in the array, each terminated
 * by a NUL byte. The size header specifies the raw size of the body.
 *
 * == Arbitary binary strings (scalar messages)
 * Its raw format consists of a 32-bit big endian size header
 * followed by the raw string data.
 *
 * == File descriptor passing and negotiation
 * Unix socket file descriptor passing is not safe without some kind
 * of negotiation protocol. If one side passes a file descriptor, and
 * the other side accidentally read()s past the normal data then it
 * will read away the passed file descriptor too without actually
 * receiving it.
 *
 * For example suppose that side A looks like this:
 *
 *   read(fd, buf, 1024)
 *   read_io(fd)
 *
 * and side B:
 *
 *   write(fd, buf, 100)
 *   send_io(fd_to_pass)
 *
 * If B completes both write() and send_io(), then A's read() call
 * reads past the 100 bytes that B sent. On some platforms, like
 * Linux, this will cause read_io() to fail. And it just so happens
 * that Ruby's IO#read method slurps more than just the given amount
 * of bytes.
 *
 * In order to solve this problem, we wrap the actual file descriptor
 * passing/reading code into a negotiation protocol to ensure that
 * this situation can never happen.
 */

// For ntohl/htonl/ntohs/htons.
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <cstdarg>

#include <boost/cstdint.hpp>
#include <boost/bind.hpp>
#include <boost/scoped_array.hpp>

#include <oxt/macros.hpp>

#include <StaticString.h>
#include <Exceptions.h>
#include <SecurityKit/MemZeroGuard.h>
#include <Utils/ScopeGuard.h>
#include <IOTools/IOUtils.h>
#include <StrIntTools/StrIntUtils.h>


namespace Passenger {

using namespace std;
using namespace boost;

/**
 * Reads a 16-bit unsigned integer from the given file descriptor. The result
 * is put into 'output'.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @return True if reading was successful, false if end-of-file was prematurely reached.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline bool
readUint16(int fd, boost::uint16_t &output, unsigned long long *timeout = NULL) {
	boost::uint16_t temp;

	if (readExact(fd, &temp, sizeof(boost::uint16_t), timeout) == sizeof(boost::uint16_t)) {
		output = ntohs(temp);
		return true;
	} else {
		return false;
	}
}

/**
 * Reads a 16-bit unsigned integer from the given file descriptor.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws EOFException End-of-file was reached before a full integer could be read.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline boost::uint16_t
readUint16(int fd, unsigned long long *timeout = NULL) {
	boost::uint16_t temp;

	if (readUint16(fd, temp, timeout)) {
		return temp;
	} else {
		throw EOFException("EOF encountered before a full 16-bit integer could be read");
	}
}

/**
 * Reads a 32-bit unsigned integer from the given file descriptor. The result
 * is put into 'output'.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @return True if reading was successful, false if end-of-file was prematurely reached.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline bool
readUint32(int fd, boost::uint32_t &output, unsigned long long *timeout = NULL) {
	boost::uint32_t temp;

	if (readExact(fd, &temp, sizeof(boost::uint32_t), timeout) == sizeof(boost::uint32_t)) {
		output = ntohl(temp);
		return true;
	} else {
		return false;
	}
}

/**
 * Reads a 32-bit unsigned integer from the given file descriptor.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws EOFException End-of-file was reached before a full integer could be read.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline boost::uint32_t
readUint32(int fd, unsigned long long *timeout = NULL) {
	boost::uint32_t temp;

	if (readUint32(fd, temp, timeout)) {
		return temp;
	} else {
		throw EOFException("EOF encountered before a full 32-bit integer could be read");
	}
}


/**
 * Reads an array message from the given file descriptor. This version
 * puts the result into the given collection instead of returning a
 * new collection.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @return True if an array message was read, false if end-of-file was reached
 *         before a full array message could be read.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
template<typename Collection>
inline bool
readArrayMessage(int fd, Collection &output, unsigned long long *timeout = NULL) {
	boost::uint16_t size;
	if (!readUint16(fd, size, timeout)) {
		return false;
	}

	scoped_array<char> buffer(new char[size]);
	MemZeroGuard g(buffer.get(), size);
	if (readExact(fd, buffer.get(), size, timeout) != size) {
		return false;
	}

	output.clear();
	if (size != 0) {
		string::size_type start = 0, pos;
		StaticString buffer_str(buffer.get(), size);
		while ((pos = buffer_str.find('\0', start)) != string::npos) {
			output.push_back(buffer_str.substr(start, pos - start));
			start = pos + 1;
		}
	}
	return true;
}

/**
 * Reads an array message from the given file descriptor. This version returns
 * the result immediately as a string vector.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws EOFException End-of-file was reached before a full integer could be read.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline vector<string>
readArrayMessage(int fd, unsigned long long *timeout = NULL) {
	vector<string> output;

	if (readArrayMessage(fd, output, timeout)) {
		return output;
	} else {
		throw EOFException("EOF encountered before the full array message could be read");
	}
}


/**
 * Reads a scalar message from the given file descriptor.
 *
 * @param maxSize The maximum number of bytes that may be read. If the
 *                scalar to read is larger than this, then a SecurityException
 *                will be thrown. Set to 0 for no size limit.
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @return True if a scalar message was read, false if EOF was encountered.
 * @throws SystemException Something went wrong.
 * @throws SecurityException The message body is larger than allowed by maxSize.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline bool
readScalarMessage(int fd, string &output, unsigned int maxSize = 0, unsigned long long *timeout = NULL) {
	boost::uint32_t size;
	if (!readUint32(fd, size, timeout)) {
		return false;
	}

	if (maxSize != 0 && size > (boost::uint32_t) maxSize) {
		throw SecurityException("The scalar message body is larger than the size limit");
	}

	unsigned int remaining = size;
	if (OXT_UNLIKELY(!output.empty())) {
		output.clear();
	}
	output.reserve(size);
	if (OXT_LIKELY(remaining > 0)) {
		char buf[1024 * 32];
		MemZeroGuard g(buf, sizeof(buf));

		while (remaining > 0) {
			unsigned int blockSize = min((unsigned int) sizeof(buf), remaining);

			if (readExact(fd, buf, blockSize, timeout) != blockSize) {
				return false;
			}
			output.append(buf, blockSize);
			remaining -= blockSize;
		}
	}
	return true;
}

/**
 * Reads a scalar message from the given file descriptor.
 *
 * @param maxSize The maximum number of bytes that may be read. If the
 *                scalar to read is larger than this, then a SecurityException
 *                will be thrown. Set to 0 for no size limit.
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws EOFException End-of-file was reached before a full integer could be read.
 * @throws SystemException Something went wrong.
 * @throws SecurityException The message body is larger than allowed by maxSize.
 * @throws TimeoutException Unable to read the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline string
readScalarMessage(int fd, unsigned int maxSize = 0, unsigned long long *timeout = NULL) {
	string output;
	if (readScalarMessage(fd, output, maxSize, timeout)) {
		return output;
	} else {
		throw EOFException("EOF encountered before a full scalar message could be read");
	}
}


/**
 * Writes a 16-bit unsigned integer to the given file descriptor.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on writing the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to write the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline void
writeUint16(int fd, boost::uint16_t value, unsigned long long *timeout = NULL) {
	boost::uint16_t l = htons(value);
	writeExact(fd, &l, sizeof(boost::uint16_t), timeout);
}

/**
 * Writes a 32-bit unsigned integer to the given file descriptor.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on writing the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to write the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline void
writeUint32(int fd, boost::uint32_t value, unsigned long long *timeout = NULL) {
	boost::uint32_t l = htonl(value);
	writeExact(fd, &l, sizeof(boost::uint32_t), timeout);
}


/**
 * Writes an array message to the given file descriptor.
 *
 * @param args A collection of strings containing the array message's elements.
 *             The collection must have an STL container-like interface and
 *             the strings must have an STL string-like interface.
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on writing the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to write the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
template<typename Collection>
inline void
writeArrayMessageEx(int fd, const Collection &args, unsigned long long *timeout = NULL) {
	typename Collection::const_iterator it, end = args.end();
	boost::uint16_t bodySize = 0;

	for (it = args.begin(); it != end; it++) {
		bodySize += it->size() + 1;
	}

	scoped_array<char> data(new char[sizeof(boost::uint16_t) + bodySize]);
	boost::uint16_t header = htons(bodySize);
	memcpy(data.get(), &header, sizeof(boost::uint16_t));

	char *dataEnd = data.get() + sizeof(boost::uint16_t);
	for (it = args.begin(); it != end; it++) {
		memcpy(dataEnd, it->data(), it->size());
		dataEnd += it->size();
		*dataEnd = '\0';
		dataEnd++;
	}

	writeExact(fd, data.get(), sizeof(boost::uint16_t) + bodySize, timeout);
}

inline void
writeArrayMessage(int fd, const vector<StaticString> &args, unsigned long long *timeout = NULL) {
	writeArrayMessageEx(fd, args, timeout);
}

inline void
writeArrayMessage(int fd, const vector<string> &args, unsigned long long *timeout = NULL) {
	writeArrayMessageEx(fd, args, timeout);
}

inline void
writeArrayMessage(int fd, const StaticString args[], unsigned int nargs, unsigned long long *timeout = NULL) {
	unsigned int i;
	boost::uint16_t bodySize = 0;

	for (i = 0; i < nargs; i++) {
		bodySize += args[i].size() + 1;
	}

	scoped_array<char> data(new char[sizeof(boost::uint16_t) + bodySize]);
	boost::uint16_t header = htons(bodySize);
	memcpy(data.get(), &header, sizeof(boost::uint16_t));

	char *dataEnd = data.get() + sizeof(boost::uint16_t);
	for (i = 0; i < nargs; i++) {
		memcpy(dataEnd, args[i].data(), args[i].size());
		dataEnd += args[i].size();
		*dataEnd = '\0';
		dataEnd++;
	}

	writeExact(fd, data.get(), sizeof(boost::uint16_t) + bodySize, timeout);
}

inline void
writeArrayMessageVA(int fd, const StaticString &name, va_list &ap, unsigned long long *timeout = NULL) {
	StaticString args[10];
	unsigned int nargs = 1;
	bool done = false;

	args[0] = name;
	do {
		const char *arg = va_arg(ap, const char *);
		if (arg == NULL) {
			done = true;
		} else {
			args[nargs] = arg;
			nargs++;
		}
	} while (!done && nargs < sizeof(args) / sizeof(StaticString));

	if (done) {
		writeArrayMessage(fd, args, nargs, timeout);
	} else {
		// Arguments don't fit in static array. Use dynamic
		// array instead.
		vector<StaticString> dyn_args;

		for (unsigned int i = 0; i < nargs; i++) {
			dyn_args.push_back(args[i]);
		}
		do {
			const char *arg = va_arg(ap, const char *);
			if (arg == NULL) {
				done = true;
			} else {
				dyn_args.push_back(arg);
			}
		} while (!done);

		writeArrayMessage(fd, dyn_args, timeout);
	}
}

struct _VaGuard {
	va_list &ap;

	_VaGuard(va_list &_ap)
		: ap(_ap)
		{ }

	~_VaGuard() {
		va_end(ap);
	}
};

/** Version of writeArrayMessage() that accepts a variadic list of 'const char *'
 * arguments as message elements. The list must be terminated with a NULL.
 */
inline void
writeArrayMessage(int fd, const char *name, ...) {
	va_list ap;
	va_start(ap, name);
	_VaGuard guard(ap);
	writeArrayMessageVA(fd, name, ap);
}

/** Version of writeArrayMessage() that accepts a variadic list of 'const char *'
 * arguments as message elements, with timeout support. The list must be terminated
 * with a NULL.
 */
inline void
writeArrayMessage(int fd, unsigned long long *timeout, const char *name, ...) {
	va_list ap;
	va_start(ap, name);
	_VaGuard guard(ap);
	writeArrayMessageVA(fd, name, ap, timeout);
}

/**
 * Writes a scalar message to the given file descriptor.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on writing the necessary data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to write the necessary data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline void
writeScalarMessage(int fd, const StaticString &data, unsigned long long *timeout = NULL) {
	boost::uint32_t header = htonl(data.size());
	StaticString buffers[2] = {
		StaticString((const char *) &header, sizeof(boost::uint32_t)),
		data
	};
	gatheredWrite(fd, buffers, 2, timeout);
}

inline void
writeScalarMessage(int fd, const char *data, size_t size, unsigned long long *timeout = NULL) {
	writeScalarMessage(fd, StaticString(data, size), timeout);
}


/**
 * Receive a file descriptor over the given Unix domain socket,
 * involving a negotiation protocol.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on receiving the file descriptor.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on receiving will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @return The received file descriptor.
 * @throws SystemException Something went wrong.
 * @throws IOException Whatever was received doesn't seem to be a
 *                     file descriptor.
 * @throws TimeoutException Unable to receive a file descriptor within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline int
readFileDescriptorWithNegotiation(int fd, unsigned long long *timeout = NULL) {
	writeArrayMessage(fd, timeout, "pass IO", NULL);
	int result = readFileDescriptor(fd, timeout);
	ScopeGuard guard(boost::bind(safelyClose, result, false));
	writeArrayMessage(fd, timeout, "got IO", NULL);
	guard.clear();
	return result;
}


/**
 * Pass the file descriptor 'fdToSend' over the Unix socket 'fd',
 * involving a negotiation protocol.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on trying to pass the file descriptor.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to pass the file descriptor within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
inline void
writeFileDescriptorWithNegotiation(int fd, int fdToPass, unsigned long long *timeout = NULL) {
	vector<string> args;

	args = readArrayMessage(fd, timeout);
	if (args.size() != 1 || args[0] != "pass IO") {
		throw IOException("FD passing pre-negotiation message expected");
	}

	writeFileDescriptor(fd, fdToPass, timeout);

	args = readArrayMessage(fd, timeout);
	if (args.size() != 1 || args[0] != "got IO") {
		throw IOException("FD passing post-negotiation message expected.");
	}
}


} // namespace Passenger

#endif /* _PASSENGER_IOTOOLS_MESSAGE_IO_H_ */
