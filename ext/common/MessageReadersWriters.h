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
#ifndef _PASSENGER_MESSAGE_READERS_WRITERS_H_
#define _PASSENGER_MESSAGE_READERS_WRITERS_H_

#include <boost/cstdint.hpp>
#include <algorithm>
#include <vector>
#include <string>
#include <sys/types.h>
#include <cstring>
#include <arpa/inet.h>
#include "StaticString.h"

/**
 * This file provides a bunch of classes for reading and writing messages in the
 * MessageChannel format. Unlike MessageChannel, whose operations take control over
 * the I/O handle and may block, these classes are like parsers and data generators.
 * Reader classes require the user to feed data to them. Writer classes generate a
 * bunch of bytes that the user can send out. These classes will never block, making
 * them ideal for use in evented servers.
 *
 * <h2>Reading messages</h2>
 * To read a single message, one must instantiate a reader object and feed network
 * data to it with the feed() method. This method returns the number of bytes
 * actually processed by the reader (i.e. the number of bytes that it has recognized
 * as part of the message).
 *
 * When the reader has either successfully parsed the data or encountered an error,
 * it will indicate so via the done() method. With hasError() one can check whether
 * an error was encountered or whether the reader succeeded, and with errorCode()
 * one can obtain the exact error reason. Not all readers support hasError() and
 * errorCode() because some readers can never encounter errors and some readers
 * only have a single reason to fail.
 *
 * When successful, the parsed message can be obtained with value(). This method
 * may only be called when done() is true and there is no error, otherwise the
 * return value is undefined.
 *
 * At this point, the reader object cannot process any more data and feed() will
 * always return 0. To reuse the object for processing another message, one must
 * reset its state by calling reset().
 *
 * The following example demonstrates how to read a continuous stream of 32-bit
 * integers:
 * @code
   Uint32Reader intReader;
   while (true) {
       // Read a bunch of network data...
       char buf[1024];
       ssize_t size = recv(fd, buf, sizeof(buf));
       size_t consumed = 0;
       
       // ...and process it all. We only feed data to the reader that
       // hasn't already been fed.
       while (consumed < size) {
           consumed += intReader.feed(buf + consumed, size - consumed);
           if (intReader.done()) {
               printf("Integer: %d\n", (int) intReader.value());
               // The state must be reset before the reader can be reused.
               intReader.reset();
           }
       }
   }
 * @endcode
 *
 * Some readers return non-primitive values in their value() methods, such as
 * ArrayReader and ScalarReader which return <tt>const vector<StaticString> &</tt>
 * and <tt>StaticString</tt>, respectively. These values are only valid until either
 * of the following things occur:
 *
 * - The buffer containing last the fed data has been destroyed or modified.
 * - The reader itself has been destroyed.
 *
 * This is because the readers try to apply copy-zero optimizations whenever
 * possible. For example, in case of ScalarReader, it'll check whether the data
 * that has been fed in the first feed() call already contains a full scalar message.
 * If so then it'll just return a StaticString that points to the scalar message
 * in the fed data; it will not copy the fed data. In this case it is important
 * that the buffer containing the fed data is not modified or destroyed while the
 * StaticString is in use.
 * If the first feed() call does not supply a full scalar message then it will
 * buffer all fed data until the buffer contains a full scalar message, and the
 * result will point to this buffer. Because the buffer is owned by the reader,
 * the result will be invalidated as soon as the reader is destroyed.
 */

namespace Passenger {

using namespace std;

/**
 * Class for reading a 16-bit big-endian integer.
 */
class Uint16Reader {
private:
	uint16_t val;
	uint8_t  consumed;
	
public:
	Uint16Reader() {
		consumed = 0;
	}
	
	void reset() {
		consumed = 0;
	}
	
	size_t feed(const char *data, size_t size) {
		size_t locallyConsumed;
		
		locallyConsumed = std::min(size, sizeof(uint16_t) - consumed);
		memcpy(&val + consumed, data, locallyConsumed);
		consumed += locallyConsumed;
		if (done()) {
			val = ntohs(val);
		}
		return locallyConsumed;
	}
	
	bool done() const {
		return consumed == sizeof(uint16_t);
	}
	
	uint16_t value() const {
		return val;
	}
	
	void generate(void *buf, uint16_t val) const {
		val = htons(val);
		memcpy(buf, &val, sizeof(val));
	}
};

/**
 * Class for reading a 32-bit big-endian integer.
 */
class Uint32Reader {
private:
	uint32_t val;
	uint8_t  consumed;
	
public:
	Uint32Reader() {
		consumed = 0;
	}
	
	void reset() {
		consumed = 0;
	}
	
	size_t feed(const char *data, size_t size) {
		size_t locallyConsumed;
		
		locallyConsumed = std::min(size, sizeof(uint32_t) - consumed);
		memcpy(&val + consumed, data, locallyConsumed);
		consumed += locallyConsumed;
		if (done()) {
			val = ntohl(val);
		}
		return locallyConsumed;
	}
	
