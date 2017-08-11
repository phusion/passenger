/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_CHUNKED_BODY_PARSER_H_
#define _PASSENGER_SERVER_KIT_CHUNKED_BODY_PARSER_H_

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstring>

#include <ServerKit/Errors.h>
#include <ServerKit/HttpChunkedBodyParserState.h>

namespace Passenger {
namespace ServerKit {

using namespace std;


#define CBP_DEBUG(expr) \
	do { \
		if (OXT_UNLIKELY(Passenger::LoggingKit::getLevel() >= Passenger::LoggingKit::DEBUG3)) { \
			char _buf[256]; \
			unsigned int size = loggingPrefixFormatter(_buf, sizeof(_buf), userData); \
			P_TRACE(3, StaticString(_buf, size) << expr); \
		} \
	} while (false)


struct HttpChunkedEvent {
	enum Type {
		NONE,
		DATA,
		END,
		ERROR
	};

	unsigned int consumed;
	int errcode;
	Type type;
	bool end;
	MemoryKit::mbuf data;

	HttpChunkedEvent() { }

	HttpChunkedEvent(Type _type, unsigned int _consumed, bool _end)
		: consumed(_consumed),
		  errcode(0),
		  type(_type),
		  end(_end)
		{ }

	HttpChunkedEvent(Type _type, const MemoryKit::mbuf &_data, unsigned int _consumed, bool _end)
		: consumed(_consumed),
		  errcode(0),
		  type(_type),
		  end(_end),
		  data(_data)
		{ }

	HttpChunkedEvent(Type _type, int _errcode, unsigned int _consumed, bool _end)
		: consumed(_consumed),
		  errcode(_errcode),
		  type(_type),
		  end(_end)
		{ }
};

/**
 * Parses data in HTTP/1.1 chunked transfer encoding.
 *
 * This is a POD struct so that we can put it in a union.
 */
class HttpChunkedBodyParser {
public:
	typedef unsigned int (*LoggingPrefixFormatter)(char *buf, unsigned int bufsize, void *userData);

private:
	HttpChunkedBodyParserState *state;
	LoggingPrefixFormatter loggingPrefixFormatter;
	void *userData;

	static bool isHexDigit(char ch) {
		return (ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'f')
			|| (ch >= 'A' && ch <= 'F');
	}

	static int parseHexDigit(char ch) {
		if (ch >= 'A' && ch <= 'F') {
			return 10 + ch - 'A';
		} else if (ch >= 'a' && ch <= 'z') {
			return 10 + ch - 'a';
		} else {
			return ch - '0';
		}
	}

	void logChunkSize() {
		CBP_DEBUG("chunk size determined: " << state->remainingDataSize << " bytes");
	}

	HttpChunkedEvent setError(int errcode, const char *bufferStart, const char *current) {
		CBP_DEBUG("setting error: " << getErrorDesc(errcode));
		state->state = HttpChunkedBodyParserState::ERROR;
		return HttpChunkedEvent(HttpChunkedEvent::ERROR, errcode,
			current - bufferStart, true);
	}

public:
	HttpChunkedBodyParser(HttpChunkedBodyParserState *_state,
		LoggingPrefixFormatter formatter, void *_userData)
		: state(_state),
		  loggingPrefixFormatter(formatter),
		  userData(_userData)
		{ }

	void initialize() {
		state->state = HttpChunkedBodyParserState::EXPECTING_SIZE_FIRST_DIGIT;
	}

