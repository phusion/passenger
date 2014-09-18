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

#ifndef _PASSENGER_SERVER_KIT_HTTP_SERVERLER_H_
#define _PASSENGER_SERVER_KIT_HTTP_SERVERLER_H_

#include <Utils/sysqueue.h>
#include <boost/pool/object_pool.hpp>
#include <oxt/macros.hpp>
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <pthread.h>
#include <Logging.h>
#include <ServerKit/Server.h>
#include <ServerKit/HttpClient.h>
#include <ServerKit/HttpRequest.h>
#include <ServerKit/HttpRequestRef.h>
#include <ServerKit/HttpHeaderParser.h>
#include <ServerKit/HttpChunkedBodyParser.h>
#include <Utils/SystemTime.h>
#include <Utils/StrIntUtils.h>
#include <Utils/HttpConstants.h>
#include <Utils/Hasher.h>

namespace Passenger {
namespace ServerKit {

using namespace boost;


extern const char DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE[];
extern const unsigned int DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE_SIZE;


template< typename DerivedServer, typename Client = HttpClient<HttpRequest> >
class HttpServer: public BaseServer<DerivedServer, Client> {
public:
	typedef typename Client::RequestType Request;
	typedef HttpRequestRef<DerivedServer, Request> RequestRef;
	STAILQ_HEAD(FreeRequestList, Request);

	FreeRequestList freeRequests;
	unsigned int freeRequestCount, requestFreelistLimit;
	unsigned long totalRequestsAccepted;

private:
	/***** Types and nested classes *****/

	typedef BaseServer<DerivedServer, Client> ParentClass;

	class RequestHooksImpl: public HooksImpl {
	public:
		virtual bool hook_isConnected(Hooks *hooks, void *source) {
			Request *req = static_cast<Request *>(static_cast<BaseHttpRequest *>(hooks->userData));
			return !req->ended();
		}

		virtual void hook_ref(Hooks *hooks, void *source, const char *file, unsigned int line) {
			Request *req       = static_cast<Request *>(static_cast<BaseHttpRequest *>(hooks->userData));
			Client *client     = static_cast<Client *>(req->client);
			HttpServer *server = static_cast<HttpServer *>(HttpServer::getServerFromClient(client));
			server->refRequest(req, file, line);
		}

		virtual void hook_unref(Hooks *hooks, void *source, const char *file, unsigned int line) {
			Request *req       = static_cast<Request *>(static_cast<BaseHttpRequest *>(hooks->userData));
			Client *client     = static_cast<Client *>(req->client);
			HttpServer *server = static_cast<HttpServer *>(HttpServer::getServerFromClient(client));
			server->unrefRequest(req, file, line);
		}
	};

	friend class RequestHooksImpl;


	/***** Working state *****/

	RequestHooksImpl requestHooksImpl;
	object_pool<HttpHeaderParserState> headerParserStatePool;


	/***** Request object creation and destruction *****/

	Request *checkoutRequestObject(Client *client) {
		// Try to obtain request object from freelist.
		if (!STAILQ_EMPTY(&freeRequests)) {
			return checkoutRequestObjectFromFreelist();
		} else {
			return createNewRequestObject(client);
		}
	}

	Request *checkoutRequestObjectFromFreelist() {
		assert(freeRequestCount > 0);
		SKS_TRACE(3, "Checking out request object from freelist (" <<
			freeRequestCount << " -> " << (freeRequestCount - 1) << ")");
		Request *request = STAILQ_FIRST(&freeRequests);
		P_ASSERT_EQ(request->httpState, Request::IN_FREELIST);
		freeRequestCount--;
		STAILQ_REMOVE_HEAD(&freeRequests, nextRequest.freeRequest);
		return request;
	}

	Request *createNewRequestObject(Client *client) {
		Request *request;
		SKS_TRACE(3, "Creating new request object");
		try {
			request = new Request();
		} catch (const std::bad_alloc &) {
			return NULL;
		}
		onRequestObjectCreated(client, request);
		return request;
	}

	void requestReachedZeroRefcount(Request *request) {
		Client *client = static_cast<Client *>(request->client);
		P_ASSERT_EQ(request->httpState, Request::WAITING_FOR_REFERENCES);
		assert(client->endedRequestCount > 0);
		assert(client->currentRequest != request);
		assert(!LIST_EMPTY(&client->endedRequests));

		SKC_TRACE(client, 3, "Request object reached a reference count of 0");
		LIST_REMOVE(request, nextRequest.endedRequest);
		assert(client->endedRequestCount > 0);
		client->endedRequestCount--;
		request->client = NULL;

		if (addRequestToFreelist(request)) {
			SKC_TRACE(client, 3, "Request object added to freelist (" <<
				(freeRequestCount - 1) << " -> " << freeRequestCount << ")");
		} else {
			SKC_TRACE(client, 3, "Request object destroyed; not added to freelist " <<
				"because it's full (" << freeRequestCount << ")");
			delete request;
		}

		this->unrefClient(client, __FILE__, __LINE__);
	}

