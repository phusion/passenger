/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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

#ifndef _PASSENGER_SERVER_KIT_HTTP_SERVERLER_H_
#define _PASSENGER_SERVER_KIT_HTTP_SERVERLER_H_

#include <psg_sysqueue.h>
#include <boost/pool/object_pool.hpp>
#include <oxt/macros.hpp>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <pthread.h>
#include <LoggingKit/LoggingKit.h>
#include <ServerKit/Server.h>
#include <ServerKit/HttpClient.h>
#include <ServerKit/HttpRequest.h>
#include <ServerKit/HttpRequestRef.h>
#include <ServerKit/HttpHeaderParser.h>
#include <ServerKit/HttpChunkedBodyParser.h>
#include <Algorithms/MovingAverage.h>
#include <Integrations/LibevJsonUtils.h>
#include <SystemTools/SystemTime.h>
#include <StrIntTools/StrIntUtils.h>
#include <Utils/HttpConstants.h>
#include <Algorithms/Hasher.h>
#include <SystemTools/SystemTime.h>

namespace Passenger {
namespace ServerKit {

using namespace boost;


extern const char DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE[];
extern const unsigned int DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE_SIZE;


/*
 * BEGIN ConfigKit schema: Passenger::ServerKit::HttpServerSchema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   accept_burst_count           unsigned integer   -   default(32)
 *   client_freelist_limit        unsigned integer   -   default(0)
 *   min_spare_clients            unsigned integer   -   default(0)
 *   request_freelist_limit       unsigned integer   -   default(1024)
 *   start_reading_after_accept   boolean            -   default(true)
 *
 * END
 */
class HttpServerSchema: public BaseServerSchema {
private:
	void initialize() {
		using namespace ConfigKit;

		add("request_freelist_limit", UINT_TYPE, OPTIONAL, 1024);
	}

public:
	HttpServerSchema()
		: BaseServerSchema(true)
	{
		initialize();
		finalize();
	}

	HttpServerSchema(bool _subclassing)
		: BaseServerSchema(true)
	{
		initialize();
	}
};

struct HttpServerConfigRealization {
	unsigned int requestFreelistLimit;

	HttpServerConfigRealization(const ConfigKit::Store &config)
		: requestFreelistLimit(config["request_freelist_limit"].asUInt())
		{ }

	void swap(HttpServerConfigRealization &other) BOOST_NOEXCEPT_OR_NOTHROW {
		std::swap(requestFreelistLimit, other.requestFreelistLimit);
	}
};

struct HttpServerConfigChangeRequest {
	BaseServerConfigChangeRequest forParent;
	boost::scoped_ptr<HttpServerConfigRealization> configRlz;
};


template< typename DerivedServer, typename Client = HttpClient<HttpRequest> >
class HttpServer: public BaseServer<DerivedServer, Client> {
public:
	typedef typename Client::RequestType Request;
	typedef HttpRequestRef<DerivedServer, Request> RequestRef;
	STAILQ_HEAD(FreeRequestList, Request);

	typedef HttpServerConfigChangeRequest ConfigChangeRequest;

	FreeRequestList freeRequests;
	unsigned int freeRequestCount;
	unsigned long totalRequestsBegun, lastTotalRequestsBegun;
	double requestBeginSpeed1m, requestBeginSpeed1h;

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


	/***** Configuration *****/

	HttpServerConfigRealization configRlz;


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
		assert(client->lingeringRequestCount > 0);
		assert(client->currentRequest != request);
		assert(!LIST_EMPTY(&client->lingeringRequests));

		SKC_TRACE(client, 3, "Request object reached a reference count of 0");
		LIST_REMOVE(request, nextRequest.lingeringRequest);
		assert(client->lingeringRequestCount > 0);
		client->lingeringRequestCount--;
		request->client = NULL;

		if (addRequestToFreelist(request)) {
			SKC_TRACE(client, 3, "Request object added to freelist (" <<
				(freeRequestCount - 1) << " -> " << freeRequestCount << ")");
		} else {
			SKC_TRACE(client, 3, "Request object destroyed; not added to freelist " <<
				"because it's full (" << freeRequestCount << ")");
			if (request->pool != NULL) {
				psg_destroy_pool(request->pool);
				request->pool = NULL;
			}
			delete request;
		}

		this->unrefClient(client, __FILE__, __LINE__);
	}

	bool addRequestToFreelist(Request *request) {
		if (freeRequestCount < configRlz.requestFreelistLimit) {
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
			LIST_INSERT_HEAD(&client->lingeringRequests, req,
				nextRequest.lingeringRequest);
			client->lingeringRequestCount++;
		}
	}

