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
#include <algorithm>
#include <cassert>
#include <pthread.h>
#include <ServerKit/Server.h>
#include <ServerKit/HttpClient.h>
#include <ServerKit/HttpRequest.h>
#include <ServerKit/HttpRequestRef.h>
#include <ServerKit/HttpHeaderParser.h>

namespace Passenger {
namespace ServerKit {

using namespace boost;


template< typename DerivedServer, typename Client = HttpClient<HttpRequest> >
class HttpServer: public Server<DerivedServer, Client> {
public:
	typedef typename Client::RequestType Request;
	typedef HttpRequestRef<DerivedServer, Request> RequestRef;
	STAILQ_HEAD(FreeRequestList, Request);

	FreeRequestList freeRequests;
	unsigned int freeRequestCount, requestFreelistLimit;

private:
	/***** Types *****/
	typedef Server<DerivedServer, Client> ParentClass;

	/***** Working state *****/
	object_pool<HttpHeaderParser> headerParserObjectPool;


	Request *checkoutRequestObject() {
		// Try to obtain request object from freelist.
		if (!STAILQ_EMPTY(&freeRequests)) {
			return checkoutRequestObjectFromFreelist();
		} else {
			return createNewRequestObject();
		}
	}

	Request *checkoutRequestObjectFromFreelist() {
		assert(freeRequestCount > 0);
		Request *request = STAILQ_FIRST(&freeRequests);
		assert(request->httpState == Request::IN_FREELIST);
		freeRequestCount--;
		STAILQ_REMOVE_HEAD(&freeRequests, nextRequest.freeRequest);
		return request;
	}

	Request *createNewRequestObject() {
		Request *request;
		try {
			request = new Request();
		} catch (const std::bad_alloc &) {
			return NULL;
		}
		onRequestObjectCreated(request);
		return request;
	}

	void requestReachedZeroRefcount(Request *request) {
		Client *client = static_cast<Client *>(request->client);
		assert(request->httpState == Request::WAITING_FOR_REFERENCES);
		assert(client->endedRequestCount > 0);
		assert(!LIST_EMPTY(&client->endedRequests));

		LIST_REMOVE(request, nextRequest.endedRequest);
		assert(client->endedRequestCount > 0);
		client->endedRequestCount--;
		request->client = NULL;

		if (!addRequestToFreelist(request)) {
			delete request;
		}

		this->unrefClient(client);
	}

