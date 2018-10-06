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
#ifndef _PASSENGER_IOTOOLS_BUFFERED_IO_H_
#define _PASSENGER_IOTOOLS_BUFFERED_IO_H_

#include <string>
#include <utility>
#include <algorithm>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/macros.hpp>
#include <sys/types.h>
#include <cstring>
#include <FileDescriptor.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <IOTools/IOUtils.h>

namespace Passenger {

using namespace std;
using namespace oxt;


/**
 * Provides buffered I/O for arbitrary file descriptors. Supports features not
 * found in C++'s iostream or libc's stdio:
 * - All functions have timeout support.
 * - The readLine() method returns a C++ string, so no need to worry about
 *   buffer management. A size limit can be imposed.
 * - Read buffer is infinite in size.
 * - Unreading (pushing back) arbitrary amount of data.
 */
class BufferedIO {
private:
	FileDescriptor fd;
	string buffer;

	static pair<unsigned int, bool> nReadOrEofReached(const char *data,
		unsigned int size, void *output, unsigned int goalSize, unsigned int *alreadyRead)
	{
		char *current = (char *) output + *alreadyRead;
		unsigned int remaining = goalSize - *alreadyRead;
		unsigned int consumed = min(remaining, size);
		memcpy(current, data, consumed);
		*alreadyRead += consumed;
		return make_pair(consumed, *alreadyRead == goalSize);
	}

	static pair<unsigned int, bool> eofReached(const char *data,
		unsigned int size, string *output)
	{
		output->append(data, size);
		return make_pair(size, false);
	}

	static pair<unsigned int, bool> newlineFound(const char *data,
		unsigned int size, string *output, unsigned int max)
	{
		const char *newline = (const char *) memchr(data, '\n', size);
		if (newline != NULL) {
			unsigned int accepted = newline - data + 1;
			if (output->size() + accepted > max) {
				throw SecurityException("Line too long");
			}
			output->append(data, accepted);
			return make_pair(accepted, true);
		} else {
			if (output->size() + size > max) {
				throw SecurityException("Line too long");
			}
			output->append(data, size);
			return make_pair(size, false);
		}
	}

public:
	typedef boost::function< pair<unsigned int, bool>(const char *data, unsigned int size) > AcceptFunction;

	BufferedIO() { }

	BufferedIO(const FileDescriptor &_fd)
		: fd(_fd)
		{ }

	FileDescriptor getFd() const {
		return fd;
	}

	const string &getBuffer() const {
		return buffer;
	}

	/**
	 * This method keeps reading data in a loop, feeding each chunk to the given
	 * acceptor function, until the function says that it has consumed all data
	 * that it needs. Leftover data that has been read from the file descriptor
	 * but not consumed by the acceptor function will be put in the buffer, making
	 * it available for future read operations.
	 *
	 * The acceptor function accepts (data, size) as arguments and returns a
	 * (consumed, done) pair, where 'consumed' indicates the number of bytes
	 * from 'data' that it has consumed. 'done' indicates whether the acceptor
	 * function is done consuming (true), or whether it expects more data (false).
	 *
	 * readUntil() can be used for e.g. reading data until a newline is encountered.
	 *
	 * If the acceptor function throws an exception then the BufferedIO instance
	 * will be left in an undefined state, making it unusable.
	 *
	 * @throws RuntimeException The acceptor function returned an invalid result.
	 * @throws SystemException
	 * @throws TimeoutException
	 * @throws boost::thread_interrupted
	 */
	unsigned int readUntil(const AcceptFunction &acceptor, unsigned long long *timeout = NULL) {
		pair<unsigned int, bool> acceptResult;
		unsigned int totalRead = 0;

		if (!buffer.empty()) {
			acceptResult = acceptor(buffer.c_str(), buffer.size());
			if (OXT_UNLIKELY(!acceptResult.second && acceptResult.first < buffer.size())) {
				throw RuntimeException("Acceptor function cannot return (x,false) where x is smaller than the input size");
			} else if (OXT_UNLIKELY(acceptResult.first > buffer.size())) {
				throw RuntimeException("Acceptor function cannot return a larger accept count than the input size");
			}
			buffer.erase(0, acceptResult.first);
			totalRead = acceptResult.first;
			if (acceptResult.second) {
				return totalRead;
			}
		}

		while (true) {
			if (OXT_UNLIKELY(timeout != NULL && !waitUntilReadable(fd, timeout))) {
				throw TimeoutException("Read timeout");
			}

			char tmp[1024 * 8];
			ssize_t ret = syscalls::read(fd, tmp, sizeof(tmp));
			if (ret == 0) {
				return totalRead;
			} else if (OXT_UNLIKELY(ret == -1)) {
				if (errno != EAGAIN) {
					int e = errno;
					throw SystemException("read() failed", e);
				}
			} else {
				acceptResult = acceptor(tmp, ret);
				totalRead += acceptResult.first;
				if (OXT_UNLIKELY(!acceptResult.second && acceptResult.first < (unsigned int) ret)) {
					throw RuntimeException("Acceptor function cannot return (x,false) where x is smaller than the input size");
				} else if (OXT_UNLIKELY(acceptResult.first > (unsigned int) ret)) {
					throw RuntimeException("Acceptor function cannot return a larger accept count than the input size");
				}
				if (acceptResult.second) {
					buffer.assign(tmp + acceptResult.first, ret - acceptResult.first);
					return totalRead;
				}
			}
		}
	}

	unsigned int read(void *buf, unsigned int size, unsigned long long *timeout = NULL) {
		unsigned int counter = 0;
		return readUntil(
			boost::bind(nReadOrEofReached,
				boost::placeholders::_1,
				boost::placeholders::_2,
				buf,
				size,
				&counter),
			timeout);
	}

	string readAll(unsigned long long *timeout = NULL) {
		string output;
		readUntil(
			boost::bind(eofReached,
				boost::placeholders::_1,
				boost::placeholders::_2,
				&output),
			timeout);
		return output;
	}

	/**
	 * Reads a line and returns the line including the newline character. Upon
	 * encountering EOF, the empty string is returned.
	 *
	 * The `max` parameter dictates the maximum length of the returned line.
	 * If the line is longer than this number of characters, then a SecurityException
	 * is thrown, and the BufferedIO becomes unusable (enters an undefined state).
	 *
	 * @throws SystemException
	 * @throws TimeoutException
	 * @throws SecurityException
	 * @throws boost::thread_interrupted
	 */
	string readLine(unsigned int max = 1024 * 8, unsigned long long *timeout = NULL) {
		string output;
		readUntil(
			boost::bind(newlineFound,
				boost::placeholders::_1,
				boost::placeholders::_2,
				&output,
				max),
			timeout);
		return output;
	}

	void unread(const void *buf, unsigned int size) {
		string newBuffer;
		newBuffer.reserve(size + buffer.size());
		newBuffer.append((const char *) buf, (string::size_type) size);
		newBuffer.append(buffer);
		buffer = newBuffer;
	}

	void unread(const StaticString &str) {
		unread(str.c_str(), str.size());
	}
};


} // namespace Passenger

#endif /* _PASSENGER_IOTOOLS_BUFFERED_IO_H_ */
