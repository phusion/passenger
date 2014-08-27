/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
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
#ifndef _PASSENGER_SERVER_KIT_HTTP_HEADER_PARSER_H_
#define _PASSENGER_SERVER_KIT_HTTP_HEADER_PARSER_H_

#include <boost/cstdint.hpp>
#include <cstddef>
#include <cassert>
#include <MemoryKit/mbuf.h>
#include <ServerKit/Context.h>
#include <ServerKit/HttpRequest.h>
#include <ServerKit/HttpHeaderParserState.h>
#include <DataStructures/LString.h>
#include <DataStructures/HashedStaticString.h>
#include <Logging.h>
#include <Utils/Hasher.h>

namespace Passenger {
namespace ServerKit {


extern const HashedStaticString TRANSFER_ENCODING;
void forceLowerCase(unsigned char *data, size_t len);

struct HttpParseRequest {};
struct HttpParseResponse {};

template<typename Message, typename MessageType = HttpParseRequest>
class HttpHeaderParser {
private:
	Context *ctx;
	HttpHeaderParserState *state;
	Message *message;
	psg_pool_t *pool;
	const MemoryKit::mbuf *currentBuffer;

	bool validateHeader(const Header *header) {
		if (!state->secureMode) {
			if (!psg_lstr_cmp(&header->key, P_STATIC_STRING("!~"), 2)) {
				return true;
			} else {
				if (header->key.size == 2) {
					// Security password. Check whether it hasn't been
					// given before and whether it is correct.
					if (ctx->secureModePassword.empty()
					 || psg_lstr_cmp(&header->val, ctx->secureModePassword))
					{
						state->secureMode = true;
						return true;
					} else {
						state->state = HttpHeaderParserState::ERROR_SECURITY_PASSWORD_MISMATCH;
						return false;
					}
				} else {
					// Secure header encountered without having
					// encountered a security password.
					state->state = HttpHeaderParserState::ERROR_SECURE_HEADER_NOT_ALLOWED;
					return false;
				}
			}
		} else {
			if (psg_lstr_cmp(&header->key, P_STATIC_STRING("!~"), 2)) {
				if (header->key.size == 2) {
					state->secureMode = false;
				}
				return true;
			} else {
				// To prevent Internet clients from injecting secure headers,
				// we require the web server put secure headers between a begin
				// marker (the security password header) and an end marker.
				// If we find a normal header between the markers, then we
				// can assume the web server is bugged or compromised.
				state->state = HttpHeaderParserState::ERROR_NORMAL_HEADER_NOT_ALLOWED_AFTER_SECURITY_PASSWORD;
				return false;
			}
		}
	}

	void insertCurrentHeader() {
		if (!state->secureMode) {
			message->headers.insert(state->currentHeader);
		} else {
			message->secureHeaders.insert(state->currentHeader);
		}
	}

	bool hasTransferEncodingChunked() {
		const LString *value = message->headers.lookup(TRANSFER_ENCODING);
		return value != NULL && psg_lstr_cmp(value, P_STATIC_STRING("chunked"));
	}

	static size_t http_parser_execute_and_handle_pause(http_parser *parser,
		const http_parser_settings *settings, const char *data, size_t len,
		bool &paused)
	{
		size_t ret = http_parser_execute(parser, settings, data, len);
		if (len > 0 && ret != len && HTTP_PARSER_ERRNO(parser) == HPE_PAUSED) {
			paused = true;
			http_parser_pause(parser, 0);
			http_parser_execute(parser, settings, data + len - 1, 1);
		}
		return ret;
	}

	static int _onURL(http_parser *parser, const char *data, size_t len) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);
		return self->onURL(MessageType(), data, len);
	}

	int onURL(const HttpParseRequest &tag, const char *data, size_t len) {
		state->state = HttpHeaderParserState::PARSING_URL;
		psg_lstr_append(&message->path, pool, *currentBuffer, data, len);
		return 0;
	}

	int onURL(const HttpParseResponse &tag, const char *data, size_t len) {
		P_BUG("Should never be called");
		return 0;
	}