	void doneWithCurrentRequest(Client **client) {
		Client *c = *client;
		assert(c->currentRequest != NULL);
		Request *req = c->currentRequest;
		bool keepAlive = canKeepAlive(req);
		int nextRequestEarlyReadError = req->nextRequestEarlyReadError;

		P_ASSERT_EQ(req->httpState, Request::WAITING_FOR_REFERENCES);
		assert(req->pool != NULL);
		c->currentRequest = NULL;
		if (!psg_reset_pool(req->pool, PSG_DEFAULT_POOL_SIZE)) {
			psg_destroy_pool(req->pool);
			req->pool = NULL;
		}
		unrefRequest(req, __FILE__, __LINE__);
		if (keepAlive) {
			SKC_TRACE(c, 3, "Keeping alive connection, handling next request");
			handleNextRequest(c);
			if (nextRequestEarlyReadError != 0) {
				onClientDataReceived(c, MemoryKit::mbuf(), nextRequestEarlyReadError);
			}
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
			ret = createRequestHeaderParser(this->getContext(), req).
				feed(buffer);
			if (req->httpState == Request::PARSING_HEADERS) {
				// Not yet done parsing.
				return Channel::Result(buffer.size(), false);
			}

			// Done parsing.
			SKC_TRACE(client, 2, "New request received: #" << (totalRequestsBegun + 1));
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
				req->detectingNextRequestEarlyReadError = true;
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

			assert(req->aux.bodyInfo.contentLength > 0);
			maxRemaining = req->aux.bodyInfo.contentLength - req->bodyAlreadyRead;
			assert(maxRemaining > 0);
			remaining = std::min<boost::uint64_t>(buffer.size(), maxRemaining);
			req->bodyAlreadyRead += remaining;
			SKC_TRACE(client, 3, "Event comes with " << buffer.size() <<
				" bytes of fixed-length HTTP request body: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			SKC_TRACE(client, 3, "Request body: " <<
				req->bodyAlreadyRead << " of " <<
				req->aux.bodyInfo.contentLength << " bytes already read");

			req->bodyChannel.feed(MemoryKit::mbuf(buffer, 0, remaining));
			if (req->ended()) {
				return Channel::Result(remaining, false);
			}

			if (req->bodyChannel.acceptingInput()) {
				if (req->bodyFullyRead()) {
					SKC_TRACE(client, 2, "End of request body reached");
					req->detectingNextRequestEarlyReadError = true;
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

	Channel::Result processClientDataWhenParsingChunkedBody(
		Client *client, Request *req, const MemoryKit::mbuf &buffer, int errcode)
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

			SKC_TRACE(client, 3, "Event comes with " << buffer.size() <<
				" bytes of chunked HTTP request body: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
			HttpChunkedEvent event(createChunkedBodyParser(req).feed(buffer));
			req->bodyAlreadyRead += event.consumed;

			switch (event.type) {
			case HttpChunkedEvent::NONE:
				assert(!event.end);
				if (!shouldAutoDechunkBody(client, req)) {
					req->bodyChannel.feed(MemoryKit::mbuf(buffer, 0, event.consumed));
				}
				return Channel::Result(event.consumed, false);
			case HttpChunkedEvent::DATA:
				assert(!event.end);
				if (shouldAutoDechunkBody(client, req)) {
					req->bodyChannel.feed(event.data);
				} else {
					req->bodyChannel.feed(MemoryKit::mbuf(buffer, 0, event.consumed));
				}
				return Channel::Result(event.consumed, false);
			case HttpChunkedEvent::END:
				assert(event.end);
				req->detectingNextRequestEarlyReadError = true;
				req->aux.bodyInfo.endChunkReached = true;
				if (shouldAutoDechunkBody(client, req)) {
					req->bodyChannel.feed(MemoryKit::mbuf());
				} else {
					req->bodyChannel.feed(MemoryKit::mbuf(buffer, 0, event.consumed));
					if (!req->ended()) {
						if (req->bodyChannel.acceptingInput()) {
							req->bodyChannel.feed(MemoryKit::mbuf());
						} else if (req->bodyChannel.mayAcceptInputLater()) {
							client->input.stop();
							req->bodyChannel.consumedCallback = onRequestBodyChannelConsumed;
						}
					}
				}
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

			SKC_TRACE(client, 3, "Event comes with " << buffer.size() <<
				" bytes of upgraded HTTP request body: \"" << cEscapeString(StaticString(
					buffer.start, buffer.size())) << "\"");
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

	bool detectNextRequestEarlyReadError(Client *client, Request *req, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		if (req->detectingNextRequestEarlyReadError) {
			// When we have previously fully read the expected request body,
			// the above flag is set to true. This tells us to detect whether
			// an EOF or an error on the socket has occurred before we are done
			// processing the request.

			req->detectingNextRequestEarlyReadError = false;
			client->input.stop();

			if (!req->ended() && buffer.empty()) {
				if (errcode == 0) {
					SKC_TRACE(client, 3, "Early read EOF detected");
					req->nextRequestEarlyReadError = EARLY_EOF_DETECTED;
				} else {
					SKC_TRACE(client, 3, "Early body receive error detected: "
						<< getErrorDesc(errcode) << " (errno=" << errcode << ")");
					req->nextRequestEarlyReadError = errcode;
				}
				onNextRequestEarlyReadError(client, req, req->nextRequestEarlyReadError);
			} else {
				SKC_TRACE(client, 3, "No early read EOF or body receive error detected");
			}

			return true;
		} else {
			return false;
		}
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
	/***** Hook overrides *****/

	virtual void onClientObjectCreated(Client *client) {
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
		bool ended = req->ended();

		if (!ended) {
			req->lastDataReceiveTime = ev_now(this->getLoop());
		}
		if (detectNextRequestEarlyReadError(client, req, buffer, errcode)) {
			return Channel::Result(0, false);
		}

		// Moved outside switch() so that the CPU branch predictor can do its work
		if (req->httpState == Request::PARSING_HEADERS) {
			assert(!ended);
			return processClientDataWhenParsingHeaders(client, req, buffer, errcode);
		} else {
			switch (req->bodyType) {
			case Request::RBT_CONTENT_LENGTH:
				if (ended) {
					assert(!req->wantKeepAlive);
					return Channel::Result(buffer.size(), true);
				} else {
					return processClientDataWhenParsingBody(client, req, buffer, errcode);
				}
			case Request::RBT_CHUNKED:
				if (ended) {
					assert(!req->wantKeepAlive);
					return Channel::Result(buffer.size(), true);
				} else {
					return processClientDataWhenParsingChunkedBody(client, req, buffer, errcode);
				}
			case Request::RBT_UPGRADE:
				if (ended) {
					assert(!req->wantKeepAlive);
					return Channel::Result(buffer.size(), true);
				} else {
					return processClientDataWhenUpgraded(client, req, buffer, errcode);
				}
			default:
				P_BUG("Invalid request body type " << (int) req->bodyType);
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

	virtual void onUpdateStatistics() {
		ParentClass::onUpdateStatistics();
		ev_tstamp now = ev_now(this->getLoop());
		ev_tstamp duration = now - this->lastStatisticsUpdateTime;

		// Statistics are updated about every 5 seconds, so about 12 updates
		// per minute. We want the old average to decay to 5% after 1 minute
		// and 1 hour, respectively, so:
		// 1 minute: 1 - exp(ln(0.05) / 12) = 0.22092219194555585
		// 1 hour  : 1 - exp(ln(0.05) / (60 * 12)) = 0.0041520953856636345
		requestBeginSpeed1m = expMovingAverage(requestBeginSpeed1m,
			(totalRequestsBegun - lastTotalRequestsBegun) / duration,
			0.22092219194555585);
		requestBeginSpeed1h = expMovingAverage(requestBeginSpeed1h,
			(totalRequestsBegun - lastTotalRequestsBegun) / duration,
			0.0041520953856636345);
	}

	virtual void onFinalizeStatisticsUpdate() {
		ParentClass::onFinalizeStatisticsUpdate();
		lastTotalRequestsBegun = totalRequestsBegun;
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
		totalRequestsBegun++;
		client->requestsBegun++;
	}

	virtual Channel::Result onRequestBody(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		if (errcode != 0 || buffer.empty()) {
			this->disconnect(&client);
		}
		return Channel::Result(buffer.size(), false);
	}

	virtual void onNextRequestEarlyReadError(Client *client, Request *req, int errcode) {
		// Do nothing.
	}

	virtual bool supportsUpgrade(Client *client, Request *req) {
		return false;
	}

	virtual LoggingKit::Level getClientOutputErrorDisconnectionLogLevel(
		Client *client, int errcode) const
	{
		if (errcode == EPIPE || errcode == ECONNRESET) {
			return LoggingKit::INFO;
		} else {
			return LoggingKit::WARN;
		}
	}

	virtual bool shouldAutoDechunkBody(Client *client, Request *req) {
		return true;
	}

	virtual void reinitializeClient(Client *client, int fd) {
		ParentClass::reinitializeClient(client, fd);
		client->requestsBegun = 0;
		assert(client->currentRequest == NULL);
	}

	virtual void reinitializeRequest(Client *client, Request *req) {
		req->httpMajor = 1;
		req->httpMinor = 0;
		req->httpState = Request::PARSING_HEADERS;
		req->bodyType  = Request::RBT_NO_BODY;
		req->method    = HTTP_GET;
		req->wantKeepAlive = false;
		req->responseBegun = false;
		req->detectingNextRequestEarlyReadError = false;
		req->parserState.headerParser = headerParserStatePool.construct();
		createRequestHeaderParser(this->getContext(), req).initialize();
		if (OXT_UNLIKELY(req->pool == NULL)) {
			// We assume that most of the time, the pool from the
			// last request is reset and reused.
			req->pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		}
		psg_lstr_init(&req->path);
		req->bodyChannel.reinitialize();
		req->aux.bodyInfo.contentLength = 0; // Sets the entire union to 0.
		req->bodyAlreadyRead = 0;
		req->lastDataReceiveTime = 0;
		req->lastDataSendTime = 0;
		req->queryStringIndex = -1;
		req->bodyError = 0;
		req->nextRequestEarlyReadError = 0;
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
			psg_lstr_deinit(&it->header->origKey);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		it = HeaderTable::Iterator(req->secureHeaders);
		while (*it != NULL) {
			psg_lstr_deinit(&it->header->key);
			psg_lstr_deinit(&it->header->origKey);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		if (req->pool != NULL && !psg_reset_pool(req->pool, PSG_DEFAULT_POOL_SIZE)) {
			psg_destroy_pool(req->pool);
			req->pool = NULL;
		}

		req->httpState = Request::WAITING_FOR_REFERENCES;
		req->headers.clear();
		req->secureHeaders.clear();
		req->bodyChannel.consumedCallback = NULL;
		req->bodyChannel.deinitialize();
	}


	/***** Misc *****/

	OXT_FORCE_INLINE
	static FileBufferedChannel::Callback getClientOutputDataFlushedCallback() {
		return _onClientOutputDataFlushed;
	}

public:
	HttpServer(Context *context, const HttpServerSchema &schema,
		const Json::Value &initialConfig = Json::Value(),
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: ParentClass(context, schema, initialConfig, translator),
		  freeRequestCount(0),
		  totalRequestsBegun(0),
		  lastTotalRequestsBegun(0),
		  requestBeginSpeed1m(-1),
		  requestBeginSpeed1h(-1),
		  configRlz(ParentClass::config),
		  headerParserStatePool(16, 256)
	{
		STAILQ_INIT(&freeRequests);
	}


	/***** Server management *****/

	virtual void compact(LoggingKit::Level logLevel = LoggingKit::NOTICE) {
		ParentClass::compact();
		unsigned int count = freeRequestCount;

		while (!STAILQ_EMPTY(&freeRequests)) {
			Request *request = STAILQ_FIRST(&freeRequests);
			if (request->pool != NULL) {
				psg_destroy_pool(request->pool);
				request->pool = NULL;
			}
			P_ASSERT_EQ(request->httpState, Request::IN_FREELIST);
			freeRequestCount--;
			STAILQ_REMOVE_HEAD(&freeRequests, nextRequest.freeRequest);
			delete request;
		}
		assert(freeRequestCount == 0);

		SKS_LOG(logLevel, __FILE__, __LINE__,
			"Freed " << count << " spare request objects");
	}


	/***** Request manipulation *****/

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

	bool canKeepAlive(Request *req) const {
		return req->wantKeepAlive
			&& req->bodyFullyRead()
			&& HttpServer::serverState < HttpServer::SHUTTING_DOWN;
	}

	void writeResponse(Client *client, const MemoryKit::mbuf &buffer) {
		client->currentRequest->responseBegun = true;
		client->currentRequest->lastDataSendTime = ev_now(this->getLoop());
		client->output.feedWithoutRefGuard(buffer);
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
			pos = appendData(pos, end, value);
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
			pos = appendData(pos, end, value);
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
			pos = appendData(pos, end, value);
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
			pos = appendData(pos, end, value);
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
					pos = appendData(pos, end, &it->header->origKey);
					pos = appendData(pos, end, P_STATIC_STRING(": "));
					pos = appendData(pos, end, &it->header->val);
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
			c->output.feedWithoutRefGuard(MemoryKit::mbuf());
		}
		if (c->output.endAcked()) {
			doneWithCurrentRequest(&c);
		} else {
			// Call doneWithCurrentRequest() when data flushed
			SKC_TRACE(c, 2, "Waiting until output is flushed");
			req->httpState = Request::FLUSHING_OUTPUT;
			// If the request body is not fully read at this time,
			// then ensure that onClientDataReceived() discards any
			// request body data that we receive from now on.
			req->wantKeepAlive = canKeepAlive(req);
		}

		return true;
	}

	void endAsBadRequest(Client **client, Request **req, const StaticString &body) {
		endWithErrorResponse(client, req, 400, body);
	}


	/***** Configuration and introspection *****/

	bool prepareConfigChange(const Json::Value &updates,
		vector<ConfigKit::Error> &errors, HttpServerConfigChangeRequest &req)
	{
		if (ParentClass::prepareConfigChange(updates, errors, req.forParent)) {
			req.configRlz.reset(new HttpServerConfigRealization(*req.forParent.config));
		}
		return errors.empty();
	}

	void commitConfigChange(HttpServerConfigChangeRequest &req)
		BOOST_NOEXCEPT_OR_NOTHROW
	{
		ParentClass::commitConfigChange(req.forParent);
		configRlz.swap(*req.configRlz);
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = ParentClass::inspectStateAsJson();
		doc["free_request_count"] = freeRequestCount;
		doc["total_requests_begun"] = (Json::UInt64) totalRequestsBegun;
		doc["request_begin_speed"]["1m"] = averageSpeedToJson(
			capFloatPrecision(requestBeginSpeed1m * 60),
			"minute", "1 minute", -1);
		doc["request_begin_speed"]["1h"] = averageSpeedToJson(
			capFloatPrecision(requestBeginSpeed1h * 60),
			"minute", "1 hour", -1);
		return doc;
	}

	virtual Json::Value inspectClientStateAsJson(const Client *client) const {
		Json::Value doc = ParentClass::inspectClientStateAsJson(client);
		if (client->currentRequest) {
			doc["current_request"] = inspectRequestStateAsJson(client->currentRequest);
		}
		doc["requests_begun"] = client->requestsBegun;
		doc["lingering_request_count"] = client->lingeringRequestCount;
		return doc;
	}

	virtual Json::Value inspectRequestStateAsJson(const Request *req) const {
		Json::Value doc(Json::objectValue);
		assert(req->httpState != Request::IN_FREELIST);
		const LString::Part *part;

		doc["refcount"] = req->refcount.load(boost::memory_order_relaxed);
		doc["http_state"] = req->getHttpStateString();

		if (req->begun()) {
			ev_tstamp evNow = ev_now(this->getLoop());
			unsigned long long now = SystemTime::getUsec();

			doc["http_major"] = req->httpMajor;
			doc["http_minor"] = req->httpMinor;
			doc["want_keep_alive"] = req->wantKeepAlive;
			doc["request_body_type"] = req->getBodyTypeString();
			doc["request_body_fully_read"] = req->bodyFullyRead();
			doc["request_body_already_read"] = (Json::Value::UInt64) req->bodyAlreadyRead;
			doc["response_begun"] = req->responseBegun;
			doc["last_data_receive_time"] = evTimeToJson(req->lastDataReceiveTime, evNow, now);
			doc["last_data_send_time"] = evTimeToJson(req->lastDataSendTime, evNow, now);
			doc["method"] = http_method_str(req->method);
			if (req->httpState != Request::ERROR) {
				if (req->bodyType == Request::RBT_CONTENT_LENGTH) {
					doc["content_length"] = (Json::Value::UInt64)
						req->aux.bodyInfo.contentLength;
				} else if (req->bodyType == Request::RBT_CHUNKED) {
					doc["end_chunk_reached"] = (Json::Value::UInt64)
						req->aux.bodyInfo.endChunkReached;
				}
			} else {
				doc["parse_error"] = getErrorDesc(req->aux.parseError);
			}

			if (req->nextRequestEarlyReadError != 0) {
				doc["next_request_early_read_error"] = getErrorDesc(req->nextRequestEarlyReadError)
					+ string(" (errno=") + toString(req->nextRequestEarlyReadError) + ")";
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


	/***** Miscellaneous *****/

	object_pool<HttpHeaderParserState> &getHeaderParserStatePool() {
		return headerParserStatePool;
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