	bool addRequestToFreelist(Request *request) {
		if (freeRequestCount < requestFreelistLimit) {
			STAILQ_INSERT_HEAD(&freeRequests, request, nextRequest.freeRequest);
			freeRequestCount++;
			request->refcount.store(1, boost::memory_order_relaxed);
			request->httpState = Request::IN_FREELIST;
			return true;
		} else {
			return false;
		}
	}

	void passRequestToEventLoopThread(Request *request) {
		// The shutdown procedure waits until all ACTIVE and DISCONNECTED
		// clients are gone before destroying a Server, so we know for sure
		// that this async callback outlives the Server.
		this->getContext()->libev->runLater(boost::bind(
			&HttpServer::passRequestToEventLoopThreadCallback,
			this, RequestRef(request, __FILE__, __LINE__)));
	}

	void passRequestToEventLoopThreadCallback(RequestRef requestRef) {
		// Do nothing. Once this method returns, the reference count of the
		// request drops to 0, and requestReachedZeroRefcount() is called.
	}


	/***** Request deinitialization and preparation for next request *****/

	void deinitializeRequestAndAddToFreelist(Client *client, Request *req) {
		assert(client->currentRequest == req);

		if (req->httpState != Request::WAITING_FOR_REFERENCES) {
			req->httpState = Request::WAITING_FOR_REFERENCES;
			deinitializeRequest(client, req);
			assert(req->ended());
			LIST_INSERT_HEAD(&client->endedRequests, req,
				nextRequest.endedRequest);
			client->endedRequestCount++;
		}
	}

	void doneWithCurrentRequest(Client **client) {
		Client *c = *client;
		assert(c->currentRequest != NULL);
		Request *req = c->currentRequest;
		bool keepAlive = canKeepAlive(req);

		P_ASSERT_EQ(req->httpState, Request::WAITING_FOR_REFERENCES);
		assert(req->pool != NULL);
		c->currentRequest = NULL;
		psg_destroy_pool(req->pool);
		req->pool = NULL;
		unrefRequest(req, __FILE__, __LINE__);
		if (keepAlive) {
			SKC_TRACE(c, 3, "Keeping alive connection, handling next request");
			handleNextRequest(c);
		} else {
			SKC_TRACE(c, 3, "Not keeping alive connection, disconnecting client");
			this->disconnect(client);
		}
	}

	void handleNextRequest(Client *client) {
		Request *req;

		// A request object references its client object.
		// This reference will be removed when the request ends,
		// in requestReachedZeroRefcount().
		this->refClient(client, __FILE__, __LINE__);

		client->input.start();
		client->output.deinitialize();
		client->output.reinitialize(client->getFd());

		client->currentRequest = req = checkoutRequestObject(client);
		req->client = client;
		reinitializeRequest(client, req);
	}


	/***** Client data handling *****/

	Channel::Result processClientDataWhenParsingHeaders(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		if (buffer.size() > 0) {
			size_t ret;
			SKC_TRACE(client, 3, "Parsing " << buffer.size() <<
				" bytes of HTTP header: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			{
				ret = createRequestHeaderParser(this->getContext(), req).
					feed(buffer);
			}
			if (req->httpState == Request::PARSING_HEADERS) {
				// Not yet done parsing.
				return Channel::Result(buffer.size(), false);
			}

			// Done parsing.
			SKC_TRACE(client, 2, "New request received");
			headerParserStatePool.destroy(req->parserState.headerParser);
			req->parserState.headerParser = NULL;

			if (HttpServer::serverState == HttpServer::SHUTTING_DOWN
			 && shouldDisconnectClientOnShutdown(client))
			{
				endWithErrorResponse(&client, &req, 503, "Server shutting down\n");
				return Channel::Result(buffer.size(), false);
			}

			switch (req->httpState) {
			case Request::COMPLETE:
				client->input.stop();
				onRequestBegin(client, req);
				return Channel::Result(ret, false);
			case Request::PARSING_BODY:
				SKC_TRACE(client, 2, "Expecting a request body");
				onRequestBegin(client, req);
				return Channel::Result(ret, false);
			case Request::PARSING_CHUNKED_BODY:
				SKC_TRACE(client, 2, "Expecting a chunked request body");
				prepareChunkedBodyParsing(client, req);
				onRequestBegin(client, req);
				return Channel::Result(ret, false);
			case Request::UPGRADED:
				assert(!req->wantKeepAlive);
				if (supportsUpgrade(client, req)) {
					SKC_TRACE(client, 2, "Expecting connection upgrade");
					onRequestBegin(client, req);
					return Channel::Result(ret, false);
				} else {
					endWithErrorResponse(&client, &req, 422,
						"Connection upgrading not allowed for this request");
					return Channel::Result(0, true);
				}
			case Request::ERROR:
				// Change state so that the response body will be written.
				req->httpState = Request::COMPLETE;
				if (req->aux.parseError == HTTP_VERSION_NOT_SUPPORTED) {
					endWithErrorResponse(&client, &req, 505, "HTTP version not supported\n");
				} else {
					endAsBadRequest(&client, &req, getErrorDesc(req->aux.parseError));
				}
				return Channel::Result(0, true);
			default:
				P_BUG("Invalid request HTTP state " << (int) req->httpState);
				return Channel::Result(0, true);
			}
		} else {
			this->disconnect(&client);
			return Channel::Result(0, true);
		}
	}

