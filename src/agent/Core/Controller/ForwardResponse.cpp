/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
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
#include <Core/Controller.h>

/*************************************************************************
 *
 * Implements Core::Controller methods pertaining sending application
 * response data to the client. This happens in parallel to the process
 * of sending request data to the application.
 *
 *************************************************************************/

namespace Passenger {
namespace Core {

using namespace std;
using namespace boost;


/****************************
 *
 * Private methods
 *
 ****************************/


ServerKit::Channel::Result
Controller::_onAppSourceData(Channel *_channel, const MemoryKit::mbuf &buffer, int errcode) {
	FdSourceChannel *channel = reinterpret_cast<FdSourceChannel *>(_channel);
	Request *req = static_cast<Request *>(static_cast<
		ServerKit::BaseHttpRequest *>(channel->getHooks()->userData));
	Client *client = static_cast<Client *>(req->client);
	Controller *self = static_cast<Controller *>(getServerFromClient(client));
	return self->onAppSourceData(client, req, buffer, errcode);
}

ServerKit::Channel::Result
Controller::onAppSourceData(Client *client, Request *req, const MemoryKit::mbuf &buffer,
	int errcode)
{
	SKC_LOG_EVENT(Controller, client, "onAppSourceData");
	AppResponse *resp = &req->appResponse;

	switch (resp->httpState) {
	case AppResponse::PARSING_HEADERS:
		if (buffer.size() > 0) {
			// Data
			UPDATE_TRACE_POINT();
			size_t ret;
			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			{
				ret = createAppResponseHeaderParser(getContext(), req).
					feed(buffer);
			}
			if (resp->httpState == AppResponse::PARSING_HEADERS) {
				// Not yet done parsing.
				return Channel::Result(buffer.size(), false);
			}

			// Done parsing.
			UPDATE_TRACE_POINT();
			SKC_TRACE(client, 2, "Application response headers received");
			getHeaderParserStatePool().destroy(resp->parserState.headerParser);
			resp->parserState.headerParser = NULL;

			switch (resp->httpState) {
			case AppResponse::COMPLETE:
				req->appSource.stop();
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::PARSING_BODY_WITH_LENGTH:
				SKC_TRACE(client, 2, "Expecting an app response body with fixed length");
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::PARSING_BODY_UNTIL_EOF:
				SKC_TRACE(client, 2, "Expecting app response body until end of stream");
				req->wantKeepAlive = false;
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::PARSING_CHUNKED_BODY:
				SKC_TRACE(client, 2, "Expecting a chunked app response body");
				prepareAppResponseChunkedBodyParsing(client, req);
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::UPGRADED:
				SKC_TRACE(client, 2, "Application upgraded connection");
				req->wantKeepAlive = false;
				onAppResponseBegin(client, req);
				return Channel::Result(ret, false);
			case AppResponse::ONEHUNDRED_CONTINUE:
				SKC_TRACE(client, 2, "Application sent 100-Continue status");
				onAppResponse100Continue(client, req);
				return Channel::Result(ret, false);
			case AppResponse::ERROR:
				SKC_ERROR(client, "Error parsing application response header: " <<
					ServerKit::getErrorDesc(resp->aux.parseError));
				endRequestAsBadGateway(&client, &req);
				return Channel::Result(0, true);
			default:
				P_BUG("Invalid response HTTP state " << (int) resp->httpState);
				return Channel::Result(0, true);
			}
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// EOF
			UPDATE_TRACE_POINT();
			SKC_DEBUG(client, "Application sent EOF before finishing response headers");
			endRequestWithAppSocketIncompleteResponse(&client, &req);
			return Channel::Result(0, true);
		} else {
			// Error
			UPDATE_TRACE_POINT();
			SKC_DEBUG(client, "Application socket read error occurred before finishing response headers");
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, true);
		}

	case AppResponse::PARSING_BODY_WITH_LENGTH:
		if (buffer.size() > 0) {
			// Data
			UPDATE_TRACE_POINT();
			boost::uint64_t maxRemaining, remaining;

			maxRemaining = resp->aux.bodyInfo.contentLength - resp->bodyAlreadyRead;
			remaining = std::min<boost::uint64_t>(buffer.size(), maxRemaining);
			resp->bodyAlreadyRead += remaining;

			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			SKC_TRACE(client, 3, "Application response body: " <<
				resp->bodyAlreadyRead << " of " <<
				resp->aux.bodyInfo.contentLength << " bytes already read");

			if (remaining > 0) {
				UPDATE_TRACE_POINT();
				writeResponseAndMarkForTurboCaching(client, req,
					MemoryKit::mbuf(buffer, 0, remaining));
				if (!req->ended()) {
					if (resp->bodyFullyRead()) {
						SKC_TRACE(client, 2, "End of application response body reached");
						handleAppResponseBodyEnd(client, req);
						endRequest(&client, &req);
					} else {
						maybeThrottleAppSource(client, req);
					}
				}
			} else {
				UPDATE_TRACE_POINT();
				SKC_TRACE(client, 2, "End of application response body reached");
				handleAppResponseBodyEnd(client, req);
				endRequest(&client, &req);
			}
			return Channel::Result(remaining, false);
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// EOF
			UPDATE_TRACE_POINT();
			if (resp->bodyFullyRead()) {
				SKC_TRACE(client, 2, "Application sent EOF");
				handleAppResponseBodyEnd(client, req);
				endRequest(&client, &req);
			} else {
				SKC_WARN(client, "Application sent EOF before finishing response body: " <<
					resp->bodyAlreadyRead << " bytes already read, " <<
					resp->aux.bodyInfo.contentLength << " bytes expected");
				endRequestWithAppSocketIncompleteResponse(&client, &req);
			}
			return Channel::Result(0, true);
		} else {
			// Error
			UPDATE_TRACE_POINT();
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, true);
		}

