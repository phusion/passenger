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
#include <cassert>
#include <pthread.h>
#include <ServerKit/Server.h>
#include <ServerKit/HttpClient.h>
#include <ServerKit/HttpRequest.h>
#include <ServerKit/HttpRequestRef.h>
#include <ServerKit/HttpHeaderParser.h>
#include <ServerKit/HttpChunkedBodyParser.h>
#include <Utils/SystemTime.h>
#include <Utils/StrIntUtils.h>

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

private:
	/***** Types and nested classes *****/

	typedef BaseServer<DerivedServer, Client> ParentClass;

	class RequestHooksImpl: public HooksImpl {
	public:
		virtual bool hook_isConnected(Hooks *hooks, void *source) {
			Request *req = static_cast<Request *>(hooks->userData);
			return !req->ended();
		}

		virtual void hook_ref(Hooks *hooks, void *source) {
			Request *req       = static_cast<Request *>(hooks->userData);
			Client *client     = static_cast<Client *>(req->client);
			HttpServer *server = static_cast<HttpServer *>(client->getServer());
			server->refRequest(req);
		}

		virtual void hook_unref(Hooks *hooks, void *source) {
			Request *req       = static_cast<Request *>(hooks->userData);
			Client *client     = static_cast<Client *>(req->client);
			HttpServer *server = static_cast<HttpServer *>(client->getServer());
			server->unrefRequest(req);
		}
	};

	friend class RequestHooksImpl;


	/***** Working state *****/

	RequestHooksImpl requestHooksImpl;
	object_pool<HttpHeaderParser> headerParserObjectPool;


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
		assert(request->httpState == Request::IN_FREELIST);
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
		assert(request->httpState == Request::WAITING_FOR_REFERENCES);
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

		this->unrefClient(client);
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
			this, RequestRef(request)));
	}

	void passRequestToEventLoopThreadCallback(RequestRef requestRef) {
		// Do nothing. Once this method returns, the reference count of the
		// request drops to 0, and requestReachedZeroRefcount() is called.
	}


	/***** Request deinitialization and preparation for next request *****/

	void deinitCurrentRequest(Client *client, Request *req) {
		assert(client->currentRequest == req);

		if (req->httpState == Request::PARSING_HEADERS && req->reqParser.headerParser != NULL) {
			headerParserObjectPool.destroy(req->reqParser.headerParser);
			req->reqParser.headerParser = NULL;
		}

		req->httpState = Request::WAITING_FOR_REFERENCES;
		req->deinitialize();
		assert(req->ended());
		LIST_INSERT_HEAD(&client->endedRequests, req,
			nextRequest.endedRequest);
		client->endedRequestCount++;
	}

	void doneWithCurrentRequest(Client **client) {
		Client *c = *client;
		assert(c->currentRequest != NULL);
		Request *req = c->currentRequest;
		bool keepAlive = req->canKeepAlive();

		assert(req->httpState = Request::WAITING_FOR_REFERENCES);
		c->currentRequest = NULL;
		unrefRequest(req);
		if (keepAlive) {
			handleNextRequest(c);
		} else {
			this->disconnect(client);
		}
	}

	void handleNextRequest(Client *client) {
		Request *req;

		client->input.start();
		client->output.deinitialize();
		client->output.reinitialize(client->getFd());

		client->currentRequest = req = checkoutRequestObject(client);
		req->client = client;
		req->reinitialize();
		req->reqParser.headerParser = headerParserObjectPool.construct(
			this->getContext(), req);

		this->refClient(client);
	}


	/***** Miscellaneous *****/

	void writeDefault500Response(Client *client, Request *req) {
		const unsigned int DATE_BUFFER_SIZE = 128;
		char *dateBuffer = (char *) psg_pnalloc(req->pool, DATE_BUFFER_SIZE);
		char *pos = dateBuffer;
		const char *end = dateBuffer + DATE_BUFFER_SIZE;
		time_t the_time = SystemTime::get();
		struct tm the_tm;

		SKC_WARN(client, "The server did not generate a response. Sending default 500 response");
		req->wantKeepAlive = false;

		pos = appendData(pos, end, "HTTP/1.0 500 Internal Server Error\r\n");
		pos = appendData(pos, end, "Date: ");
		gmtime_r(&the_time, &the_tm);
		pos += strftime(pos, end - pos, "%a, %d %b %Y %H:%M:%S %Z", &the_tm);
		pos = appendData(pos, end, "\r\n");

		writeResponse(client, dateBuffer, pos - dateBuffer);
		writeResponse(client, DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE);
	}

	void prepareChunkedBodyParsing(Client *client, Request *req) {
		assert(req->requestBodyType == Request::RBT_CHUNKED);
		HttpChunkedBodyParser_initialize(&req->reqParser.chunkedBodyParser, req);
	}

	void requestBodyConsumed(Client *client, Request *req) {
		if (req->requestBodyFullyRead()) {
			client->input.stop();
			req->requestBodyChannel.feed(MemoryKit::mbuf());
		}
	}


	/***** Channel callbacks *****/

	static void _onClientOutputDataFlushed(ServerKit::FileBufferedFdOutputChannel *channel) {
		Client *client = static_cast<Client *>(channel->getHooks()->userData);
		HttpServer *self = static_cast<HttpServer *>(client->getServer());
		if (client->currentRequest != NULL
		 && client->currentRequest->httpState == Request::FLUSHING_OUTPUT)
		{
			self->doneWithCurrentRequest(&client);
		}
	}

	static Channel::Result onRequestBodyChannelData(FileBufferedChannel *channel,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		Request *req     = static_cast<Request *>(channel->getHooks()->userData);
		Client *client   = static_cast<Client *>(req->client);
		HttpServer *self = static_cast<HttpServer *>(client->getServer());

		return self->onRequestBody(client, req, buffer, errcode);
	}

	static void onRequestBodyChannelBuffersFlushed(FileBufferedChannel *channel) {
		Request *req     = static_cast<Request *>(channel->getHooks()->userData);
		Client *client   = static_cast<Client *>(req->client);
		HttpServer *self = static_cast<HttpServer *>(client->getServer());

		req->requestBodyChannel.buffersFlushedCallback = NULL;
		client->input.start();
		self->requestBodyConsumed(client, req);
	}