	Channel::Result processClientDataWhenParsingBody(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		if (buffer.size() > 0) {
			// Data
			if (!req->bodyChannel.acceptingInput()) {
				if (req->bodyChannel.mayAcceptInputLater()) {
					client->input.stop();
					req->bodyChannel.consumedCallback =
						onRequestBodyChannelConsumed;
					return Channel::Result(0, false);
				} else {
					return Channel::Result(0, true);
				}
			}

			boost::uint64_t maxRemaining, remaining;

			maxRemaining = req->aux.bodyInfo.contentLength - req->bodyAlreadyRead;
			remaining = std::min<boost::uint64_t>(buffer.size(), maxRemaining);
			req->bodyAlreadyRead += remaining;
			SKC_TRACE(client, 3, "Request body: " <<
				req->bodyAlreadyRead << " of " <<
				req->aux.bodyInfo.contentLength << " bytes already read");

			if (remaining > 0) {
				req->bodyChannel.feed(MemoryKit::mbuf(buffer, 0, remaining));
				if (req->ended()) {
					return Channel::Result(remaining, false);
				}

				if (req->bodyChannel.acceptingInput()) {
					if (req->bodyFullyRead()) {
						SKC_TRACE(client, 2, "End of request body reached");
						client->input.stop();
						req->bodyChannel.feed(MemoryKit::mbuf());
					}
					return Channel::Result(remaining, false);
				} else if (req->bodyChannel.mayAcceptInputLater()) {
					client->input.stop();
					req->bodyChannel.consumedCallback =
						onRequestBodyChannelConsumed;
					return Channel::Result(remaining, false);
				} else {
					return Channel::Result(remaining, true);
				}
			} else {
				SKC_TRACE(client, 2, "End of request body reached");
				client->input.stop();
				req->bodyChannel.feed(MemoryKit::mbuf());
				return Channel::Result(0, false);
			}
		} else if (errcode == 0) {
			// Premature EOF. This cannot be an expected EOF because we
			// stop client->input upon consuming the end of the body,
			// and we only resume it upon handling the next request.
			assert(!req->bodyFullyRead());
			SKC_DEBUG(client, "Client sent EOF before finishing response body: " <<
				req->bodyAlreadyRead << " bytes already read, " <<
				req->aux.bodyInfo.contentLength << " bytes expected");
			return feedBodyChannelError(client, req, UNEXPECTED_EOF);
		} else {
			// Error
			SKC_TRACE(client, 2, "Request body receive error: " <<
				getErrorDesc(errcode) << " (errno=" << errcode << ")");
			return feedBodyChannelError(client, req, errcode);
		}
	}

	Channel::Result processClientDataWhenParsingChunkedBody(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		if (buffer.size() > 0) {
			// Data
			if (!req->bodyChannel.acceptingInput()) {
				if (req->bodyChannel.mayAcceptInputLater()) {
					client->input.stop();
					req->bodyChannel.consumedCallback =
						onRequestBodyChannelConsumed;
					return Channel::Result(0, false);
				} else {
					return Channel::Result(0, true);
				}
			}

			HttpChunkedEvent event(createChunkedBodyParser(req).feed(buffer));
			req->bodyAlreadyRead += event.consumed;

			switch (event.type) {
			case HttpChunkedEvent::NONE:
				assert(!event.end);
				return Channel::Result(event.consumed, false);
			case HttpChunkedEvent::DATA:
				assert(!event.end);
				req->bodyChannel.feed(event.data);
				if (!req->ended()) {
					if (req->bodyChannel.acceptingInput()) {
						return Channel::Result(event.consumed, false);
					} else if (req->bodyChannel.mayAcceptInputLater()) {
						client->input.stop();
						req->bodyChannel.consumedCallback = onRequestBodyChannelConsumed;
						return Channel::Result(event.consumed, false);
					} else {
						return Channel::Result(event.consumed, false);
					}
				} else {
					return Channel::Result(event.consumed, false);
				}
			case HttpChunkedEvent::END:
				assert(event.end);
				client->input.stop();
				req->aux.bodyInfo.endChunkReached = true;
				req->bodyChannel.feed(MemoryKit::mbuf());
				return Channel::Result(event.consumed, false);
			case HttpChunkedEvent::ERROR:
				assert(event.end);
				client->input.stop();
				req->wantKeepAlive = false;
				req->bodyChannel.feedError(event.errcode);
				return Channel::Result(event.consumed, true);
			default:
				P_BUG("Unknown HttpChunkedEvent type " << event.type);
				return Channel::Result(0, true);
			}
		} else if (errcode == 0) {
			// Premature EOF. This cannot be an expected EOF because we
			// stop client->input upon consuming the end of the chunked body,
			// and we only resume it upon handling the next request.
			SKC_TRACE(client, 2, "Request body receive error: unexpected end of "
				"chunked stream (errno=" << errcode << ")");
			req->bodyChannel.feedError(UNEXPECTED_EOF);
			return Channel::Result(0, true);
		} else {
			// Error
			SKC_TRACE(client, 2, "Request body receive error: " <<
				getErrorDesc(errcode) << " (errno=" << errcode << ")");
			return feedBodyChannelError(client, req, errcode);
		}
	}