	case AppResponse::PARSING_CHUNKED_BODY:
		if (!buffer.empty()) {
			// Data
			UPDATE_TRACE_POINT();
			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			ServerKit::HttpChunkedEvent event(createAppResponseChunkedBodyParser(req)
				.feed(buffer));
			resp->bodyAlreadyRead += event.consumed;

			if (req->dechunkResponse) {
				UPDATE_TRACE_POINT();
				switch (event.type) {
				case ServerKit::HttpChunkedEvent::NONE:
					assert(!event.end);
					return Channel::Result(event.consumed, false);
				case ServerKit::HttpChunkedEvent::DATA:
					assert(!event.end);
					writeResponseAndMarkForTurboCaching(client, req, event.data);
					maybeThrottleAppSource(client, req);
					return Channel::Result(event.consumed, false);
				case ServerKit::HttpChunkedEvent::END:
					assert(event.end);
					SKC_TRACE(client, 2, "End of application response body reached");
					resp->aux.bodyInfo.endReached = true;
					handleAppResponseBodyEnd(client, req);
					endRequest(&client, &req);
					return Channel::Result(event.consumed, true);
				case ServerKit::HttpChunkedEvent::ERROR:
					assert(event.end);
					{
						string message = "error parsing app response chunked encoding: ";
						message.append(ServerKit::getErrorDesc(event.errcode));
						disconnectWithError(&client, message);
					}
					return Channel::Result(event.consumed, true);
				}
			} else {
				UPDATE_TRACE_POINT();
				switch (event.type) {
				case ServerKit::HttpChunkedEvent::NONE:
				case ServerKit::HttpChunkedEvent::DATA:
					assert(!event.end);
					writeResponse(client, MemoryKit::mbuf(buffer, 0, event.consumed));
					markResponsePartForTurboCaching(client, req, event.data);
					maybeThrottleAppSource(client, req);
					return Channel::Result(event.consumed, false);
				case ServerKit::HttpChunkedEvent::END:
					assert(event.end);
					SKC_TRACE(client, 2, "End of application response body reached");
					resp->aux.bodyInfo.endReached = true;
					handleAppResponseBodyEnd(client, req);
					writeResponse(client, MemoryKit::mbuf(buffer, 0, event.consumed));
					if (!req->ended()) {
						endRequest(&client, &req);
					}
					return Channel::Result(event.consumed, true);
				case ServerKit::HttpChunkedEvent::ERROR:
					assert(event.end);
					{
						string message = "error parsing app response chunked encoding: ";
						message.append(ServerKit::getErrorDesc(event.errcode));
						disconnectWithError(&client, message);
					}
					return Channel::Result(event.consumed, true);
				}
			}
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// Premature EOF. This cannot be an expected EOF because
			// we end the request upon consuming the end of the chunked body.
			UPDATE_TRACE_POINT();
			disconnectWithError(&client, "error parsing app response chunked encoding: "
				"unexpected end-of-stream");
			return Channel::Result(0, false);
		} else {
			// Error
			UPDATE_TRACE_POINT();
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, true);
		}
		break; // Never reached, shut up compiler warning.

	case AppResponse::PARSING_BODY_UNTIL_EOF:
	case AppResponse::UPGRADED:
		if (buffer.size() > 0) {
			// Data
			UPDATE_TRACE_POINT();
			SKC_TRACE(client, 3, "Processing " << buffer.size() <<
				" bytes of application data: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			resp->bodyAlreadyRead += buffer.size();
			writeResponseAndMarkForTurboCaching(client, req, buffer);
			maybeThrottleAppSource(client, req);
			return Channel::Result(buffer.size(), false);
		} else if (errcode == 0 || errcode == ECONNRESET) {
			// EOF
			UPDATE_TRACE_POINT();
			SKC_TRACE(client, 2, "Application sent EOF");
			SKC_TRACE(client, 2, "Not keep-aliving application session connection");
			req->session->close(true, false);
			endRequest(&client, &req);
			return Channel::Result(0, false);
		} else {
			// Error
			UPDATE_TRACE_POINT();
			endRequestWithAppSocketReadError(&client, &req, errcode);
			return Channel::Result(0, false);
		}
		break; // Never reached, shut up compiler warning.

	default:
		P_BUG("Invalid request HTTP state " << (int) resp->httpState);
		return Channel::Result(0, false);
	}

	return Channel::Result(0, false); // Never reached, shut up compiler warning.
}

