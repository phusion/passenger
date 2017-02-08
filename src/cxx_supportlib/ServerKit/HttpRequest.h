/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_HTTP_REQUEST_H_
#define _PASSENGER_SERVER_KIT_HTTP_REQUEST_H_

#include <psg_sysqueue.h>
#include <boost/cstdint.hpp>
#include <boost/atomic.hpp>
#include <ServerKit/http_parser.h>
#include <ServerKit/Hooks.h>
#include <ServerKit/Client.h>
#include <ServerKit/HeaderTable.h>
#include <ServerKit/FileBufferedChannel.h>
#include <ServerKit/HttpHeaderParserState.h>
#include <ServerKit/HttpChunkedBodyParserState.h>
#include <MemoryKit/palloc.h>
#include <DataStructures/LString.h>

namespace Passenger {
namespace ServerKit {


class BaseHttpRequest {
public:
	enum HttpState {
		/** The request headers are still being parsed. */
		PARSING_HEADERS,
		/** Internal state used by the parser. Users should never see this state. */
		PARSED_HEADERS,
		/** The request headers have been parsed, and there is no body. */
		COMPLETE,
		/** The request headers have been parsed, and we are now receiving/parsing the body,
		 * which does not have the chunked transfer-encoding. */
		PARSING_BODY,
		/** The request headers have been parsed, and we are now receiving/parsing the body,
		 * which has the chunked transfer-encoding. */
		PARSING_CHUNKED_BODY,
		/** The request headers have been parsed, and the connection has been upgraded. */
		UPGRADED,

		// The following states are recognized as 'ended'.

		/** An error occurred. */
		ERROR,
		/**
		 * The request has been ended. We've deinitialized the request object, and we're now
		 * waiting for output to be flushed before transitioning to WAITING_FOR_REFERENCES.
		 * In this state, the client object's `currentRequest` field still points to this
		 * request.
		 */
		FLUSHING_OUTPUT,
		/**
		 * The request has ended. We've deinitialized the request object, and we're now
		 * waiting until all references to this request object are gone. In this state,
		 * the client object's `currentRequest` field no longer points to this request.
		 */
		WAITING_FOR_REFERENCES,
		/** This request object is in the freelist. */
		IN_FREELIST
	};

	// Enum values are deliberately chosen so that hasBody() can be branchless.
	enum BodyType {
		/** The request has no body and the connection will not be upgraded. */
		RBT_NO_BODY = 0,
		/** The connection has been upgraded. */
		RBT_UPGRADE = 1,
		/** The request body's size is determined by the Content-Length header. */
		RBT_CONTENT_LENGTH = 2,
		/** The request body's size is determined by the chunked Transfer-Encoding. */
		RBT_CHUNKED = 4
	};

	boost::uint8_t httpMajor;
	boost::uint8_t httpMinor;
	HttpState httpState: 5;
	BodyType bodyType: 3;

	http_method method: 5;
	bool wantKeepAlive: 1;
	bool responseBegun: 1;
	bool detectingNextRequestEarlyReadError: 1;

	boost::atomic<int> refcount;

	BaseClient *client;
	union {
		// If httpState == PARSING_HEADERS
		HttpHeaderParserState *headerParser;
		// If httpState == PARSING_CHUNKED_BODY
		HttpChunkedBodyParserState chunkedBodyParser;
	} parserState;
	psg_pool_t *pool;
	Hooks hooks;
	// Guaranteed to be contiguous.
	LString path;
	HeaderTable headers;
	// We separate headers and secure headers because the number of normal
	// headers is variable, but the number of secure headers is more or less
	// constant.
	HeaderTable secureHeaders;
	// HttpServer feeds all body data received via client->input to bodyChannel
	Channel bodyChannel;

	union {
		/** Length of the message body. Only use when httpState != ERROR. */
		union {
			// If bodyType == RBT_CONTENT_LENGTH. Guaranteed to be > 0.
			boost::uint64_t contentLength;
			// If bodyType == RBT_CHUNKED
			bool endChunkReached;
		} bodyInfo;

		/** If a request parsing error occurred, the error code is stored here.
		 * Only use if httpState == ERROR.
		 */
		int parseError;
	} aux;
	boost::uint64_t bodyAlreadyRead;

