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
#include <ServerKit/Errors.h>
#include <ServerKit/HttpChunkedBodyParserState.h>
#include <ServerKit/HttpRequest.h>

namespace Passenger {
namespace ServerKit {

using namespace std;


#define CBP_DEBUG(expr) \
	P_TRACE(3, adapter.getLoggingPrefix() << expr)


/**
 * Parses data in HTTP/1.1 chunked transfer encoding.
 *
 * This is a POD struct so that we can put it in a union.
 */
template<typename Adapter>
class HttpChunkedBodyParser {
private:
	HttpChunkedBodyParserState *state;
	typename Adapter::Message *message;
	typename Adapter::InputChannel *input;
	typename Adapter::OutputChannel *output;
	int *errcodeOutput;
	Adapter adapter;

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

	void setError(int errcode) {
		CBP_DEBUG("setting error: " << getErrorDesc(errcode));
		state->state = HttpChunkedBodyParserState::ERROR;
		if (output != NULL) {
			output->feedError(errcode);
		}
		if (errcodeOutput != NULL) {
			*errcodeOutput = errcode;
		}
	}

public:
	HttpChunkedBodyParser(HttpChunkedBodyParserState *_state,
		typename Adapter::Message *_message,
		typename Adapter::InputChannel *_input,
		typename Adapter::OutputChannel *_output,
		int *_errcodeOutput,
		const Adapter &_adapter)
		: state(_state),
		  message(_message),
		  input(_input),
		  output(_output),
		  errcodeOutput(_errcodeOutput),
		  adapter(_adapter)
		{ }

	void initialize() {
		state->state = HttpChunkedBodyParserState::EXPECTING_SIZE_FIRST_DIGIT;
	}

	Channel::Result feed(const MemoryKit::mbuf &buffer) {
		const char *current  = buffer.start;
		const char *end      = buffer.start + buffer.size();
		const char *needle;
		size_t dataSize;

		assert(!buffer.empty());

		while (current < end && state->state < HttpChunkedBodyParserState::DONE) {
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
					if (output != NULL) {
						output->feed(MemoryKit::mbuf(buffer, current - buffer.start, dataSize));
						if (!adapter.requestEnded() && !output->ended()
						 && output->passedThreshold())
						{
							input->stop();
							adapter.setOutputBuffersFlushedCallback();
						}
						return Channel::Result(current + dataSize - buffer.start, false);
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
					setError(CHUNK_SIZE_PARSE_ERROR);
					break;
				}

			case HttpChunkedBodyParserState::EXPECTING_SIZE:
				if (isHexDigit(*current)) {
					if (state->remainingDataSize >= HttpChunkedBodyParserState::MAX_CHUNK_SIZE) {
						setError(CHUNK_SIZE_TOO_LARGE);
						break;
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
					setError(CHUNK_SIZE_PARSE_ERROR);
					break;
				}
				break;

			case HttpChunkedBodyParserState::EXPECTING_CHUNK_EXTENSION:
				needle = (const char *) memchr(current, HttpChunkedBodyParserState::CR,
					end - current);
				if (needle == NULL) {
					return Channel::Result(buffer.size(), false);
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
					setError(CHUNK_SIZE_PARSE_ERROR);
					break;
				}

			case HttpChunkedBodyParserState::EXPECTING_NON_FINAL_CR:
				if (*current == HttpChunkedBodyParserState::CR) {
					state->state = HttpChunkedBodyParserState::EXPECTING_NON_FINAL_LF;
					current++;
					break;
				} else {
					setError(CHUNK_FOOTER_PARSE_ERROR);
					break;
				}

			case HttpChunkedBodyParserState::EXPECTING_NON_FINAL_LF:
				if (*current == HttpChunkedBodyParserState::LF) {
					CBP_DEBUG("done parsing a chunk");
					state->state = HttpChunkedBodyParserState::EXPECTING_SIZE_FIRST_DIGIT;
					current++;
					break;
				} else {
					setError(CHUNK_FOOTER_PARSE_ERROR);
					break;
				}

			case HttpChunkedBodyParserState::EXPECTING_FINAL_CR:
				if (*current == HttpChunkedBodyParserState::CR) {
					state->state = HttpChunkedBodyParserState::EXPECTING_FINAL_LF;
					current++;
					break;
				} else {
					setError(CHUNK_FINALIZER_PARSE_ERROR);
					break;
				}

			case HttpChunkedBodyParserState::EXPECTING_FINAL_LF:
				if (*current == HttpChunkedBodyParserState::LF) {
					CBP_DEBUG("end chunk reached");
					state->state = HttpChunkedBodyParserState::DONE;
					input->stop();
					message->bodyInfo.endChunkReached = true;
					if (output != NULL) {
						output->feed(MemoryKit::mbuf());
						return Channel::Result(current + 1 - buffer.start, false);
					} else {
						return Channel::Result(current + 1 - buffer.start, true);
					}
				} else {
					setError(CHUNK_FINALIZER_PARSE_ERROR);
					break;
				}

			case HttpChunkedBodyParserState::DONE:
			case HttpChunkedBodyParserState::ERROR:
				P_BUG("Should never be reached");
				return Channel::Result(0, true);
			}
		}

		return Channel::Result(current - buffer.start,
			state->state == HttpChunkedBodyParserState::ERROR);
	}

	template<typename Server, typename Client, typename Request>
	void feedUnexpectedEof(Server *server, Client *client, Request *req) {
		// When state == DONE, we stop `input`, so the following
		// assertion should always be true.
		assert(state->state != HttpChunkedBodyParserState::DONE);

		setError(UNEXPECTED_EOF);
		if (!adapter.requestEnded()) {
			server->disconnectWithError(&client,
				"end of chunked request body encountered prematurely");
		}
	}

	void outputBuffersFlushed() {
		output->setBuffersFlushedCallback(NULL);
		input->start();
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CHUNKED_BODY_PARSER_H_ */