void
Controller::onAppResponseBegin(Client *client, Request *req) {
	TRACE_POINT();
	AppResponse *resp = &req->appResponse;
	ssize_t bytesWritten;
	bool oobw;

	#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
		req->timeOnRequestHeaderSent = ev_now(getLoop());
		reportLargeTimeDiff(client,
			"Headers sent until response begun",
			req->timeOnRequestHeaderSent,
			ev_now(getLoop()));
	#endif

	// Localize hash table operations for better CPU caching.
	oobw = resp->secureHeaders.lookup(PASSENGER_REQUEST_OOB_WORK) != NULL;
	resp->date = resp->headers.lookup(HTTP_DATE);
	resp->setCookie = resp->headers.lookup(ServerKit::HTTP_SET_COOKIE);
	if (resp->setCookie != NULL) {
		// Move the Set-Cookie header from resp->headers to resp->setCookie;
		// remove Set-Cookie from resp->headers without deallocating it.
		LString *copy;

		copy = (LString *) psg_palloc(req->pool, sizeof(LString));
		psg_lstr_init(copy);
		psg_lstr_move_and_append(resp->setCookie, req->pool, copy);

		P_ASSERT_EQ(resp->setCookie->size, 0);
		psg_lstr_append(resp->setCookie, req->pool, "x", 1);
		resp->headers.erase(ServerKit::HTTP_SET_COOKIE);

		resp->setCookie = copy;
	}
	resp->headers.erase(HTTP_CONNECTION);
	resp->headers.erase(HTTP_STATUS);
	if (resp->bodyType == AppResponse::RBT_CONTENT_LENGTH) {
		resp->headers.erase(HTTP_CONTENT_LENGTH);
	}
	if (resp->bodyType == AppResponse::RBT_CHUNKED) {
		resp->headers.erase(HTTP_TRANSFER_ENCODING);
		if (req->dechunkResponse) {
			req->wantKeepAlive = false;
		}
	}
	if (resp->headers.lookup(ServerKit::HTTP_X_SENDFILE) != NULL
	 || resp->headers.lookup(ServerKit::HTTP_X_ACCEL_REDIRECT) != NULL)
	{
		// If X-Sendfile or X-Accel-Redirect is set, then HttpHeaderParser
		// treats the app response as having no body, and removes the
		// Content-Length and Transfer-Encoding headers. Because of this,
		// the response that we output also doesn't Content-Length
		// or Transfer-Encoding. So we should disable keep-alive.
		req->wantKeepAlive = false;
	}

	prepareAppResponseCaching(client, req);

	if (OXT_UNLIKELY(oobw)) {
		SKC_TRACE(client, 2, "Response with OOBW detected");
		if (req->session != NULL) {
			req->session->requestOOBW();
		}
	}

	UPDATE_TRACE_POINT();
	if (!sendResponseHeaderWithWritev(client, req, bytesWritten)) {
		UPDATE_TRACE_POINT();
		if (bytesWritten >= 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
			sendResponseHeaderWithBuffering(client, req, bytesWritten);
		} else {
			int e = errno;
			P_ASSERT_EQ(bytesWritten, -1);
			disconnectWithClientSocketWriteError(&client, e);
		}
	}

	if (!req->ended() && !resp->hasBody() && !resp->upgraded()) {
		UPDATE_TRACE_POINT();
		handleAppResponseBodyEnd(client, req);
		endRequest(&client, &req);
	}
}

void
Controller::prepareAppResponseCaching(Client *client, Request *req) {
	if (turboCaching.isEnabled() && !req->cacheKey.empty()) {
		TRACE_POINT();
		AppResponse *resp = &req->appResponse;
		SKC_TRACE(client, 2, "Turbocache: preparing response caching");
		if (turboCaching.responseCache.requestAllowsStoring(req)
		 && turboCaching.responseCache.prepareRequestForStoring(req))
		{
			if (resp->bodyType == AppResponse::RBT_CONTENT_LENGTH
			 && resp->aux.bodyInfo.contentLength > ResponseCache<Request>::MAX_BODY_SIZE)
			{
				SKC_DEBUG(client, "Response body larger than " <<
					ResponseCache<Request>::MAX_BODY_SIZE <<
					" bytes, so response is not eligible for turbocaching");
				// Decrease store success ratio.
				turboCaching.responseCache.incStores();
				req->cacheKey = HashedStaticString();
			}
		} else if (turboCaching.responseCache.requestAllowsInvalidating(req)) {
			SKC_DEBUG(client, "Processing turbocache invalidation based on response");
			turboCaching.responseCache.invalidate(req);
			req->cacheKey = HashedStaticString();
			SKC_TRACE(client, 2, "Turbocache entries:\n" << turboCaching.responseCache.inspect());
		} else {
			SKC_TRACE(client, 2, "Turbocache: response not eligible for turbocaching");
			// Decrease store success ratio.
			turboCaching.responseCache.incStores();
			req->cacheKey = HashedStaticString();
		}
	}
}

void
Controller::onAppResponse100Continue(Client *client, Request *req) {
	TRACE_POINT();
	if (!req->strip100ContinueHeader) {
		UPDATE_TRACE_POINT();
		const unsigned int BUFSIZE = 32;
		char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
		int size = snprintf(buf, BUFSIZE, "HTTP/%d.%d 100 Continue\r\n",
			(int) req->httpMajor, (int) req->httpMinor);
		writeResponse(client, buf, size);
	}
	if (!req->ended()) {
		UPDATE_TRACE_POINT();
		deinitializeAppResponse(client, req);
		reinitializeAppResponse(client, req);
		req->appResponse.oneHundredContinueSent = !req->strip100ContinueHeader;
		// Allow sending more response headers.
		req->responseBegun = false;
	}
}

