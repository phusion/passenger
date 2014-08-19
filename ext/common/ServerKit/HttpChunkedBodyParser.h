/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2014 Phusion
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
#ifndef _PASSENGER_SERVER_KIT_CHUNKED_BODY_PARSER_H_
#define _PASSENGER_SERVER_KIT_CHUNKED_BODY_PARSER_H_

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <ServerKit/Server.h>
#include <ServerKit/HttpChunkedBodyParserFwd.h>
#include <ServerKit/HttpRequest.h>

namespace Passenger {
namespace ServerKit {

using namespace std;


/***** Private functions *****/

inline bool
HttpChunkedBodyParser_isHexDigit(char ch) {
	return (ch >= '0' && ch <= '9')
		|| (ch >= 'a' && ch <= 'f')
		|| (ch >= 'A' && ch <= 'F');
}

inline int
HttpChunkedBodyParser_parseHexDigit(char ch) {
	if (ch >= 'A' && ch <= 'F') {
		return 10 + ch - 'A';
	} else if (ch >= 'a' && ch <= 'z') {
		return 10 + ch - 'a';
	} else {
		return ch - '0';
	}
}

inline void
HttpChunkedBodyParser_setError(HttpChunkedBodyParser *parser, int errcode) {
	parser->state = HttpChunkedBodyParser::ERROR;
	parser->req->requestBodyChannel.feedError(errcode);
}

inline void
HttpChunkedBodyParser_nextChannelBuffersFlushed(FileBufferedChannel *requestBodyChannel) {
	BaseHttpRequest *req = static_cast<BaseHttpRequest *>(requestBodyChannel->getHooks()->userData);
	requestBodyChannel->buffersFlushedCallback = NULL;
	req->client->input.start();
}


/***** Public functions *****/

inline void
HttpChunkedBodyParser_initialize(HttpChunkedBodyParser *parser, BaseHttpRequest *request) {
	parser->req = request;
	parser->state = HttpChunkedBodyParser::EXPECTING_SIZE_FIRST_DIGIT;
}

inline Channel::Result
HttpChunkedBodyParser_feed(HttpChunkedBodyParser *parser, const MemoryKit::mbuf &buffer) {
	HttpChunkedBodyParser::State &state = parser->state;
	BaseHttpRequest *req = parser->req;
	const char *current  = buffer.start;
	const char *end      = buffer.start + buffer.size();
	const char *needle;
	size_t dataSize;

	assert(!buffer.empty());

	while (current < end && state < HttpChunkedBodyParser::DONE) {
		switch (state) {
		case HttpChunkedBodyParser::EXPECTING_DATA:
			dataSize = std::min<size_t>(parser->remainingDataSize, end - current);
			if (dataSize == 0) {
				state = HttpChunkedBodyParser::EXPECTING_FINAL_CR;
				break;
			} else {
				parser->remainingDataSize -= (unsigned int) dataSize;
				if (parser->remainingDataSize == 0) {
					state = HttpChunkedBodyParser::EXPECTING_NON_FINAL_CR;
				}
				req->requestBodyChannel.feed(MemoryKit::mbuf(buffer, current - buffer.start, dataSize));
				if (!req->ended() && req->requestBodyChannel.passedThreshold()) {
					req->client->input.stop();
					req->requestBodyChannel.buffersFlushedCallback =
						HttpChunkedBodyParser_nextChannelBuffersFlushed;
				}
				return Channel::Result(current + dataSize - buffer.start, false);
			}

		case HttpChunkedBodyParser::EXPECTING_SIZE_FIRST_DIGIT:
			if (HttpChunkedBodyParser_isHexDigit(*current)) {
				parser->remainingDataSize = HttpChunkedBodyParser_parseHexDigit(*current);
				state = HttpChunkedBodyParser::EXPECTING_SIZE;
				current++;
				break;
			} else {
				HttpChunkedBodyParser_setError(parser,
					HttpChunkedBodyParser::CHUNK_SIZE_PARSE_ERROR);
				break;
			}

		case HttpChunkedBodyParser::EXPECTING_SIZE:
			if (HttpChunkedBodyParser_isHexDigit(*current)) {
				if (parser->remainingDataSize >= HttpChunkedBodyParser::MAX_CHUNK_SIZE) {
					HttpChunkedBodyParser_setError(parser,
						HttpChunkedBodyParser::CHUNK_SIZE_STRING_TOO_LARGE);
					break;
				} else {
					parser->remainingDataSize = 10 * parser->remainingDataSize +
						HttpChunkedBodyParser_parseHexDigit(*current);
					current++;
				}
			} else if (*current == HttpChunkedBodyParser::CR) {
				state = HttpChunkedBodyParser::EXPECTING_HEADER_LF;
				current++;
			} else if (*current == ';') {
				state = HttpChunkedBodyParser::EXPECTING_CHUNK_EXTENSION;
				current++;
			} else {
				HttpChunkedBodyParser_setError(parser,
					HttpChunkedBodyParser::CHUNK_SIZE_PARSE_ERROR);
				break;
			}
			break;

		case HttpChunkedBodyParser::EXPECTING_CHUNK_EXTENSION:
			needle = (const char *) memchr(current, HttpChunkedBodyParser::CR, end - current);
			if (needle == NULL) {
				return Channel::Result(buffer.size(), false);
			} else {
				state = HttpChunkedBodyParser::EXPECTING_HEADER_LF;
				current = needle + 1;
				break;
			}

		case HttpChunkedBodyParser::EXPECTING_HEADER_LF:
			if (*current == HttpChunkedBodyParser::LF) {
				state = HttpChunkedBodyParser::EXPECTING_DATA;
				current++;
				break;
			} else {
				HttpChunkedBodyParser_setError(parser,
					HttpChunkedBodyParser::CHUNK_SIZE_PARSE_ERROR);
				break;
			}

		case HttpChunkedBodyParser::EXPECTING_NON_FINAL_CR:
			if (*current == HttpChunkedBodyParser::CR) {
				state = HttpChunkedBodyParser::EXPECTING_NON_FINAL_LF;
				current++;
				break;
			} else {
				HttpChunkedBodyParser_setError(parser,
					HttpChunkedBodyParser::CHUNK_FOOTER_PARSE_ERROR);
				break;
			}

		case HttpChunkedBodyParser::EXPECTING_NON_FINAL_LF:
			if (*current == HttpChunkedBodyParser::LF) {
				state = HttpChunkedBodyParser::EXPECTING_SIZE_FIRST_DIGIT;
				current++;
				break;
			} else {
				HttpChunkedBodyParser_setError(parser,
					HttpChunkedBodyParser::CHUNK_FOOTER_PARSE_ERROR);
				break;
			}

		case HttpChunkedBodyParser::EXPECTING_FINAL_CR:
			if (*current == HttpChunkedBodyParser::CR) {
				state = HttpChunkedBodyParser::EXPECTING_FINAL_LF;
				current++;
				break;
			} else {
				HttpChunkedBodyParser_setError(parser,
					HttpChunkedBodyParser::CHUNK_FINALIZER_PARSE_ERROR);
				break;
			}

		case HttpChunkedBodyParser::EXPECTING_FINAL_LF:
			if (*current == HttpChunkedBodyParser::LF) {
				state = HttpChunkedBodyParser::DONE;
				req->client->input.stop();
				req->requestBodyInfo.endChunkReached = true;
				req->requestBodyChannel.feed(MemoryKit::mbuf());
				return Channel::Result(current + 1 - buffer.start, false);
			} else {
				HttpChunkedBodyParser_setError(parser,
					HttpChunkedBodyParser::CHUNK_FINALIZER_PARSE_ERROR);
				break;
			}

		case HttpChunkedBodyParser::DONE:
		case HttpChunkedBodyParser::ERROR:
			P_BUG("Should never be reached");
			return Channel::Result(0, true);
		}
	}

	return Channel::Result(current - buffer.start,
		state == HttpChunkedBodyParser::ERROR);
}

template<typename Server, typename Client, typename Request>
inline void
HttpChunkedBodyParser_feedEof(HttpChunkedBodyParser *parser, Server *server,
	Client *client, Request *req)
{
	SKC_DEBUG_FROM_STATIC(server, client, "End of chunked request body encountered prematurely");

	// When state == DONE, we stop req->input, so the following
	// assertion should always be true.
	assert(parser->state != HttpChunkedBodyParser::DONE);

	HttpChunkedBodyParser_setError(parser, HttpChunkedBodyParser::UNEXPECTED_EOF);
	if (!req->ended()) {
		server->disconnect(&client);
	}
}


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CHUNKED_BODY_PARSER_H_ */
