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
#ifndef _PASSENGER_SERVER_KIT_HTTP_REQUEST_H_
#define _PASSENGER_SERVER_KIT_HTTP_REQUEST_H_

#include <Utils/sysqueue.h>
#include <boost/cstdint.hpp>
#include <boost/atomic.hpp>
#include <ServerKit/http_parser.h>
#include <ServerKit/Client.h>
#include <ServerKit/HeaderTable.h>
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
		 * The request has been ended. We're now waiting for output to be flushed before
		 * transitioning to WAITING_FOR_REFERENCES. In this state, the client object's
		 * `currentRequest` field still points to this request.
		 */
		FLUSHING_OUTPUT,
		/**
		 * The request has ended. We're now waiting until all references to this request
		 * object are gone. In this state, the client object's `currentRequest` field no
		 * longer points to this request.
		 */
		WAITING_FOR_REFERENCES,
		/** This request object is in the freelist. */
		IN_FREELIST
	};

	boost::uint8_t httpMajor;
	boost::uint8_t httpMinor;
	HttpState httpState;

	http_method method: 5;
	bool keepAlive: 1;

	boost::atomic<int> refcount;

	BaseClient *client;
	psg_pool_t *pool;
	LString path;
	HeaderTable headers;

	/** If a request parsing error occurred, the error message is stored here. */
	const char *parseError;

	/** Length of the message body. Only has meaning when state is PARSING_BODY. */
	boost::uint64_t contentLength;
	boost::uint64_t contentAlreadyRead;


	BaseHttpRequest()
		: refcount(1),
		  client(NULL),
		  pool(NULL),
		  parseError(NULL)
	{
		psg_lstr_init(&path);
	}

	~BaseHttpRequest() {
		deinitialize();
	}

	void reinitialize() {
		httpMajor = 1;
		httpMinor = 0;
		httpState = PARSING_HEADERS;
		method    = HTTP_GET;
		keepAlive = false;
		pool      = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		psg_lstr_init(&path);
		parseError = NULL;
		contentLength = 0;
		contentAlreadyRead = 0;
	}

	void deinitialize() {
		if (pool != NULL) {
			psg_destroy_pool(pool);
			pool = NULL;
		}
		psg_lstr_deinit(&path);

		HeaderTable::Iterator it(headers);
		while (*it != NULL) {
			psg_lstr_deinit(&it->header->key);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		headers.clear();
	}

	bool ended() const {
		return (int) httpState >= (int) ERROR;
	}
};


#define DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(RequestType) \
	public: \
	union { \
		STAILQ_ENTRY(RequestType) freeRequest; \
		LIST_ENTRY(RequestType) endedRequest; \
	} nextRequest


class HttpRequest: public BaseHttpRequest {
public:
	DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(HttpRequest);
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_REQUEST_H_ */