protected:
	/***** Protected API *****/

	/** Increase request reference count. */
	void refRequest(Request *request) {
		request->refcount.fetch_add(1, boost::memory_order_relaxed);
	}

	/** Decrease request reference count. Adds request to the
	 * freelist if reference count drops to 0.
	 */
	void unrefRequest(Request *request) {
		int oldRefcount = request->refcount.fetch_sub(1, boost::memory_order_release);
		assert(oldRefcount >= 1);

		if (oldRefcount == 1) {
			boost::atomic_thread_fence(boost::memory_order_acquire);

			if (this->getContext()->libev->onEventLoopThread()) {
				requestReachedZeroRefcount(request);
			} else {
				// Let the event loop handle the request reaching the 0 refcount.
				passRequestToEventLoopThread(request);
			}
		}
	}

	void writeResponse(Client *client, const MemoryKit::mbuf &buffer) {
		client->currentRequest->responded = true;
		client->output.feed(buffer);
	}

	void writeResponse(Client *client, const char *data, unsigned int size) {
		writeResponse(client, MemoryKit::mbuf(data, size));
	}

	void writeResponse(Client *client, const StaticString &data) {
		writeResponse(client, data.data(), data.size());
	}

	bool endRequest(Client **client, Request **request) {
		Client *c = *client;
		Request *req = *request;

		*client = NULL;
		*request = NULL;

		if (req->ended()) {
			return false;
		}

		SKC_TRACE(c, 2, "Ending request");
		assert(c->currentRequest == req);

		if (OXT_UNLIKELY(!req->responded)) {
			writeDefault500Response(c, req);
		}

		deinitCurrentRequest(c, req);
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


	/***** Hook overrides *****/

	virtual void onClientObjectCreated(Client *client) {
		ParentClass::onClientObjectCreated(client);
		client->output.setDataFlushedCallback(_onClientOutputDataFlushed);
	}

	virtual void onClientAccepted(Client *client) {
		ParentClass::onClientAccepted(client);
		handleNextRequest(client);
	}

	virtual Channel::Result onClientDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		assert(client->currentRequest != NULL);
		Request *req = client->currentRequest;
		RequestRef ref(req);

		switch (req->httpState) {
		case Request::PARSING_HEADERS:
			if (errcode == 0 && !buffer.empty()) {
				size_t ret = req->reqParser.headerParser->feed(buffer);
				if (req->httpState == Request::PARSING_HEADERS) {
					// Not yet done parsing.
					return Channel::Result(buffer.size(), false);
				}

				// Done parsing.
				SKC_TRACE(client, 2, "New request received");
				headerParserObjectPool.free(req->reqParser.headerParser);
				req->reqParser.headerParser = NULL;

				switch (req->httpState) {
				case Request::COMPLETE:
					client->input.stop();
					onRequestBegin(client, req);
					return Channel::Result(ret, false);
				case Request::PARSING_BODY:
					onRequestBegin(client, req);
					return Channel::Result(ret, false);
				case Request::PARSING_CHUNKED_BODY:
					prepareChunkedBodyParsing(client, req);
					onRequestBegin(client, req);
					return Channel::Result(ret, false);
				/* case Request::UPGRADED:
					if (supportsUpgrade(client)) {
						onRequestBegin(client);
					} else {
						disconnect(&client);
					}
					return buffer.size(); */
				case Request::ERROR:
					this->disconnectWithError(&client, req->parseError);
					return Channel::Result(0, true);
				default:
					P_BUG("TODO");
					return Channel::Result(0, true);
				}
			} else {
				this->disconnect(&client);
				return Channel::Result(0, true);
			}

		case Request::PARSING_BODY:
			if (errcode != 0) {
				req->requestBodyChannel.feedError(errcode);
				return Channel::Result(0, false);
			} else if (buffer.empty()) {
				req->requestBodyChannel.feed(MemoryKit::mbuf());
				return Channel::Result(0, false);
			} else {
				boost::uint64_t maxRemaining, remaining;

				if (req->requestBodyInfo.contentLength == 0) {
					maxRemaining = std::numeric_limits<boost::uint64_t>::max();
				} else {
					maxRemaining = req->requestBodyInfo.contentLength -
						req->requestBodyAlreadyRead;
				}
				remaining = std::min<boost::uint64_t>(buffer.size(), maxRemaining);

				req->requestBodyAlreadyRead += remaining;
				req->requestBodyChannel.feed(MemoryKit::mbuf(buffer, 0, remaining));
				if (!req->ended()) {
					if (!req->requestBodyChannel.passedThreshold()) {
						requestBodyConsumed(client, req);
					} else {
						client->input.stop();
						req->requestBodyChannel.buffersFlushedCallback =
							onRequestBodyChannelBuffersFlushed;
					}
				}
				return Channel::Result(remaining, false);
			}

		case Request::PARSING_CHUNKED_BODY:
			if (!buffer.empty()) {
				Channel::Result r = HttpChunkedBodyParser_feed(&req->reqParser.chunkedBodyParser,
					buffer);
				return r;
			} else {
				HttpChunkedBodyParser_feedEof(&req->reqParser.chunkedBodyParser,
					this, client, req);
				return Channel::Result(0, false);
			}

		case Request::UPGRADED:
			P_BUG("TODO");
			return Channel::Result(0, false);

		default:
			P_BUG("Invalid request HTTP state " << toString((int) req->httpState));
			return Channel::Result(0, false);
		}
	}

	virtual void onClientDisconnecting(Client *client) {
		ParentClass::onClientDisconnecting(client);

		// Handle client being disconnect()'ed without endRequest().

		if (client->currentRequest != NULL) {
			Request *req = client->currentRequest;
			deinitCurrentRequest(client, req);
			client->currentRequest = NULL;
			unrefRequest(req);
		}
	}


	/***** New hooks *****/

	virtual void onRequestObjectCreated(Client *client, Request *req) {
		req->hooks.impl = &requestHooksImpl;
		req->hooks.userData = req;
		req->requestBodyChannel.setContext(this->getContext());
		req->requestBodyChannel.setHooks(&req->hooks);
		req->requestBodyChannel.setDataCallback(onRequestBodyChannelData);
	}

	virtual void onRequestBegin(Client *client, Request *req) {
		// Do nothing.
	}

	virtual Channel::Result onRequestBody(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		if (errcode != 0 || buffer.empty()) {
			this->disconnect(&client);
		}
		return Channel::Result(buffer.size(), false);
	}

	virtual bool supportsUpgrade(Client *client) {
		return false;
	}

public:
	HttpServer(Context *context)
		: ParentClass(context),
		  freeRequests(STAILQ_HEAD_INITIALIZER(freeRequests)),
		  freeRequestCount(0),
		  requestFreelistLimit(1024),
		  headerParserObjectPool(16, 256)
		{ }


	/***** Friend-public methods and hook implementations *****/

	void _refRequest(Request *request) {
		refRequest(request);
	}

	void _unrefRequest(Request *request) {
		unrefRequest(request);
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_SERVER_H_ */