	Channel::Result processClientDataWhenUpgraded(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		if (buffer.size() > 0) {
			// Data
			if (!req->bodyChannel.acceptingInput()) {
				if (req->bodyChannel.mayAcceptInputLater()) {
					client->input.stop();
					req->bodyChannel.consumedCallback =
						onRequestBodyChannelConsumed;
					return Channel::Result(0, false);
				} else {
					return Channel::Result(0, true);
				}
			}

			req->bodyAlreadyRead += buffer.size();
			req->bodyChannel.feed(buffer);
			if (!req->ended()) {
				if (req->bodyChannel.acceptingInput()) {
					return Channel::Result(buffer.size(), false);
				} else if (req->bodyChannel.mayAcceptInputLater()) {
					client->input.stop();
					req->bodyChannel.consumedCallback =
						onRequestBodyChannelConsumed;
					return Channel::Result(buffer.size(), false);
				} else {
					return Channel::Result(buffer.size(), true);
				}
			} else {
				return Channel::Result(buffer.size(), false);
			}
		} else if (errcode == 0) {
			// EOF
			SKC_TRACE(client, 2, "End of request body reached");
			if (req->bodyChannel.acceptingInput()) {
				req->bodyChannel.feed(MemoryKit::mbuf());
				return Channel::Result(0, true);
			} else if (req->bodyChannel.mayAcceptInputLater()) {
				SKC_TRACE(client, 3, "BodyChannel currently busy; will feed "
					"end of request body to bodyChannel later");
				req->bodyChannel.consumedCallback =
					onRequestBodyChannelConsumed_onBodyEof;
				return Channel::Result(-1, false);
			} else {
				SKC_TRACE(client, 3, "BodyChannel already ended");
				return Channel::Result(0, true);
			}
		} else {
			// Error
			SKC_TRACE(client, 2, "Request body receive error: " <<
				getErrorDesc(errcode) << " (errno=" << errcode << ")");
			return feedBodyChannelError(client, req, errcode);
		}
	}

	Channel::Result feedBodyChannelError(Client *client, Request *req, int errcode) {
		if (req->bodyChannel.acceptingInput()) {
			req->bodyChannel.feedError(errcode);
			return Channel::Result(0, true);
		} else if (req->bodyChannel.mayAcceptInputLater()) {
			SKC_TRACE(client, 3, "BodyChannel currently busy; will feed "
				"error to bodyChannel later");
			req->bodyChannel.consumedCallback =
				onRequestBodyChannelConsumed_onBodyError;
			req->bodyError = errcode;
			return Channel::Result(-1, false);
		} else {
			SKC_TRACE(client, 3, "BodyChannel already ended");
			return Channel::Result(0, true);
		}
	}


	/***** Miscellaneous *****/

	char *appendLStringData(char *pos, const char *end, const LString *lstr) {
		const LString::Part *part = lstr->start;
		while (part != NULL) {
			pos = appendData(pos, end, part->data, part->size);
			part = part->next;
		}
		return pos;
	}

	void writeDefault500Response(Client *client, Request *req) {
		writeSimpleResponse(client, 500, NULL, DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE);
	}