/**
 * Construct an array of buffers, which together contain the HTTP response
 * data that should be sent to the client. This method does not copy any data:
 * it just constructs buffers that point to the data stored inside `req->pool`,
 * `req->appResponse.headers`, etc.
 *
 * The buffers will be stored in the array pointed to by `buffer`. This array must
 * have space for at least `maxbuffers` items. The actual number of buffers constructed
 * is stored in `nbuffers`, and the total data size of the buffers is stored in `dataSize`.
 * Upon success, returns true. If the actual number of buffers necessary exceeds
 * `maxbuffers`, then false is returned.
 *
 * You can also set `buffers` to NULL, in which case this method will not construct any
 * buffers, but only count the number of buffers necessary, as well as the total data size.
 * In this case, this method always returns true.
 */
bool
Controller::constructHeaderBuffersForResponse(Request *req, struct iovec *buffers,
	unsigned int maxbuffers, unsigned int & restrict_ref nbuffers,
	unsigned int & restrict_ref dataSize,
	unsigned int & restrict_ref nCacheableBuffers)
{
	#define BEGIN_PUSH_NEXT_BUFFER() \
		do { \
			if (buffers != NULL && i >= maxbuffers) { \
				return false; \
			} \
		} while (false)
	#define INC_BUFFER_ITER(i) \
		do { \
			i++; \
		} while (false)
	#define PUSH_STATIC_BUFFER(str) \
		do { \
			BEGIN_PUSH_NEXT_BUFFER(); \
			if (buffers != NULL) { \
				buffers[i].iov_base = (void *) str; \
				buffers[i].iov_len  = sizeof(str) - 1; \
			} \
			INC_BUFFER_ITER(i); \
			dataSize += sizeof(str) - 1; \
		} while (false)

	AppResponse *resp = &req->appResponse;
	ServerKit::HeaderTable::Iterator it(resp->headers);
	const LString::Part *part;
	const char *statusAndReason;
	unsigned int i = 0;

	nbuffers = 0;
	dataSize = 0;

	PUSH_STATIC_BUFFER("HTTP/");

	if (buffers != NULL) {
		BEGIN_PUSH_NEXT_BUFFER();
		const unsigned int BUFSIZE = 16;
		char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
		const char *end = buf + BUFSIZE;
		char *pos = buf;
		pos += uintToString(req->httpMajor, pos, end - pos);
		pos = appendData(pos, end, ".", 1);
		pos += uintToString(req->httpMinor, pos, end - pos);
		buffers[i].iov_base = (void *) buf;
		buffers[i].iov_len  = pos - buf;
		dataSize += pos - buf;
	} else {
		char buf[16];
		const char *end = buf + sizeof(buf);
		char *pos = buf;
		pos += uintToString(req->httpMajor, pos, end - pos);
		pos = appendData(pos, end, ".", 1);
		pos += uintToString(req->httpMinor, pos, end - pos);
		dataSize += pos - buf;
	}
	INC_BUFFER_ITER(i);

	PUSH_STATIC_BUFFER(" ");

	statusAndReason = getStatusCodeAndReasonPhrase(resp->statusCode);
	if (statusAndReason != NULL) {
		size_t len = strlen(statusAndReason);
		BEGIN_PUSH_NEXT_BUFFER();
		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			buffers[i].iov_base = (void *) statusAndReason;
			buffers[i].iov_len  = len;
		}
		INC_BUFFER_ITER(i);
		dataSize += len;

		PUSH_STATIC_BUFFER("\r\nStatus: ");
		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			buffers[i].iov_base = (void *) statusAndReason;
			buffers[i].iov_len  = len;
		}
		INC_BUFFER_ITER(i);
		dataSize += len;

		PUSH_STATIC_BUFFER("\r\n");
	} else {
		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			const unsigned int BUFSIZE = 8;
			char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
			const char *end = buf + BUFSIZE;
			char *pos = buf;
			unsigned int size = uintToString(resp->statusCode, pos, end - pos);
			buffers[i].iov_base = (void *) buf;
			buffers[i].iov_len  = size;
			INC_BUFFER_ITER(i);
			dataSize += size;

			PUSH_STATIC_BUFFER(" Unknown Reason-Phrase\r\nStatus: ");
			BEGIN_PUSH_NEXT_BUFFER();
			buffers[i].iov_base = (void *) buf;
			buffers[i].iov_len  = size;
			INC_BUFFER_ITER(i);
			dataSize += size;

			PUSH_STATIC_BUFFER("\r\n");
		} else {
			char buf[8];
			const char *end = buf + sizeof(buf);
			char *pos = buf;
			unsigned int size = uintToString(resp->statusCode, pos, end - pos);
			INC_BUFFER_ITER(i);
			dataSize += size;

			dataSize += sizeof(" Unknown Reason-Phrase\r\nStatus: ") - 1;
			INC_BUFFER_ITER(i);
			dataSize += size;
			INC_BUFFER_ITER(i);
			dataSize += sizeof("\r\n");
			INC_BUFFER_ITER(i);
		}
	}

	while (*it != NULL) {
		dataSize += it->header->origKey.size + sizeof(": ") - 1;
		dataSize += it->header->val.size + sizeof("\r\n") - 1;

		part = it->header->origKey.start;
		while (part != NULL) {
			if (buffers != NULL) {
				BEGIN_PUSH_NEXT_BUFFER();
				buffers[i].iov_base = (void *) part->data;
				buffers[i].iov_len  = part->size;
			}
			INC_BUFFER_ITER(i);
			part = part->next;
		}
		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			buffers[i].iov_base = (void *) ": ";
			buffers[i].iov_len  = sizeof(": ") - 1;
		}
		INC_BUFFER_ITER(i);

		part = it->header->val.start;
		while (part != NULL) {
			if (buffers != NULL) {
				BEGIN_PUSH_NEXT_BUFFER();
				buffers[i].iov_base = (void *) part->data;
				buffers[i].iov_len  = part->size;
			}
			INC_BUFFER_ITER(i);
			part = part->next;
		}
		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			buffers[i].iov_base = (void *) "\r\n";
			buffers[i].iov_len  = sizeof("\r\n") - 1;
		}
		INC_BUFFER_ITER(i);

		it.next();
	}

	// Add Date header. https://code.google.com/p/phusion-passenger/issues/detail?id=485
	if (resp->date == NULL) {
		unsigned int size;

		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			const unsigned int BUFSIZE = 60;
			char *dateStr = (char *) psg_pnalloc(req->pool, BUFSIZE);
			size = constructDateHeaderBuffersForResponse(dateStr, BUFSIZE);
			buffers[i].iov_base = dateStr;
			buffers[i].iov_len  = size;
		} else {
			char dateStr[60];
			size = constructDateHeaderBuffersForResponse(dateStr, sizeof(dateStr));
		}
		INC_BUFFER_ITER(i);
		dataSize += size;

		PUSH_STATIC_BUFFER("\r\n");
	}

	if (resp->setCookie != NULL) {
		PUSH_STATIC_BUFFER("Set-Cookie: ");
		part = resp->setCookie->start;
		while (part != NULL) {
			if (part->size == 1 && part->data[0] == '\n') {
				// HeaderTable joins multiple Set-Cookie headers together using \n.
				PUSH_STATIC_BUFFER("\r\nSet-Cookie: ");
			} else {
				if (buffers != NULL) {
					BEGIN_PUSH_NEXT_BUFFER();
					buffers[i].iov_base = (void *) part->data;
					buffers[i].iov_len  = part->size;
				}
				INC_BUFFER_ITER(i);
				dataSize += part->size;
			}
			part = part->next;
		}
		PUSH_STATIC_BUFFER("\r\n");
	}

	nCacheableBuffers = i;

	if (resp->bodyType == AppResponse::RBT_CONTENT_LENGTH) {
		PUSH_STATIC_BUFFER("Content-Length: ");
		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			const unsigned int BUFSIZE = 16;
			char *buf = (char *) psg_pnalloc(req->pool, BUFSIZE);
			unsigned int size = integerToOtherBase<boost::uint64_t, 10>(
				resp->aux.bodyInfo.contentLength, buf, BUFSIZE);
			buffers[i].iov_base = (void *) buf;
			buffers[i].iov_len  = size;
			dataSize += size;
		} else {
			dataSize += integerSizeInOtherBase<boost::uint64_t, 10>(
				resp->aux.bodyInfo.contentLength);
		}
		INC_BUFFER_ITER(i);
		PUSH_STATIC_BUFFER("\r\n");
	} else if (resp->bodyType == AppResponse::RBT_CHUNKED && !req->dechunkResponse) {
		PUSH_STATIC_BUFFER("Transfer-Encoding: chunked\r\n");
	}

	if (resp->bodyType == AppResponse::RBT_UPGRADE) {
		PUSH_STATIC_BUFFER("Connection: upgrade\r\n");
	} else if (canKeepAlive(req)) {
		unsigned int httpVersion = req->httpMajor * 1000 + req->httpMinor * 10;
		if (httpVersion < 1010) {
			// HTTP < 1.1 defaults to "Connection: close"
			PUSH_STATIC_BUFFER("Connection: keep-alive\r\n");
		}
	} else {
		unsigned int httpVersion = req->httpMajor * 1000 + req->httpMinor * 10;
		if (httpVersion >= 1010) {
			// HTTP 1.1 defaults to "Connection: keep-alive"
			PUSH_STATIC_BUFFER("Connection: close\r\n");
		}
	}

	if (req->stickySession) {
		StaticString baseURI = req->options.baseURI;
		if (baseURI.empty()) {
			baseURI = P_STATIC_STRING("/");
		}

		// Note that we do NOT set HttpOnly. If we set that flag then Chrome
		// doesn't send cookies over WebSocket handshakes. Confirmed on Chrome 25.

		const LString *cookieName = getStickySessionCookieName(req);
		unsigned int stickySessionId;
		unsigned int stickySessionIdSize;
		char *stickySessionIdStr;

		PUSH_STATIC_BUFFER("Set-Cookie: ");

		part = cookieName->start;
		while (part != NULL) {
			if (buffers != NULL) {
				BEGIN_PUSH_NEXT_BUFFER();
				buffers[i].iov_base = (void *) part->data;
				buffers[i].iov_len  = part->size;
			}
			dataSize += part->size;
			INC_BUFFER_ITER(i);
			part = part->next;
		}

		stickySessionId = req->session->getStickySessionId();
		stickySessionIdSize = uintSizeAsString(stickySessionId);
		stickySessionIdStr = (char *) psg_pnalloc(req->pool, stickySessionIdSize + 1);
		uintToString(stickySessionId, stickySessionIdStr, stickySessionIdSize + 1);

		PUSH_STATIC_BUFFER("=");

		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			buffers[i].iov_base = stickySessionIdStr;
			buffers[i].iov_len  = stickySessionIdSize;
		}
		dataSize += stickySessionIdSize;
		INC_BUFFER_ITER(i);

		PUSH_STATIC_BUFFER("; Path=");

		if (buffers != NULL) {
			BEGIN_PUSH_NEXT_BUFFER();
			buffers[i].iov_base = (void *) baseURI.data();
			buffers[i].iov_len  = baseURI.size();
		}
		dataSize += baseURI.size();
		INC_BUFFER_ITER(i);

		PUSH_STATIC_BUFFER("\r\n");
	}

	if (req->config->showVersionInHeader) {
		#ifdef PASSENGER_IS_ENTERPRISE
			PUSH_STATIC_BUFFER("X-Powered-By: " PROGRAM_NAME " Enterprise " PASSENGER_VERSION "\r\n\r\n");
		#else
			PUSH_STATIC_BUFFER("X-Powered-By: " PROGRAM_NAME " " PASSENGER_VERSION "\r\n\r\n");
		#endif
	} else {
		#ifdef PASSENGER_IS_ENTERPRISE
			PUSH_STATIC_BUFFER("X-Powered-By: " PROGRAM_NAME " Enterprise\r\n\r\n");
		#else
			PUSH_STATIC_BUFFER("X-Powered-By: " PROGRAM_NAME "\r\n\r\n");
		#endif
	}

	nbuffers = i;
	return true;

	#undef BEGIN_PUSH_NEXT_BUFFER
	#undef INC_BUFFER_ITER
	#undef PUSH_STATIC_BUFFER
}