	bool done() const {
		return consumed == sizeof(uint32_t);
	}
	
	uint32_t value() const {
		return val;
	}
};

/**
 * Class for reading a an array message.
 */
class ArrayReader {
public:
	enum Error {
		TOO_LARGE
	};
	
private:
	enum State {
		READING_HEADER,
		READING_BODY,
		DONE,
		ERROR
	};
	
	uint16_t toReserve;
	uint16_t maxSize;
	Uint16Reader headerReader;
	uint8_t state;
	uint8_t error;
	string buffer;
	vector<StaticString> result;
	
	void parseBody(const char *data, size_t size) {
		const char *start = data;
		const char *terminator;
		size_t rest = size;
		
		while ((terminator = (const char *) memchr(start, '\0', rest)) != NULL) {
			size_t len = terminator - start;
			result.push_back(StaticString(start, len));
			start = terminator + 1;
			rest = size - (start - data);
		}
	}
	
public:
	ArrayReader() {
		state = READING_HEADER;
		toReserve = 0;
		maxSize = 0;
	}
	
	void reserve(uint16_t size) {
		toReserve = size;
		result.reserve(size);
	}
	
	void setMaxSize(uint16_t size) {
		maxSize = size;
	}
	
	void reset() {
		state = READING_HEADER;
		headerReader.reset();
		buffer.clear();
		result.clear();
		if (toReserve > 0) {
			result.reserve(toReserve);
		}
	}
	
	size_t feed(const char *data, size_t size) {
		size_t consumed = 0;
		
		while (consumed < size && !done()) {
			const char *current = data + consumed;
			size_t rest = size - consumed;
			
			switch (state) {
			case READING_HEADER:
				consumed += headerReader.feed(current, rest);
				if (headerReader.done()) {
					if (maxSize > 0 && headerReader.value() > maxSize) {
						state = ERROR;
						error = TOO_LARGE;
					} else {
						state = READING_BODY;
					}
				}
				break;
			case READING_BODY:
				if (buffer.empty() && rest >= headerReader.value()) {
					parseBody(current, headerReader.value());
					state = DONE;
					consumed += headerReader.value();
				} else {
					size_t toConsume = std::min(rest,
						headerReader.value() - buffer.size());
					if (buffer.capacity() < headerReader.value()) {
						buffer.reserve(headerReader.value());
					}
					buffer.append(current, toConsume);
					consumed += toConsume;
					if (buffer.size() == headerReader.value()) {
						parseBody(buffer.data(), buffer.size());
						state = DONE;
					}
				}
				break;
			default:
				// Never reached.
				abort();
			}
		}
		return consumed;
	}
	
	bool done() const {
		return state == DONE || state == ERROR;
	}
	
	bool hasError() const {
		return state == ERROR;
	}
	
	Error errorCode() const {
		return (Error) error;
	}
	
	const vector<StaticString> &value() const {
		return result;
	}
};

/**
 * Class for reading a scalar message.
 */
class ScalarReader {
public:
	enum Error {
		TOO_LARGE
	};
	
private:
	enum State {
		READING_HEADER,
		READING_BODY,
		DONE,
		ERROR
	};
	
	uint8_t state;
	uint8_t error;
	uint32_t maxSize;
	Uint32Reader headerReader;
	string buffer;
	StaticString result;
	
public:
	ScalarReader(uint32_t maxSize = 0) {
		state = READING_HEADER;
		this->maxSize = maxSize;
	}
	
	void reset() {
		state = READING_HEADER;
		headerReader.reset();
		buffer.clear();
	}
	
	size_t feed(const char *data, size_t size) {
		size_t consumed = 0;
		
		while (consumed < size && !done()) {
			const char *current = data + consumed;
			size_t rest = size - consumed;
			
			switch (state) {
			case READING_HEADER:
				consumed += headerReader.feed(current, rest);
				if (headerReader.done()) {
					if (maxSize > 0 && headerReader.value() > maxSize) {
						state = ERROR;
						error = TOO_LARGE;
					} else {
						state = READING_BODY;
					}
				}
				break;
			case READING_BODY:
				if (buffer.empty() && rest >= headerReader.value()) {
					result = StaticString(current, headerReader.value());
					state = DONE;
					consumed += headerReader.value();
				} else {
					size_t toConsume = std::min(rest,
						headerReader.value() - buffer.size());
					if (buffer.capacity() < headerReader.value()) {
						buffer.reserve(headerReader.value());
					}
					buffer.append(current, toConsume);
					consumed += toConsume;
					if (buffer.size() == headerReader.value()) {
						result = StaticString(buffer);
						state = DONE;
					}
				}
				break;
			default:
				// Never reached.
				abort();
			};
		}
		return consumed;
	}
	
	bool done() const {
		return state == DONE || state == ERROR;
	}
	
	bool hasError() const {
		return state == ERROR;
	}
	
	Error errorCode() const {
		return (Error) error;
	}
	
	StaticString value() const {
		return result;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_READERS_WRITERS_H_ */