	bool addRequestToFreelist(Request *request) {
		if (freeRequestCount < requestFreelistLimit) {
			STAILQ_INSERT_HEAD(&freeRequests, request, nextRequest.freeRequest);
			freeRequestCount++;
			int prevref = request->refcount.fetch_add(1, boost::memory_order_relaxed);
			assert(prevref == 0);
			(void) prevref;
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


	static void _onClientOutputFlushed(ServerKit::FileBufferedFdOutputChannel *channel) {
		Client *client = static_cast<Client *>(channel->getHooks()->userData);
		HttpServer *self = static_cast<HttpServer *>(client->getServer());
		self->onClientOutputFlushed(client);
	}

	void onClientOutputFlushed(Client *client) {
		if (client->currentRequest != NULL
		 && client->currentRequest->httpState == Request::FLUSHING_OUTPUT)
		{
			doneWithCurrentRequest(client);
		}
	}

	void doneWithCurrentRequest(Client *client) {
		assert(client->currentRequest != NULL);
		bool keepAlive = client->currentRequest->keepAlive;

		client->currentRequest->httpState = Request::WAITING_FOR_REFERENCES;
		unrefRequest(client->currentRequest);
		client->currentRequest = NULL;
		if (keepAlive) {
			handleNextRequest(client);
		} else {
			this->disconnect(&client);
		}
	}

	void handleNextRequest(Client *client) {
		client->input.start();
		client->currentRequest = checkoutRequestObject();
		client->currentRequest->client = client;
		client->currentRequest->reinitialize();
		client->reqHeaderParser = headerParserObjectPool.construct(
			this->getContext(), client->currentRequest);
		this->refClient(client);
	}

	void prepareChunkedBodyParsing(Client *client) {
		// TODO
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

			if (pthread_equal(pthread_self(),
			    this->getContext()->libev->getCurrentThread()))
			{
				requestReachedZeroRefcount(request);
			} else {
				// Let the event loop handle the request reaching the 0 refcount.
				passRequestToEventLoopThread(request);
			}
		}
	}


	/***** Hook overrides *****/

	virtual void onClientObjectCreated(Client *client) {
		ParentClass::onClientObjectCreated(client);
		client->output.setFlushedCallback(_onClientOutputFlushed);
	}

	virtual void onClientAccepted(Client *client) {
		ParentClass::onClientAccepted(client);
		handleNextRequest(client);
	}

	virtual int onClientDataReceived(Client *client, const MemoryKit::mbuf &buffer,
		int errcode)
	{
		assert(client->currentRequest != NULL);
		Request *req = client->currentRequest;

		switch (req->httpState) {
		case Request::PARSING_HEADERS:
			if (errcode != 0 || buffer.is_null()) {
				req->keepAlive = false;
				endRequest(&req);
				return 0;
			} else {
				size_t ret = client->reqHeaderParser->feed(buffer);
				if (req->httpState == Request::PARSING_HEADERS) {
					// Not yet done parsing.
					return buffer.size();
				}

				// Done parsing.
				headerParserObjectPool.free(client->reqHeaderParser);
				client->reqHeaderParser = NULL;

				switch (req->httpState) {
				case Request::COMPLETE:
					client->input.stop();
					onRequestBegin(client, req);
					return buffer.size();
				/* case PARSING_BODY:
					onRequestBegin(client);
					return buffer.size();
				case PARSING_CHUNKED_BODY:
					prepareChunkedBodyParsing(client);
					onRequestBegin(client);
					return buffer.size();
				case UPGRADED:
					if (supportsUpgrade(client)) {
						onRequestBegin(client);
					} else {
						disconnect(&client);
					}
					return buffer.size(); */
				case Request::ERROR:
					// TODO: auto-detect errors and set keepalive to false
					req->keepAlive = false;
					endRequest(&req);
					this->disconnect(&client);
					return 0;
				default:
					P_BUG("TODO");
					break;
				}
				return ret;
			}

		case Request::PARSING_BODY: {
			/*
			if () {

			}

			uint64_t maxRemaining = client->req.contentLength -
				client->req.contentAlreadyRead;
			uint64_t remaining = std::min<uint64_t>(buffer.size(),
				maxRemaining);

			client->req.bodyChannel.feed(MemoryKit::mbuf(buf, 0, remaining),
				errcode);
			if (client->req.bodyChannel.acceptingInput()) {

			} else {
				client->input.stop();
				client->req.bodyChannel.idleCallback = function() {
					client->req.bodyChannel.idleCallback = NULL;
					client->input.start();
				}
				return -1;
			} else {
				return buffer.size();
			}

			function onBodyChannelData(buffer, errcode) {
				return server->onRequestBody(buffer, errcode);
			}

			if (errcode != 0) {
				return onRequestBody(client, MemoryKit::mbuf(), errcode);
			} else {
				uint64_t maxRemaining = client->req.contentLength -
					client->req.contentAlreadyRead;
				uint64_t remaining = std::min<uint64_t>(buffer.size(), maxRemaining);
				int ret = onRequestBody(client, MemoryKit::mbuf(buf, 0, remaining), 0);
				if (ret > 0) {
					handleRequestBodyConsumed(client, ret);
				}
				return ret;
			}
			*/
			break;
		}

		case Request::PARSING_CHUNKED_BODY:
			// TODO
			break;
		case Request::UPGRADED:
			// TODO
			//return onRequestBody(client, buffer, errcode);
			break;
		default:
			P_BUG("Invalid request HTTP state " << toString((int) req->httpState));
			return 0;
		}
		P_BUG("TODO");
		return 0;
	}
/*
	void handleRequestBodyConsumed(Client *client, unsigned int size) {
		assert(client->req.httpState == Request::PARSING_BODY
			|| client->req.httpState == Request::PARSING_CHUNKED_BODY);
		client->req.contentAlreadyRead += size;
		assert(client->req.contentAlreadyRead <= client->req.contentLength);
		if (client->req.contentAlreadyRead == client->req.contentLength) {
			client->req.httpState = Request::DONE;
			onRequestBody(client, MemoryKit::mbuf(), 0);
		}
	}

	void requestBodyConsumed(Client *client, unsigned int size) {
		handleRequestBodyConsumed(client, size);
		client->input.consumed(size);
	}
*/


	/***** New hooks *****/

	virtual void onRequestObjectCreated(Request *request) {
		// Do nothing.
	}

	virtual void onRequestBegin(Client *client, Request *request) {
		// Do nothing.
	}
/*
	virtual int onRequestBody(Client *client, const MemoryKit::mbuf &buffer, int errcode) {
		if (errcode != 0 || buffer.is_null()) {
			disconnect(&client);
		}
		return buf.size();
	}
*/
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

	void endRequest(Request **request) {
		Request *req = *request;
		if (req->ended()) {
			return;
		}

		Client *client = static_cast<Client *>(req->client);
		assert(client->currentRequest == req);

		if (client->reqHeaderParser != NULL) {
			headerParserObjectPool.free(client->reqHeaderParser);
			client->reqHeaderParser = NULL;
		}

		LIST_INSERT_HEAD(&client->endedRequests, req,
			nextRequest.endedRequest);
		client->endedRequestCount++;

		req->deinitialize();
		*request = NULL;

		if (req->client->output.writing()) {
			req->httpState = Request::FLUSHING_OUTPUT;
		} else {
			doneWithCurrentRequest(client);
		}
	}


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