unsigned int
Controller::constructDateHeaderBuffersForResponse(char *dateStr, unsigned int bufsize) {
	char *pos = dateStr;
	const char *end = dateStr + bufsize - 1;
	time_t the_time = (time_t) ev_now(getContext()->libev->getLoop());
	struct tm the_tm;

	pos = appendData(pos, end, "Date: ");
	gmtime_r(&the_time, &the_tm);
	pos += strftime(pos, end - pos, "%a, %d %b %Y %H:%M:%S GMT", &the_tm);
	return pos - dateStr;
}

bool
Controller::sendResponseHeaderWithWritev(Client *client, Request *req,
	ssize_t &bytesWritten)
{
	TRACE_POINT();

	if (OXT_UNLIKELY(mainConfig.benchmarkMode == BM_RESPONSE_BEGIN)) {
		writeBenchmarkResponse(&client, &req, false);
		return true;
	}

	unsigned int maxbuffers = std::min<unsigned int>(
		8 + req->appResponse.headers.size() * 4 + 11, IOV_MAX);
	struct iovec *buffers = (struct iovec *) psg_palloc(req->pool,
		sizeof(struct iovec) * maxbuffers);
	unsigned int nbuffers, dataSize, nCacheableBuffers;

	if (constructHeaderBuffersForResponse(req, buffers,
		maxbuffers, nbuffers, dataSize, nCacheableBuffers))
	{
		UPDATE_TRACE_POINT();
		SKC_TRACE(client, 2, "Sending response headers using writev()");
		logResponseHeaders(client, req, buffers, nbuffers, dataSize);
		markHeaderBuffersForTurboCaching(client, req, buffers, nCacheableBuffers);

		ssize_t ret;
		do {
			ret = writev(client->getFd(), buffers, nbuffers);
		} while (ret == -1 && errno == EINTR);
		bytesWritten = ret;
		req->responseBegun |= ret > 0;
		return ret == (ssize_t) dataSize;
	} else {
		UPDATE_TRACE_POINT();
		bytesWritten = 0;
		return false;
	}
}

