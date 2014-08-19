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
#include <DataStructures/LString.h>
#include <DataStructures/HashedStaticString.h>
#include <Logging.h>
#include <Utils/Hasher.h>

namespace Passenger {
namespace ServerKit {


extern const HashedStaticString TRANSFER_ENCODING;
void forceLowerCase(unsigned char *data, size_t len);


class HttpHeaderParser {
private:
	Context *ctx;
	BaseHttpRequest *request;
	http_parser parser;
	const MemoryKit::mbuf *currentBuffer;
	Header *currentHeader;
	Hasher hasher;
	enum {
		PARSING_NOT_STARTED,
		PARSING_URL,
		PARSING_FIRST_HEADER_FIELD,
		PARSING_FIRST_HEADER_VALUE,
		PARSING_HEADER_FIELD,
		PARSING_HEADER_VALUE,
		ERROR_SECURITY_PASSWORD_MISMATCH,
		ERROR_SECURITY_PASSWORD_DUPLICATE,
		ERROR_SECURE_HEADER_NOT_ALLOWED
	} state;
	bool secureMode;

	bool validateHeader(const Header *header) {
		switch (state) {
		case PARSING_FIRST_HEADER_VALUE:
			// We're just done parsing the first header.
			// Check whether it contains the secure mode password.
			if (psg_lstr_cmp(&header->key, P_STATIC_STRING("!~"))) {
				if (ctx->secureModePassword.empty()
				 || psg_lstr_cmp(&header->key, ctx->secureModePassword))
				{
					secureMode = true;
					return true;
				} else {
					state = ERROR_SECURITY_PASSWORD_MISMATCH;
					return false;
				}
			} else {
				return true;
			}
		case PARSING_HEADER_VALUE:
			// We're just done parsing a header, which is not the first one.
			// We only allow secure headers in secure mode.
			if (secureMode) {
				if (psg_lstr_cmp(&header->key, P_STATIC_STRING("!~"), 2)) {
					if (header->key.size >= 3) {
						return true;
					} else {
						state = ERROR_SECURITY_PASSWORD_DUPLICATE;
						return false;
					}
				} else {
					return true;
				}
			} else {
				if (psg_lstr_cmp(&header->key, P_STATIC_STRING("!~"), 2)) {
					state = ERROR_SECURE_HEADER_NOT_ALLOWED;
					return false;
				} else {
					return true;
				}
			}
		default:
			P_BUG("validateHeader() called from invalid state");
			return false;
		}
	}

	bool hasTransferEncodingChunked() {
		const LString *value = request->headers.lookup(TRANSFER_ENCODING);
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

	static int onURL(http_parser *parser, const char *data, size_t len) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);
		self->state = PARSING_URL;
		psg_lstr_append(&self->request->path, self->request->pool,
			*self->currentBuffer, data, len);
		return 0;
	}

	static int onHeaderField(http_parser *parser, const char *data, size_t len) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);

		if (self->state == PARSING_URL
		 || self->state == PARSING_HEADER_VALUE
		 || self->state == PARSING_FIRST_HEADER_VALUE)
		{
			// New header key encountered.

			if (self->state == PARSING_FIRST_HEADER_VALUE
			 || self->state == PARSING_HEADER_VALUE)
			{
				// Validate previous header and insert into table.
				if (!self->validateHeader(self->currentHeader)) {
					return 1;
				}
				self->request->headers.insert(self->currentHeader);
			}

			self->currentHeader = (Header *) psg_palloc(self->request->pool, sizeof(Header));
			psg_lstr_init(&self->currentHeader->key);
			psg_lstr_init(&self->currentHeader->val);
			self->hasher.reset();
			if (self->state == PARSING_URL) {
				self->state = PARSING_FIRST_HEADER_FIELD;
			} else {
				self->state = PARSING_HEADER_FIELD;
			}
		}

		psg_lstr_append(&self->currentHeader->key, self->request->pool,
			*self->currentBuffer, data, len);
		forceLowerCase((unsigned char *) const_cast<char *>(data), len);
		self->hasher.update(data, len);