	void endWithErrorResponse(Client **client, Request **req, int code, const StaticString &body) {
		HeaderTable headers;
		headers.insert((*req)->pool, "connection", "close");
		headers.insert((*req)->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(*client, code, &headers, body);
		endRequest(client, req);
	}

	static HttpHeaderParser<Request> createRequestHeaderParser(Context *ctx,
		Request *req)
	{
		return HttpHeaderParser<Request>(ctx, req->parserState.headerParser,
			req, req->pool);
	}

	static HttpChunkedBodyParser createChunkedBodyParser(Request *req) {
		return HttpChunkedBodyParser(&req->parserState.chunkedBodyParser,
			formatChunkedBodyParserLoggingPrefix, req);
	}

	static unsigned int formatChunkedBodyParserLoggingPrefix(char *buf,
		unsigned int bufsize, void *userData)
	{
		Request *req = static_cast<Request *>(userData);
		return snprintf(buf, bufsize,
			"[Client %u] ChunkedBodyParser: ",
			static_cast<Client *>(req->client)->number);
	}

	void prepareChunkedBodyParsing(Client *client, Request *req) {
		P_ASSERT_EQ(req->bodyType, Request::RBT_CHUNKED);
		createChunkedBodyParser(req).initialize();
	}


	/***** Channel callbacks *****/

	static void _onClientOutputDataFlushed(FileBufferedChannel *_channel) {
		FileBufferedFdSinkChannel *channel =
			reinterpret_cast<FileBufferedFdSinkChannel *>(_channel);
		Client *client = static_cast<Client *>(static_cast<BaseClient *>(
			channel->getHooks()->userData));

		HttpServer *self = static_cast<HttpServer *>(HttpServer::getServerFromClient(client));
		if (client->currentRequest != NULL
		 && client->currentRequest->httpState == Request::FLUSHING_OUTPUT)
		{
			client->currentRequest->httpState = Request::WAITING_FOR_REFERENCES;
			self->doneWithCurrentRequest(&client);
		}
	}

	static Channel::Result onRequestBodyChannelData(Channel *channel,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		Request *req     = static_cast<Request *>(static_cast<BaseHttpRequest *>(
			channel->hooks->userData));
		Client *client   = static_cast<Client *>(req->client);
		HttpServer *self = static_cast<HttpServer *>(HttpServer::getServerFromClient(client));

		return self->onRequestBody(client, req, buffer, errcode);
	}

	static void onRequestBodyChannelConsumed(Channel *channel, unsigned int size) {
		Request *req     = static_cast<Request *>(static_cast<BaseHttpRequest *>(
			channel->hooks->userData));
		Client *client   = static_cast<Client *>(req->client);
		HttpServer *self = static_cast<HttpServer *>(HttpServer::getServerFromClient(client));
		SKC_LOG_EVENT_FROM_STATIC(self, HttpServer, client, "onRequestBodyChannelConsumed");

		channel->consumedCallback = NULL;
		if (channel->acceptingInput()) {
			if (req->bodyFullyRead()) {
				req->bodyChannel.feed(MemoryKit::mbuf());
			} else {
				client->input.start();
			}
		}
	}

	static void onRequestBodyChannelConsumed_onBodyEof(Channel *channel, unsigned int size) {
		Request *req     = static_cast<Request *>(static_cast<BaseHttpRequest *>(
			channel->hooks->userData));
		Client *client   = static_cast<Client *>(req->client);
		HttpServer *self = static_cast<HttpServer *>(HttpServer::getServerFromClient(client));
		SKC_LOG_EVENT_FROM_STATIC(self, HttpServer, client, "onRequestBodyChannelConsumed_onBodyEof");

		channel->consumedCallback = NULL;
		client->input.consumed(0, true);
		if (channel->acceptingInput()) {
			req->bodyChannel.feed(MemoryKit::mbuf());
		}
	}

	static void onRequestBodyChannelConsumed_onBodyError(Channel *channel, unsigned int size) {
		Request *req     = static_cast<Request *>(static_cast<BaseHttpRequest *>(
			channel->hooks->userData));
		Client *client   = static_cast<Client *>(req->client);
		HttpServer *self = static_cast<HttpServer *>(HttpServer::getServerFromClient(client));
		SKC_LOG_EVENT_FROM_STATIC(self, HttpServer, client, "onRequestBodyChannelConsumed_onBodyError");

		channel->consumedCallback = NULL;
		client->input.consumed(0, true);
		if (channel->acceptingInput()) {
			req->bodyChannel.feedError(req->bodyError);
		}
	}

protected:
	/***** Protected API *****/

	/** Increase request reference count. */
	void refRequest(Request *req, const char *file, unsigned int line) {
		int oldRefcount = req->refcount.fetch_add(1, boost::memory_order_relaxed);
		SKC_TRACE_WITH_POS(static_cast<Client *>(req->client), 3, file, line,
			"Request refcount increased; it is now " << (oldRefcount + 1));
	}

	/** Decrease request reference count. Adds request to the
	 * freelist if reference count drops to 0.
	 */
	void unrefRequest(Request *req, const char *file, unsigned int line) {
		int oldRefcount = req->refcount.fetch_sub(1, boost::memory_order_release);
		assert(oldRefcount >= 1);

		SKC_TRACE_WITH_POS(static_cast<Client *>(req->client), 3, file, line,
			"Request refcount decreased; it is now " << (oldRefcount - 1));
		if (oldRefcount == 1) {
			boost::atomic_thread_fence(boost::memory_order_acquire);

			if (this->getContext()->libev->onEventLoopThread()) {
				requestReachedZeroRefcount(req);
			} else {
				// Let the event loop handle the request reaching the 0 refcount.
				passRequestToEventLoopThread(req);
			}
		}
	}

	object_pool<HttpHeaderParserState> &getHeaderParserStatePool() {
		return headerParserStatePool;
	}

	bool canKeepAlive(Request *req) const {
		return req->wantKeepAlive
			&& req->bodyFullyRead()
			&& HttpServer::serverState < HttpServer::SHUTTING_DOWN;
	}

	void writeResponse(Client *client, const MemoryKit::mbuf &buffer) {
		client->currentRequest->responseBegun = true;
		client->output.feed(buffer);
	}

	void writeResponse(Client *client, const char *data, unsigned int size) {
		writeResponse(client, MemoryKit::mbuf(data, size));
	}

	void writeResponse(Client *client, const StaticString &data) {
		writeResponse(client, data.data(), data.size());
	}

	void
	writeSimpleResponse(Client *client, int code, const HeaderTable *headers,
		const StaticString &body)
	{
		unsigned int headerBufSize = 300;

		if (headers != NULL) {
			HeaderTable::ConstIterator it(*headers);
			while (*it != NULL) {
				headerBufSize += it->header->key.size + sizeof(": ") - 1;
				headerBufSize += it->header->val.size + sizeof("\r\n") - 1;
				it.next();
			}
		}

		Request *req = client->currentRequest;
		char *header = (char *) psg_pnalloc(req->pool, headerBufSize);
		char statusBuffer[50];
		char *pos = header;
		const char *end = header + headerBufSize;
		const char *status;
		const LString *value;

		status = getStatusCodeAndReasonPhrase(code);
		if (status == NULL) {
			snprintf(statusBuffer, sizeof(statusBuffer), "%d Unknown Reason-Phrase", code);
			status = statusBuffer;
		}

		pos += snprintf(pos, end - pos,
			"HTTP/%d.%d %s\r\n"
			"Status: %s\r\n",
			(int) req->httpMajor, (int) req->httpMinor, status, status);

		value = (headers != NULL) ? headers->lookup(P_STATIC_STRING("content-type")) : NULL;
		if (value == NULL) {
			pos = appendData(pos, end, P_STATIC_STRING("Content-Type: text/html; charset=UTF-8\r\n"));
		} else {
			pos = appendData(pos, end, P_STATIC_STRING("Content-Type: "));
			pos = appendLStringData(pos, end, value);
			pos = appendData(pos, end, P_STATIC_STRING("\r\n"));
		}

		value = (headers != NULL) ? headers->lookup(P_STATIC_STRING("date")) : NULL;
		pos = appendData(pos, end, P_STATIC_STRING("Date: "));
		if (value == NULL) {
			time_t the_time = time(NULL);
			struct tm the_tm;
			gmtime_r(&the_time, &the_tm);
			pos += strftime(pos, end - pos, "%a, %d %b %Y %H:%M:%S %z", &the_tm);
		} else {
			pos = appendLStringData(pos, end, value);
		}
		pos = appendData(pos, end, P_STATIC_STRING("\r\n"));

		value = (headers != NULL) ? headers->lookup(P_STATIC_STRING("connection")) : NULL;
		if (value == NULL) {
			if (canKeepAlive(req)) {
				pos = appendData(pos, end, P_STATIC_STRING("Connection: keep-alive\r\n"));
			} else {
				pos = appendData(pos, end, P_STATIC_STRING("Connection: close\r\n"));
			}
		} else {
			pos = appendData(pos, end, P_STATIC_STRING("Connection: "));
			pos = appendLStringData(pos, end, value);
			pos = appendData(pos, end, P_STATIC_STRING("\r\n"));
			if (!psg_lstr_cmp(value, P_STATIC_STRING("Keep-Alive"))
			 && !psg_lstr_cmp(value, P_STATIC_STRING("keep-alive")))
			{
				req->wantKeepAlive = false;
			}
		}

		value = (headers != NULL) ? headers->lookup(P_STATIC_STRING("content-length")) : NULL;
		pos = appendData(pos, end, P_STATIC_STRING("Content-Length: "));
		if (value == NULL) {
			pos += snprintf(pos, end - pos, "%u", (unsigned int) body.size());
		} else {
			pos = appendLStringData(pos, end, value);
		}
		pos = appendData(pos, end, P_STATIC_STRING("\r\n"));

		if (headers != NULL) {
			HeaderTable::ConstIterator it(*headers);
			while (*it != NULL) {
				if (!psg_lstr_cmp(&it->header->key, P_STATIC_STRING("content-type"))
				 && !psg_lstr_cmp(&it->header->key, P_STATIC_STRING("date"))
				 && !psg_lstr_cmp(&it->header->key, P_STATIC_STRING("connection"))
				 && !psg_lstr_cmp(&it->header->key, P_STATIC_STRING("content-length")))
				{
					pos = appendLStringData(pos, end, &it->header->key);
					pos = appendData(pos, end, P_STATIC_STRING(": "));
					pos = appendLStringData(pos, end, &it->header->val);
					pos = appendData(pos, end, P_STATIC_STRING("\r\n"));
				}
				it.next();
			}
		}

		pos = appendData(pos, end, P_STATIC_STRING("\r\n"));

		writeResponse(client, header, pos - header);
		if (!req->ended() && req->method != HTTP_HEAD) {
			writeResponse(client, body.data(), body.size());
		}
	}

	bool endRequest(Client **client, Request **request) {
		Client *c = *client;
		Request *req = *request;
		psg_pool_t *pool;

		*client = NULL;
		*request = NULL;

		if (req->ended()) {
			return false;
		}

		SKC_TRACE(c, 2, "Ending request");
		assert(c->currentRequest == req);

		if (OXT_UNLIKELY(!req->responseBegun)) {
			writeDefault500Response(c, req);
			if (req->ended()) {
				return false;
			}
		}

		// The memory buffers that we're writing out during the
		// FLUSHING_OUTPUT state might live in the palloc pool,
		// so we want to deinitialize the request while preserving
		// the pool. We'll destroy the pool when the output is
		// flushed.
		pool = req->pool;
		req->pool = NULL;
		deinitializeRequestAndAddToFreelist(c, req);
		req->pool = pool;

		if (!c->output.ended()) {
			c->output.feed(MemoryKit::mbuf());
		}
		if (c->output.endAcked()) {
			doneWithCurrentRequest(&c);
		} else {
			// Call doneWithCurrentRequest() when data flushed
			req->httpState = Request::FLUSHING_OUTPUT;
		}

		return true;
	}

	void endAsBadRequest(Client **client, Request **req, const StaticString &body) {
		endWithErrorResponse(client, req, 400, body);
	}


	/***** Hook overrides *****/

	virtual void onClientObjectCreated(Client *client) {
		SKC_LOG_EVENT(HttpServer, client, "onClientObjectCreated");
		ParentClass::onClientObjectCreated(client);
		client->output.setDataFlushedCallback(_onClientOutputDataFlushed);
	}

	virtual void onClientAccepted(Client *client) {
		SKC_LOG_EVENT(HttpServer, client, "onClientAccepted");
		ParentClass::onClientAccepted(client);
		handleNextRequest(client);
	}

	virtual Channel::Result onClientDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		SKC_LOG_EVENT(HttpServer, client, "onClientDataReceived");
		assert(client->currentRequest != NULL);
		Request *req = client->currentRequest;
		RequestRef ref(req, __FILE__, __LINE__);

		// Moved outside switch() so that the CPU branch predictor can do its work
		if (req->httpState == Request::PARSING_HEADERS) {
			return processClientDataWhenParsingHeaders(client, req, buffer, errcode);
		} else {
			switch (req->httpState) {
			case Request::PARSING_BODY:
				return processClientDataWhenParsingBody(client, req, buffer, errcode);
			case Request::PARSING_CHUNKED_BODY:
				return processClientDataWhenParsingChunkedBody(client, req, buffer, errcode);
			case Request::UPGRADED:
				return processClientDataWhenUpgraded(client, req, buffer, errcode);
			default:
				P_BUG("Invalid request HTTP state " << (int) req->httpState);
				// Never reached
				return Channel::Result(0, false);
			}
		}
	}