void
Controller::sendResponseHeaderWithBuffering(Client *client, Request *req,
	unsigned int offset)
{
	TRACE_POINT();
	struct iovec *buffers;
	unsigned int nbuffers, dataSize, nCacheableBuffers;
	bool ok;

	ok = constructHeaderBuffersForResponse(req, NULL, 0, nbuffers, dataSize,
		nCacheableBuffers);
	assert(ok);

	buffers = (struct iovec *) psg_palloc(req->pool,
		sizeof(struct iovec) * nbuffers);
	ok = constructHeaderBuffersForResponse(req, buffers, nbuffers,
		nbuffers, dataSize, nCacheableBuffers);
	assert(ok);
	(void) ok; // Shut up compiler warning

	UPDATE_TRACE_POINT();
	logResponseHeaders(client, req, buffers, nbuffers, dataSize);
	markHeaderBuffersForTurboCaching(client, req, buffers, nCacheableBuffers);

	MemoryKit::mbuf_pool &mbuf_pool = getContext()->mbuf_pool;
	const unsigned int MBUF_MAX_SIZE = mbuf_pool_data_size(&mbuf_pool);
	if (dataSize <= MBUF_MAX_SIZE) {
		UPDATE_TRACE_POINT();
		SKC_TRACE(client, 2, "Sending response headers using an mbuf");
		MemoryKit::mbuf buffer(MemoryKit::mbuf_get(&mbuf_pool));
		gatherBuffers(buffer.start, MBUF_MAX_SIZE, buffers, nbuffers);
		buffer = MemoryKit::mbuf(buffer, offset, dataSize - offset);
		writeResponse(client, buffer);
	} else {
		UPDATE_TRACE_POINT();
		SKC_TRACE(client, 2, "Sending response headers using a psg_pool buffer");
		char *buffer = (char *) psg_pnalloc(req->pool, dataSize);
		gatherBuffers(buffer, dataSize, buffers, nbuffers);
		writeResponse(client, buffer + offset, dataSize - offset);
	}
}