	static int onHeaderField(http_parser *parser, const char *data, size_t len) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);

		if (self->state->state == HttpHeaderParserState::PARSING_NOT_STARTED
		 || self->state->state == HttpHeaderParserState::PARSING_URL
		 || self->state->state == HttpHeaderParserState::PARSING_HEADER_VALUE
		 || self->state->state == HttpHeaderParserState::PARSING_FIRST_HEADER_VALUE)
		{
			// New header key encountered.

			if (self->state->state == HttpHeaderParserState::PARSING_FIRST_HEADER_VALUE
			 || self->state->state == HttpHeaderParserState::PARSING_HEADER_VALUE)
			{
				// Validate previous header and insert into table.
				if (!self->validateHeader(self->state->currentHeader)) {
					return 1;
				}
				self->insertCurrentHeader();
			}

			self->state->currentHeader = (Header *) psg_palloc(self->pool, sizeof(Header));
			psg_lstr_init(&self->state->currentHeader->key);
			psg_lstr_init(&self->state->currentHeader->val);
			self->state->hasher.reset();
			if (self->state->state == HttpHeaderParserState::PARSING_URL) {
				self->state->state = HttpHeaderParserState::PARSING_FIRST_HEADER_FIELD;
			} else {
				self->state->state = HttpHeaderParserState::PARSING_HEADER_FIELD;
			}
		}

		psg_lstr_append(&self->state->currentHeader->key, self->pool,
			*self->currentBuffer, data, len);
		if (psg_lstr_first_byte(&self->state->currentHeader->key) != '!') {
			forceLowerCase((unsigned char *) const_cast<char *>(data), len);
		}
		self->state->hasher.update(data, len);

		return 0;
	}

	static int onHeaderValue(http_parser *parser, const char *data, size_t len) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);

		if (self->state->state == HttpHeaderParserState::PARSING_FIRST_HEADER_FIELD
		 || self->state->state == HttpHeaderParserState::PARSING_HEADER_FIELD)
		{
			// New header value encountered. Finalize corresponding header field.
			if (self->state->state == HttpHeaderParserState::PARSING_FIRST_HEADER_FIELD) {
				self->state->state = HttpHeaderParserState::PARSING_FIRST_HEADER_VALUE;
			} else {
				self->state->state = HttpHeaderParserState::PARSING_HEADER_VALUE;
			}
			self->state->currentHeader->hash = self->state->hasher.finalize();

		}

		psg_lstr_append(&self->state->currentHeader->val, self->pool,
			*self->currentBuffer, data, len);
		self->state->hasher.update(data, len);

		return 0;
	}

	static int onHeadersComplete(http_parser *parser) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);

		if (self->state->state == HttpHeaderParserState::PARSING_HEADER_VALUE
		 || self->state->state == HttpHeaderParserState::PARSING_FIRST_HEADER_VALUE)
		{
			// Validate previous header and insert into table.
			if (!self->validateHeader(self->state->currentHeader)) {
				return 1;
			}
			self->insertCurrentHeader();
		}

		self->state->currentHeader = NULL;
		self->message->httpState = Message::PARSED_HEADERS;
		http_parser_pause(parser, 1);
		return 0;
	}

	void setMethodOrStatus(const HttpParseRequest &tag) {
		message->method = (http_method) state->parser.method;
	}

	void setMethodOrStatus(const HttpParseResponse &tag) {
		message->statusCode = state->parser.status_code;
	}

	bool isHeadRequest(const HttpParseRequest &tag) const {
		return message->method == HTTP_HEAD;
	}

	bool isHeadRequest(const HttpParseResponse &tag) const {
		return false;
	}