		return 0;
	}

	static int onHeaderValue(http_parser *parser, const char *data, size_t len) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);

		if (self->state == PARSING_FIRST_HEADER_FIELD || self->state == PARSING_HEADER_FIELD) {
			// New header value encountered. Finalize corresponding header field.
			if (self->state == PARSING_FIRST_HEADER_FIELD) {
				self->state = PARSING_FIRST_HEADER_VALUE;
			} else {
				self->state = PARSING_HEADER_VALUE;
			}
			self->currentHeader->hash = self->hasher.finalize();

		}

		psg_lstr_append(&self->currentHeader->val, self->request->pool,
			*self->currentBuffer, data, len);
		self->hasher.update(data, len);

		return 0;
	}

	static int onHeadersComplete(http_parser *parser) {
		HttpHeaderParser *self = static_cast<HttpHeaderParser *>(parser->data);

		if (self->state == PARSING_HEADER_VALUE
		 || self->state == PARSING_FIRST_HEADER_VALUE)
		{
			// Validate previous header and insert into table.
			if (!self->validateHeader(self->currentHeader)) {
				return 1;
			}
			self->request->headers.insert(self->currentHeader);
		}

		self->currentHeader = NULL;
		self->request->httpState = HttpRequest::PARSED_HEADERS;
		http_parser_pause(parser, 1);
		return 0;
	}

public:
	HttpHeaderParser(Context *context, BaseHttpRequest *_request)
		: ctx(context),
		  request(_request),
		  currentBuffer(NULL),
		  currentHeader(NULL),
		  state(PARSING_NOT_STARTED),
		  secureMode(false)
	{
		http_parser_init(&parser, HTTP_REQUEST);
		parser.data = this;
	}

	size_t feed(const MemoryKit::mbuf &buffer) {
		assert(request->httpState == HttpRequest::PARSING_HEADERS);

		http_parser_settings settings;
		size_t ret;
		bool paused;

		settings.on_message_begin = NULL;
		settings.on_url = onURL;
		settings.on_header_field = onHeaderField;
		settings.on_header_value = onHeaderValue;
		settings.on_headers_complete = onHeadersComplete;
		settings.on_body = NULL;
		settings.on_message_complete = NULL;

		currentBuffer = &buffer;
		ret = http_parser_execute_and_handle_pause(&parser,
			&settings, buffer.start, buffer.size(), paused);
		currentBuffer = NULL;

		if (parser.upgrade) {
			assert(request->httpState == HttpRequest::PARSED_HEADERS);
			request->httpState = HttpRequest::UPGRADED;
		} else if (ret != buffer.size() && !paused) {
			request->httpState = HttpRequest::ERROR;
			switch (HTTP_PARSER_ERRNO(&parser)) {
			case HPE_CB_header_field:
			case HPE_CB_headers_complete:
				switch (state) {
				case ERROR_SECURITY_PASSWORD_MISMATCH:
					request->parseError = "Security password mismatch";
					break;
				case ERROR_SECURITY_PASSWORD_DUPLICATE:
					request->parseError = "A duplicate security password header was encountered";
					break;
				case ERROR_SECURE_HEADER_NOT_ALLOWED:
					request->parseError = "A secure header was provided, but no security password was provided";
					break;
				default:
					goto default_error;
				}
				break;
			default:
				default_error:
				request->parseError = http_errno_description(HTTP_PARSER_ERRNO(&parser));
				break;
			}
		} else if (request->httpState == HttpRequest::PARSED_HEADERS) {
			bool isChunked = hasTransferEncodingChunked();
			boost::uint64_t contentLength;

			ret++;
			request->httpMajor = parser.http_major;
			request->httpMinor = parser.http_minor;
			request->keepAlive = http_should_keep_alive(&parser);
			request->method    = (http_method) parser.method;

			// TODO: check that content-length and transfer-encoding aren't simultaneously given

			// For some reason, the parser leaves content_length at ULLONG_MAX
			// if Content-Length is not given.
			contentLength = parser.content_length;
			if (contentLength == std::numeric_limits<boost::uint64_t>::max()) {
				contentLength = 0;
			}

			if (contentLength > 0 || isChunked) {
				// There is a request body.
				request->requestBodyInfo.contentLength = contentLength;
				if (isChunked) {
					request->httpState = HttpRequest::PARSING_CHUNKED_BODY;
					request->requestBodyType = HttpRequest::RBT_CHUNKED;
				} else {
					request->httpState = HttpRequest::PARSING_BODY;
					request->requestBodyType = HttpRequest::RBT_CONTENT_LENGTH;
				}
			} else {
				// There is no request body.
				request->httpState = HttpRequest::COMPLETE;
				request->requestBodyType = HttpRequest::RBT_NO_BODY;
			}
		}

		return ret;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_HEADER_PARSER_H_ */