	virtual void onClientDisconnecting(Client *client) {
		ParentClass::onClientDisconnecting(client);

		// Handle client being disconnect()'ed without endRequest().

		if (client->currentRequest != NULL) {
			Request *req = client->currentRequest;
			deinitializeRequestAndAddToFreelist(client, req);
			client->currentRequest = NULL;
			unrefRequest(req, __FILE__, __LINE__);
		}
	}

	virtual void deinitializeClient(Client *client) {
		ParentClass::deinitializeClient(client);
		client->currentRequest = NULL;
	}

	virtual bool shouldDisconnectClientOnShutdown(Client *client) {
		return client->currentRequest == NULL
			|| client->currentRequest->upgraded();
	}


	/***** New hooks *****/

	virtual void onRequestObjectCreated(Client *client, Request *req) {
		req->hooks.impl = &requestHooksImpl;
		req->hooks.userData = static_cast<BaseHttpRequest *>(req);
		req->bodyChannel.setContext(this->getContext());
		req->bodyChannel.hooks = &req->hooks;
		req->bodyChannel.dataCallback = onRequestBodyChannelData;
	}

	virtual void onRequestBegin(Client *client, Request *req) {
		totalRequestsAccepted++;
	}

	virtual Channel::Result onRequestBody(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		if (errcode != 0 || buffer.empty()) {
			this->disconnect(&client);
		}
		return Channel::Result(buffer.size(), false);
	}