void
Controller::logResponseHeaders(Client *client, Request *req, struct iovec *buffers,
	unsigned int nbuffers, unsigned int dataSize)
{
	if (OXT_UNLIKELY(LoggingKit::getLevel() >= LoggingKit::DEBUG3)) {
		TRACE_POINT();
		char *buffer = (char *) psg_pnalloc(req->pool, dataSize);
		gatherBuffers(buffer, dataSize, buffers, nbuffers);
		SKC_TRACE(client, 3, "Sending response headers: \"" <<
			cEscapeString(StaticString(buffer, dataSize)) << "\"");
	}
}

void
Controller::markHeaderBuffersForTurboCaching(Client *client, Request *req,
	struct iovec *buffers, unsigned int nbuffers)
{
	if (turboCaching.isEnabled() && !req->cacheKey.empty()) {
		unsigned int totalSize = 0;

		for (unsigned int i = 0; i < nbuffers; i++) {
			totalSize += buffers[i].iov_len;
		}

		if (totalSize > ResponseCache<Request>::MAX_HEADER_SIZE) {
			SKC_DEBUG(client, "Response headers larger than " <<
				ResponseCache<Request>::MAX_HEADER_SIZE <<
				" bytes, so response is not eligible for turbocaching");
			// Decrease store success ratio.
			turboCaching.responseCache.incStores();
			req->cacheKey = HashedStaticString();
		} else {
			req->appResponse.headerCacheBuffers = buffers;
			req->appResponse.nHeaderCacheBuffers = nbuffers;
		}
	}
}

ServerKit::HttpHeaderParser<AppResponse, ServerKit::HttpParseResponse>
Controller::createAppResponseHeaderParser(ServerKit::Context *ctx, Request *req) {
	return ServerKit::HttpHeaderParser<AppResponse, ServerKit::HttpParseResponse>(
		ctx, req->appResponse.parserState.headerParser,
		&req->appResponse, req->pool, req->method);
}

ServerKit::HttpChunkedBodyParser
Controller::createAppResponseChunkedBodyParser(Request *req) {
	return ServerKit::HttpChunkedBodyParser(
		&req->appResponse.parserState.chunkedBodyParser,
		formatAppResponseChunkedBodyParserLoggingPrefix,
		req);
}

unsigned int
Controller::formatAppResponseChunkedBodyParserLoggingPrefix(char *buf,
	unsigned int bufsize, void *userData)
{
	Request *req = static_cast<Request *>(userData);
	return snprintf(buf, bufsize,
		"[Client %u] ChunkedBodyParser: ",
		static_cast<Client *>(req->client)->number);
}

void
Controller::prepareAppResponseChunkedBodyParsing(Client *client, Request *req) {
	P_ASSERT_EQ(req->appResponse.bodyType, AppResponse::RBT_CHUNKED);
	createAppResponseChunkedBodyParser(req).initialize();
}

void
Controller::writeResponseAndMarkForTurboCaching(Client *client, Request *req,
	const MemoryKit::mbuf &buffer)
{
	if (OXT_LIKELY(mainConfig.benchmarkMode != BM_RESPONSE_BEGIN)) {
		writeResponse(client, buffer);
	}
	markResponsePartForTurboCaching(client, req, buffer);
}

void
Controller::markResponsePartForTurboCaching(Client *client, Request *req,
	const MemoryKit::mbuf &buffer)
{
	if (!req->ended() && turboCaching.isEnabled() && !req->cacheKey.empty()) {
		unsigned int totalSize = req->appResponse.bodyCacheBuffer.size + buffer.size();
		if (totalSize > ResponseCache<Request>::MAX_BODY_SIZE) {
			SKC_DEBUG(client, "Response body larger than " <<
				ResponseCache<Request>::MAX_HEADER_SIZE <<
				" bytes, so response is not eligible for turbocaching");
			// Decrease store success ratio.
			turboCaching.responseCache.incStores();
			req->cacheKey = HashedStaticString();
			psg_lstr_deinit(&req->appResponse.bodyCacheBuffer);
		} else {
			psg_lstr_append(&req->appResponse.bodyCacheBuffer, req->pool, buffer,
				buffer.start, buffer.size());
		}
	}
}