	ev_tstamp lastDataReceiveTime;
	ev_tstamp lastDataSendTime;

	/**
	 * The start index of the '?' character in `path`. -1 when it doesn't exist.
	 */
	int queryStringIndex;

	/* When a body error is encountered and bodyChannel is not immediately available,
	 * the error code is temporarily stored here.
	 */
	int bodyError;

	/**
	 * When a request body read error, or a client socket EOF, has been detected
	 * after the current request body has already fully received, the error code is
	 * temporarily stored here so that it may be processed at the next request. The
	 * error is not passed to the bodyChannel immediately because it isn't an error
	 * part of the current request's body. But users of HttpServer can still query
	 * this field to see that an error is imminent, and may choose to abort early.
	 *
	 * The value is either the body read error code, or EARLY_EOF_DETECTED. The
	 * latter means that a client socket EOF has been detected.
	 *
	 * A value of 0 means that everthing is ok.
	 */
	int nextRequestEarlyReadError;


	BaseHttpRequest()
		: refcount(1),
		  client(NULL),
		  pool(NULL),
		  headers(16),
		  secureHeaders(32),
		  bodyAlreadyRead(0)
	{
		psg_lstr_init(&path);
		aux.bodyInfo.contentLength = 0; // Sets the entire union to 0.
	}

	const char *getHttpStateString() const {
		switch (httpState) {
		case PARSING_HEADERS:
			return "PARSING_HEADERS";
		case PARSED_HEADERS:
			return "PARSED_HEADERS";
		case COMPLETE:
			return "COMPLETE";
		case PARSING_BODY:
			return "PARSING_BODY";
		case PARSING_CHUNKED_BODY:
			return "PARSING_CHUNKED_BODY";
		case UPGRADED:
			return "UPGRADED";
		case ERROR:
			return "ERROR";
		case FLUSHING_OUTPUT:
			return "FLUSHING_OUTPUT";
		case WAITING_FOR_REFERENCES:
			return "WAITING_FOR_REFERENCES";
		case IN_FREELIST:
			return "IN_FREELIST";
		default:
			return "UNKNOWN";
		}
	}

	const char *getBodyTypeString() const {
		switch (bodyType) {
		case RBT_NO_BODY:
			return "NO_BODY";
		case RBT_UPGRADE:
			return "UPGRADE";
		case RBT_CONTENT_LENGTH:
			return "CONTENT_LENGTH";
		case RBT_CHUNKED:
			return "CHUNKED";
		default:
			return "UNKNOWN";
		}
	}

	bool bodyFullyRead() const {
		switch (bodyType) {
		case RBT_NO_BODY:
			return true;
		case RBT_UPGRADE:
			return false;
		case RBT_CONTENT_LENGTH:
			return bodyAlreadyRead >= aux.bodyInfo.contentLength;
		case RBT_CHUNKED:
			return aux.bodyInfo.endChunkReached;
		default:
			return false;
		}
	}

	bool hasBody() const {
		return bodyType & (RBT_CONTENT_LENGTH | RBT_CHUNKED);
	}

	bool upgraded() const {
		return bodyType == RBT_UPGRADE;
	}

	// Not mutually exclusive with ended(). If a request has begun() and is ended(),
	// then it just means that it hasn't been reinitialized for the next request yet.
	bool begun() const {
		return (int) httpState >= COMPLETE;
	}

	bool ended() const {
		// Branchless OR.
		return ((int) httpState >= (int) ERROR) | !client->connected();
	}

	StaticString getPathWithoutQueryString() const {
		if (queryStringIndex == -1) {
			return StaticString(path.start->data, path.size);
		} else {
			return StaticString(path.start->data, queryStringIndex);
		}
	}

	StaticString getQueryString() const {
		if (queryStringIndex == -1) {
			return StaticString();
		} else {
			return StaticString(path.start->data + queryStringIndex + 1,
				path.size - queryStringIndex - 1);
		}
	}
};


#define DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(RequestType) \
	public: \
	union { \
		STAILQ_ENTRY(RequestType) freeRequest; \
		LIST_ENTRY(RequestType) lingeringRequest; \
	} nextRequest


class HttpRequest: public BaseHttpRequest {
public:
	DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(Passenger::ServerKit::HttpRequest);
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_REQUEST_H_ */