public:
	HttpHeaderParser(Context *context, HttpHeaderParserState *_state,
		Message *_message, psg_pool_t *_pool)
		: ctx(context),
		  state(_state),
		  message(_message),
		  pool(_pool),
		  currentBuffer(NULL)
		{ }

	void initialize(enum http_parser_type type) {
		http_parser_init(&state->parser, type);
		state->state = HttpHeaderParserState::PARSING_NOT_STARTED;
		state->secureMode = false;
	}

	size_t feed(const MemoryKit::mbuf &buffer) {
		P_ASSERT_EQ(message->httpState, Message::PARSING_HEADERS);

		http_parser_settings settings;
		size_t ret;
		bool paused;

		settings.on_message_begin = NULL;
		settings.on_url = _onURL;
		settings.on_header_field = onHeaderField;
		settings.on_header_value = onHeaderValue;
		settings.on_headers_complete = onHeadersComplete;
		settings.on_body = NULL;
		settings.on_message_complete = NULL;

		state->parser.data = this;
		currentBuffer = &buffer;
		ret = http_parser_execute_and_handle_pause(&state->parser,
			&settings, buffer.start, buffer.size(), paused);
		currentBuffer = NULL;

		if (!state->parser.upgrade && ret != buffer.size() && !paused) {
			message->httpState = Message::ERROR;
			switch (HTTP_PARSER_ERRNO(&state->parser)) {
			case HPE_CB_header_field:
			case HPE_CB_headers_complete:
				switch (state->state) {
				case HttpHeaderParserState::ERROR_SECURITY_PASSWORD_MISMATCH:
					message->parseError = "Security password mismatch";
					break;
				case HttpHeaderParserState::ERROR_SECURITY_PASSWORD_DUPLICATE:
					message->parseError = "A duplicate security password header was encountered";
					break;
				case HttpHeaderParserState::ERROR_SECURE_HEADER_NOT_ALLOWED:
					message->parseError = "A secure header was provided, but no security password was provided";
					break;
				case HttpHeaderParserState::ERROR_NORMAL_HEADER_NOT_ALLOWED_AFTER_SECURITY_PASSWORD:
					message->parseError = "A normal header was encountered after the security password header";
					break;
				default:
					goto default_error;
				}
				break;
			default:
				default_error:
				message->parseError = http_errno_description(HTTP_PARSER_ERRNO(&state->parser));
				break;
			}
		} else if (message->httpState == Message::PARSED_HEADERS) {
			bool isChunked = hasTransferEncodingChunked();
			boost::uint64_t contentLength;

			ret++;
			message->httpMajor = state->parser.http_major;
			message->httpMinor = state->parser.http_minor;
			message->wantKeepAlive = http_should_keep_alive(&state->parser);
			setMethodOrStatus(MessageType());

			// For some reason, the parser leaves content_length at ULLONG_MAX
			// if Content-Length is not given.
			contentLength = state->parser.content_length;
			if (contentLength == std::numeric_limits<boost::uint64_t>::max()) {
				contentLength = 0;
			}

			if (contentLength > 0 && isChunked) {
				message->httpState = Message::ERROR;
				message->parseError = "Bad request (request may not contain both Content-Length and Transfer-Encoding)";
			} else if (contentLength > 0 || isChunked) {
				// There is a request body.
				message->bodyInfo.contentLength = contentLength;
				if (state->parser.upgrade) {
					message->httpState = Message::ERROR;
					message->parseError = "Bad request ('Upgrade' header is only allowed for requests without request body)";
				} else if (isChunked) {
					message->httpState = Message::PARSING_CHUNKED_BODY;
					message->bodyType = Message::RBT_CHUNKED;
				} else {
					message->httpState = Message::PARSING_BODY;
					message->bodyType = Message::RBT_CONTENT_LENGTH;
				}
			} else {
				// There is no request body.
				if (!state->parser.upgrade) {
					message->httpState = Message::COMPLETE;
				} else if (isHeadRequest(MessageType())) {
					message->httpState = Message::ERROR;
					message->parseError = "Bad request ('Upgrade' header is not allowed for HEAD requests)";
				} else {
					message->httpState = Message::UPGRADED;
				}
				message->bodyType = Message::RBT_NO_BODY;
			}
		}

		return ret;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_HEADER_PARSER_H_ */