void
Controller::maybeThrottleAppSource(Client *client, Request *req) {
	if (!req->ended()) {
		assert(client->output.getBuffersFlushedCallback() == NULL);
		assert(client->output.getDataFlushedCallback() == getClientOutputDataFlushedCallback());
		if (mainConfig.responseBufferHighWatermark > 0
		 && client->output.getTotalBytesBuffered() >= mainConfig.responseBufferHighWatermark)
		{
			SKC_TRACE(client, 2, "Application is sending response data quicker than the client "
				"can keep up with. Throttling application socket");
			client->output.setDataFlushedCallback(_outputDataFlushed);
			req->appSource.stop();
		} else if (client->output.passedThreshold()) {
			SKC_TRACE(client, 2, "Application is sending response data quicker than the on-disk "
				"buffer can keep up with (currently buffered " << client->output.getBytesBuffered() <<
				" bytes). Throttling application socket");
			client->output.setBuffersFlushedCallback(_outputBuffersFlushed);
			req->appSource.stop();
		}
	}
}

void
Controller::_outputBuffersFlushed(FileBufferedChannel *_channel) {
	FileBufferedFdSinkChannel *channel = reinterpret_cast<FileBufferedFdSinkChannel *>(_channel);
	Client *client = static_cast<Client *>(static_cast<
		ServerKit::BaseClient *>(channel->getHooks()->userData));
	Request *req = static_cast<Request *>(client->currentRequest);
	Controller *self = static_cast<Controller *>(getServerFromClient(client));
	if (client->connected() && req != NULL) {
		self->outputBuffersFlushed(client, req);
	}
}

void
Controller::outputBuffersFlushed(Client *client, Request *req) {
	if (!req->ended()) {
		assert(!req->appSource.isStarted());
		SKC_TRACE(client, 2, "Buffered response data has been written to disk. Resuming application socket");
		client->output.clearBuffersFlushedCallback();
		req->appSource.start();
	}
}

void
Controller::_outputDataFlushed(FileBufferedChannel *_channel) {
	FileBufferedFdSinkChannel *channel = reinterpret_cast<FileBufferedFdSinkChannel *>(_channel);
	Client *client = static_cast<Client *>(static_cast<
		ServerKit::BaseClient *>(channel->getHooks()->userData));
	Request *req = static_cast<Request *>(client->currentRequest);
	Controller *self = static_cast<Controller *>(getServerFromClient(client));

	getClientOutputDataFlushedCallback()(_channel);
	if (client->connected() && req != NULL) {
		self->outputDataFlushed(client, req);
	}
}

void
Controller::outputDataFlushed(Client *client, Request *req) {
	if (!req->ended()) {
		assert(!req->appSource.isStarted());
		SKC_TRACE(client, 2, "The client is ready to receive more data. Resuming application socket");
		client->output.setDataFlushedCallback(getClientOutputDataFlushedCallback());
		req->appSource.start();
	}
}

void
Controller::handleAppResponseBodyEnd(Client *client, Request *req) {
	keepAliveAppConnection(client, req);
	storeAppResponseInTurboCache(client, req);
	assert(!req->ended());
}

OXT_FORCE_INLINE void
Controller::keepAliveAppConnection(Client *client, Request *req) {
	if (req->halfClosePolicy == Request::HALF_CLOSE_PERFORMED) {
		SKC_TRACE(client, 2, "Not keep-aliving application session connection"
			" because it had been half-closed before");
		req->session->close(true, false);
	} else {
		// halfClosePolicy is initialized in sendHeaderToApp(). That method is
		// called immediately after checking out a session, before any events
		// from the appSource channel can be received.
		assert(req->halfClosePolicy != Request::HALF_CLOSE_POLICY_UNINITIALIZED);
		if (req->appResponse.wantKeepAlive) {
			SKC_TRACE(client, 2, "Keep-aliving application session connection");
			req->session->close(true, true);
		} else {
			SKC_TRACE(client, 2, "Not keep-aliving application session connection"
				" because application did not allow it");
			req->session->close(true, false);
		}
	}
}

void
Controller::storeAppResponseInTurboCache(Client *client, Request *req) {
	if (turboCaching.isEnabled() && !req->cacheKey.empty()) {
		TRACE_POINT();
		AppResponse *resp = &req->appResponse;
		unsigned int headerSize = 0;
		unsigned int i;

		for (i = 0; i < resp->nHeaderCacheBuffers; i++) {
			headerSize += resp->headerCacheBuffers[i].iov_len;
		}
		ResponseCache<Request>::Entry entry(
			turboCaching.responseCache.store(req, ev_now(getLoop()),
				headerSize, resp->bodyCacheBuffer.size));
		if (entry.valid()) {
			UPDATE_TRACE_POINT();
			SKC_DEBUG(client, "Storing app response in turbocache");
			SKC_TRACE(client, 2, "Turbocache entries:\n" << turboCaching.responseCache.inspect());

			gatherBuffers(entry.body->httpHeaderData,
				ResponseCache<Request>::MAX_HEADER_SIZE,
				resp->headerCacheBuffers, resp->nHeaderCacheBuffers);

			char *pos = entry.body->httpBodyData;
			const char *end = entry.body->httpBodyData
				+ ResponseCache<Request>::MAX_BODY_SIZE;
			const LString::Part *part = resp->bodyCacheBuffer.start;
			while (part != NULL) {
				pos = appendData(pos, end, part->data, part->size);
				part = part->next;
			}
		} else {
			SKC_DEBUG(client, "Could not store app response for turbocaching");
		}
	}
}


} // namespace Core
} // namespace Passenger