	virtual bool supportsUpgrade(Client *client, Request *req) {
		return false;
	}

	virtual void reinitializeRequest(Client *client, Request *req) {
		req->httpMajor = 1;
		req->httpMinor = 0;
		req->httpState = Request::PARSING_HEADERS;
		req->bodyType  = Request::RBT_NO_BODY;
		req->method    = HTTP_GET;
		req->wantKeepAlive = false;
		req->responseBegun = false;
		req->parserState.headerParser = headerParserStatePool.construct();
		createRequestHeaderParser(this->getContext(), req).initialize();
		req->pool      = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		psg_lstr_init(&req->path);
		req->bodyChannel.reinitialize();
		req->aux.bodyInfo.contentLength = 0; // Sets the entire union to 0.
		req->bodyAlreadyRead = 0;
		req->queryStringIndex = -1;
	}

	/**
	 * Must be idempotent, because onClientDisconnecting() can call it
	 * after endRequest() is called.
	 */
	virtual void deinitializeRequest(Client *client, Request *req) {
		if (req->httpState == Request::PARSING_HEADERS
		 && req->parserState.headerParser != NULL)
		{
			headerParserStatePool.destroy(req->parserState.headerParser);
			req->parserState.headerParser = NULL;
		}

		psg_lstr_deinit(&req->path);

		HeaderTable::Iterator it(req->headers);
		while (*it != NULL) {
			psg_lstr_deinit(&it->header->key);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		it = HeaderTable::Iterator(req->secureHeaders);
		while (*it != NULL) {
			psg_lstr_deinit(&it->header->key);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		if (req->pool != NULL) {
			psg_destroy_pool(req->pool);
			req->pool = NULL;
		}

		req->httpState = Request::WAITING_FOR_REFERENCES;
		req->headers.clear();
		req->secureHeaders.clear();
		req->bodyChannel.consumedCallback = NULL;
		req->bodyChannel.deinitialize();
	}

public:
	HttpServer(Context *context)
		: ParentClass(context),
		  freeRequests(STAILQ_HEAD_INITIALIZER(freeRequests)),
		  freeRequestCount(0),
		  requestFreelistLimit(1024),
		  totalRequestsAccepted(0),
		  headerParserStatePool(16, 256)
		{ }

	virtual void configure(const Json::Value &doc) {
		ParentClass::configure(doc);
		if (doc.isMember("request_freelist_limit")) {
			requestFreelistLimit = doc["request_freelist_limit"].asUInt();
		}
	}

	virtual Json::Value getConfigAsJson() const {
		Json::Value doc = ParentClass::getConfigAsJson();
		doc["request_freelist_limit"] = requestFreelistLimit;
		return doc;
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = ParentClass::inspectStateAsJson();
		doc["free_request_count"] = freeRequestCount;
		doc["total_requests_accepted"] = (Json::UInt64) totalRequestsAccepted;
		return doc;
	}

	virtual Json::Value inspectClientStateAsJson(const Client *client) const {
		Json::Value doc = ParentClass::inspectClientStateAsJson(client);
		if (client->currentRequest) {
			doc["current_request"] = inspectRequestStateAsJson(client->currentRequest);
		}
		doc["ended_request_count"] = client->endedRequestCount;
		return doc;
	}

	virtual Json::Value inspectRequestStateAsJson(const Request *req) const {
		Json::Value doc(Json::objectValue);
		assert(req->httpState != Request::IN_FREELIST);
		const LString::Part *part;

		doc["refcount"] = req->refcount.load(boost::memory_order_relaxed);
		doc["http_state"] = req->getHttpStateString();

		if (req->begun()) {
			doc["http_major"] = req->httpMajor;
			doc["http_minor"] = req->httpMinor;
			doc["want_keep_alive"] = req->wantKeepAlive;
			doc["request_body_type"] = req->getBodyTypeString();
			doc["request_body_fully_read"] = req->bodyFullyRead();
			doc["request_body_already_read"] = req->bodyAlreadyRead;
			doc["response_begun"] = req->responseBegun;
			doc["method"] = http_method_str(req->method);
			if (req->httpState != Request::ERROR) {
				if (req->bodyType == Request::RBT_CONTENT_LENGTH) {
					doc["content_length"] = req->aux.bodyInfo.contentLength;
				} else if (req->bodyType == Request::RBT_CHUNKED) {
					doc["end_chunk_reached"] = req->aux.bodyInfo.endChunkReached;
				}
			} else {
				doc["parse_error"] = getErrorDesc(req->aux.parseError);
			}

			string str;
			str.reserve(req->path.size);
			part = req->path.start;
			while (part != NULL) {
				str.append(part->data, part->size);
				part = part->next;
			}
			doc["path"] = str;

			const LString *host = req->headers.lookup("host");
			if (host != NULL) {
				str.clear();
				str.reserve(host->size);
				part = host->start;
				while (part != NULL) {
					str.append(part->data, part->size);
					part = part->next;
				}
				doc["host"] = str;
			}
		}

		return doc;
	}

	/***** Friend-public methods and hook implementations *****/

	void _refRequest(Request *request, const char *file, unsigned int line) {
		refRequest(request, file, line);
	}

	void _unrefRequest(Request *request, const char *file, unsigned int line) {
		unrefRequest(request, file, line);
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_SERVER_H_ */