	HttpChunkedEvent feed(const MemoryKit::mbuf &buffer, bool outputDataEvents = true) {
		// Calling feed() on channels could result in the request being
		// ended, which modifies the buffer. So we cache the original
		// buffer start address here.
		const char *current  = buffer.start;
		const char *end      = buffer.start + buffer.size();
		const char *needle;
		size_t dataSize;

		assert(!buffer.empty());

		while (current < end) {
			switch (state->state) {
			case HttpChunkedBodyParserState::EXPECTING_DATA:
				dataSize = std::min<size_t>(state->remainingDataSize, end - current);
				CBP_DEBUG("parsing " << dataSize << " of " << state->remainingDataSize <<
					" bytes of remaining chunk data; " <<
					(state->remainingDataSize - dataSize) << " now remaining");
				if (dataSize == 0) {
					CBP_DEBUG("end chunk detected");
					state->state = HttpChunkedBodyParserState::EXPECTING_FINAL_CR;
					break;
				} else {
					state->remainingDataSize -= (unsigned int) dataSize;
					if (state->remainingDataSize == 0) {
						state->state = HttpChunkedBodyParserState::EXPECTING_NON_FINAL_CR;
					}
					if (outputDataEvents) {
						return HttpChunkedEvent(HttpChunkedEvent::DATA,
							MemoryKit::mbuf(buffer, current - buffer.start, dataSize),
							current + dataSize - buffer.start, false);
					} else {
						current += dataSize;
						break;
					}
				}

			case HttpChunkedBodyParserState::EXPECTING_SIZE_FIRST_DIGIT:
				CBP_DEBUG("parsing new chunk");
				if (isHexDigit(*current)) {
					state->remainingDataSize = parseHexDigit(*current);
					state->state = HttpChunkedBodyParserState::EXPECTING_SIZE;
					current++;
					break;
				} else {
					return setError(CHUNK_SIZE_PARSE_ERROR, buffer.start, current);
				}

			case HttpChunkedBodyParserState::EXPECTING_SIZE:
				if (isHexDigit(*current)) {
					if (state->remainingDataSize >= HttpChunkedBodyParserState::MAX_CHUNK_SIZE) {
						return setError(CHUNK_SIZE_TOO_LARGE, buffer.start, current);
					} else {
						state->remainingDataSize = 16 * state->remainingDataSize +
							parseHexDigit(*current);
						current++;
					}
				} else if (*current == HttpChunkedBodyParserState::CR) {
					logChunkSize();
					state->state = HttpChunkedBodyParserState::EXPECTING_HEADER_LF;
					current++;
				} else if (*current == ';') {
					logChunkSize();
					CBP_DEBUG("parsing chunk extension");
					state->state = HttpChunkedBodyParserState::EXPECTING_CHUNK_EXTENSION;
					current++;
				} else {
					return setError(CHUNK_SIZE_PARSE_ERROR, buffer.start, current);
				}
				break;

			case HttpChunkedBodyParserState::EXPECTING_CHUNK_EXTENSION:
				needle = (const char *) memchr(current, HttpChunkedBodyParserState::CR,
					end - current);
				if (needle == NULL) {
					return HttpChunkedEvent(HttpChunkedEvent::NONE, buffer.size(), false);
				} else {
					CBP_DEBUG("done parsing chunk extension");
					state->state = HttpChunkedBodyParserState::EXPECTING_HEADER_LF;
					current = needle + 1;
					break;
				}

			case HttpChunkedBodyParserState::EXPECTING_HEADER_LF:
				if (*current == HttpChunkedBodyParserState::LF) {
					state->state = HttpChunkedBodyParserState::EXPECTING_DATA;
					current++;
					break;
				} else {
					return setError(CHUNK_SIZE_PARSE_ERROR, buffer.start, current);
				}

			case HttpChunkedBodyParserState::EXPECTING_NON_FINAL_CR:
				if (*current == HttpChunkedBodyParserState::CR) {
					state->state = HttpChunkedBodyParserState::EXPECTING_NON_FINAL_LF;
					current++;
					break;
				} else {
					return setError(CHUNK_FOOTER_PARSE_ERROR, buffer.start, current);
				}

			case HttpChunkedBodyParserState::EXPECTING_NON_FINAL_LF:
				if (*current == HttpChunkedBodyParserState::LF) {
					CBP_DEBUG("done parsing a chunk");
					state->state = HttpChunkedBodyParserState::EXPECTING_SIZE_FIRST_DIGIT;
					current++;
					break;
				} else {
					return setError(CHUNK_FOOTER_PARSE_ERROR, buffer.start, current);
				}

			case HttpChunkedBodyParserState::EXPECTING_FINAL_CR:
				if (*current == HttpChunkedBodyParserState::CR) {
					state->state = HttpChunkedBodyParserState::EXPECTING_FINAL_LF;
					current++;
					break;
				} else {
					return setError(CHUNK_FINALIZER_PARSE_ERROR, buffer.start, current);
				}

			case HttpChunkedBodyParserState::EXPECTING_FINAL_LF:
				if (*current == HttpChunkedBodyParserState::LF) {
					CBP_DEBUG("end chunk reached");
					state->state = HttpChunkedBodyParserState::DONE;
					return HttpChunkedEvent(HttpChunkedEvent::END,
						current + 1 - buffer.start, true);
				} else {
					return setError(CHUNK_FINALIZER_PARSE_ERROR, buffer.start, current);
				}

			case HttpChunkedBodyParserState::DONE:
			case HttpChunkedBodyParserState::ERROR:
				P_BUG("Should never be reached");
				return HttpChunkedEvent(HttpChunkedEvent::ERROR, 0, 0, true);
			}
		}

		return HttpChunkedEvent(HttpChunkedEvent::NONE, current - buffer.start, false);
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CHUNKED_BODY_PARSER_H_ */
