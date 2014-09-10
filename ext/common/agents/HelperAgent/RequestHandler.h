/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2014 Phusion
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

/*
   STAGES

     Accept connect password
              |
             \|/
          Read header
              |
             \|/
       +------+------+
       |             |
       |             |
      \|/            |
     Buffer          |
     request         |
     body            |
       |             |
       |             |
      \|/            |
    Checkout <-------+
    session
       |
       |
      \|/
  Send header
    to app
       |
       |
      \|/
  Send request
   body to app



     OVERVIEW OF I/O CHANNELS, PIPES AND WATCHERS


                             OPTIONAL:                                       appOutputWatcher
                          clientBodyBuffer                                         (o)
                                 |                                                  |
    +----------+                 |             +-----------+                        |   +---------------+
    |          |     ------ clientInput -----> |  Request  | ---------------->          |               |
    |  Client  | fd                            |  Handler  |                    session |  Application  |
    |          |     <--- clientOutputPipe --- |           | <--- appInput ---          |               |
    +----------+ |                             +-----------+                            +---------------+
                 |
                (o)
        clientOutputWatcher



   REQUEST BODY HANDLING STRATEGIES

   This table describes how we should handle the request body (the part in the request
   that comes after the request header, and may include WebSocket data), given various
   factors. Strategies that are listed first have precedence.

    Method     'Upgrade'  'Content-Length' or   Application    Action
               header     'Transfer-Encoding'   socket
               present?   header present?       protocol
    ---------------------------------------------------------------------------------------------

    GET/HEAD   Y          Y                     -              Reject request[1]
    Other      Y          -                     -              Reject request[2]

    GET/HEAD   Y          N                     http_session   Set requestBodyLength=-1, keep socket open when done forwarding.
    -          N          N                     http_session   Set requestBodyLength=0, keep socket open when done forwarding.
    -          N          Y                     http_session   Keep socket open when done forwarding. If Transfer-Encoding is
                                                               chunked, rechunck the body during forwarding.

    GET/HEAD   Y          N                     session        Set requestBodyLength=-1, half-close app socket when done forwarding.
    -          N          N                     session        Set requestBodyLength=0, half-close app socket when done forwarding.
    -          N          Y                     session        Half-close app socket when done forwarding.
    ---------------------------------------------------------------------------------------------

    [1] Supporting situations in which there is both an HTTP request body and WebSocket data
        is way too complicated. The RequestHandler code is complicated enough as it is,
        so we choose not to support requests like these.
    [2] RFC 6455 states that WebSocket upgrades may only happen over GET requests.
        We don't bother supporting non-WebSocket upgrades.

 */

#ifndef _PASSENGER_REQUEST_HANDLER_H_
#define _PASSENGER_REQUEST_HANDLER_H_

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/cstdint.hpp>
#include <oxt/macros.hpp>
#include <ev++.h>
#include <ostream>

#if defined(__GLIBCXX__) || defined(__APPLE__)
	#include <cxxabi.h>
	#define CXX_ABI_API_AVAILABLE
#endif

#include <sys/types.h>
#include <sys/uio.h>
#include <utility>
#include <typeinfo>
#include <cstdio>
#include <cassert>
#include <cctype>

#include <Logging.h>
#include <MessageReadersWriters.h>
#include <Constants.h>
#include <ServerKit/Errors.h>
#include <ServerKit/HttpServer.h>
#include <ServerKit/HttpHeaderParser.h>
#include <MemoryKit/palloc.h>
#include <DataStructures/LString.h>
#include <DataStructures/StringKeyTable.h>
#include <ApplicationPool2/ErrorRenderer.h>
#include <StaticString.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>
#include <Utils/JsonUtils.h>
#include <Utils/HttpConstants.h>
#include <Utils/VariantMap.h>
#include <Utils/Timer.h>
#include <agents/HelperAgent/RequestHandler/Client.h>
#include <agents/HelperAgent/RequestHandler/AppResponse.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;
using namespace ApplicationPool2;


#define RH_LOG_EVENT(client, eventName) \
	char _clientName[32]; \
	getClientName(client, _clientName, sizeof(_clientName)); \
	TRACE_POINT_WITH_DATA(_clientName); \
	SKC_TRACE(client, 3, "Event: " eventName)


class RequestHandler: public ServerKit::HttpServer<RequestHandler, Client> {
private:
	typedef ServerKit::HttpServer<RequestHandler, Client> ParentClass;
	typedef ServerKit::Channel Channel;
	typedef ServerKit::FdInputChannel FdInputChannel;
	typedef ServerKit::FileBufferedChannel FileBufferedChannel;
	typedef ServerKit::FileBufferedFdOutputChannel FileBufferedFdOutputChannel;

	const VariantMap *agentsOptions;
	psg_pool_t *stringPool;
	StaticString defaultRuby;
	StaticString loggingAgentAddress;
	StaticString loggingAgentPassword;
	StaticString defaultUser;
	StaticString defaultGroup;
	HashedStaticString PASSENGER_APP_GROUP_NAME;
	HashedStaticString PASSENGER_MAX_REQUESTS;
	HashedStaticString PASSENGER_STICKY_SESSIONS;
	HashedStaticString PASSENGER_STICKY_SESSIONS_COOKIE_NAME;
	HashedStaticString PASSENGER_REQUEST_OOB_WORK;
	HashedStaticString UNION_STATION_SUPPORT;
	HashedStaticString REMOTE_ADDR;
	HashedStaticString FLAGS;
	HashedStaticString HTTP_COOKIE;
	HashedStaticString HTTP_DATE;
	HashedStaticString HTTP_CONTENT_LENGTH;
	HashedStaticString HTTP_EXPECT;
	HashedStaticString HTTP_CONNECTION;
	HashedStaticString HTTP_STATUS;
	HashedStaticString HTTP_TRANSFER_ENCODING;
	boost::uint32_t HTTP_CONTENT_TYPE_HASH;

	StringKeyTable< boost::shared_ptr<Options> > poolOptionsCache;
	bool singleAppMode;

public:
	ResourceLocator *resourceLocator;
	PoolPtr appPool;
	UnionStation::CorePtr unionStationCore;

protected:
	#include <agents/HelperAgent/RequestHandler/Utils.cpp>
	#include <agents/HelperAgent/RequestHandler/InitRequest.cpp>
	#include <agents/HelperAgent/RequestHandler/CheckoutSession.cpp>
	#include <agents/HelperAgent/RequestHandler/SendRequest.cpp>
	#include <agents/HelperAgent/RequestHandler/ForwardResponse.cpp>

	virtual void
	onClientAccepted(Client *client) {
		ParentClass::onClientAccepted(client);
		client->connectedAt = ev_now(getLoop());
	}

	virtual void
	onRequestObjectCreated(Client *client, Request *req) {
		ParentClass::onRequestObjectCreated(client, req);

		req->appInput.setContext(getContext());
		req->appInput.setHooks(&req->hooks);
		req->appInput.errorCallback = onAppInputError;

		req->appOutput.setContext(getContext());
		req->appOutput.setHooks(&req->hooks);
		req->appOutput.setDataCallback(_onAppOutputData);
	}

	virtual void deinitializeClient(Client *client) {
		ParentClass::deinitializeClient(client);
		client->output.setBuffersFlushedCallback(NULL);
	}

	virtual void reinitializeRequest(Client *client, Request *req) {
		ParentClass::reinitializeRequest(client, req);

		// appInput and appOutput are initialized in
		// RequestHandler::checkoutSession().

		req->state = Request::ANALYZING_REQUEST;
		req->startedAt = 0;
		req->halfCloseAppConnection = false;
		req->dechunkResponse = false;
		req->https = false;
		req->sessionCheckedOut = false;
		req->sessionCheckoutTry = 0;
		req->stickySession = false;
		req->responseHeaderSeen = false;
		req->chunkedResponse = false;
		req->responseContentLength = -1;
		req->responseBodyAlreadyRead = 0;
	}

	virtual void deinitializeRequest(Client *client, Request *req) {
		req->session.reset();
		req->responseHeaderBufferer.reset();
		req->responseDechunker.reset();
		req->endScopeLog(&req->scopeLogs.requestProxying, false);
		req->endScopeLog(&req->scopeLogs.getFromPool, false);
		req->endScopeLog(&req->scopeLogs.bufferingRequestBody, false);
		req->endScopeLog(&req->scopeLogs.requestProcessing, false);

		req->appInput.deinitialize();
		req->appInput.setBuffersFlushedCallback(NULL);
		req->appInput.setDataFlushedCallback(NULL);
		req->appOutput.deinitialize();

		deinitializeAppResponse(client, req);

		ParentClass::deinitializeRequest(client, req);
	}

	void reinitializeAppResponse(Client *client, Request *req) {
		AppResponse *resp = &req->appResponse;

		resp->httpMajor = 1;
		resp->httpMinor = 0;
		resp->httpState = AppResponse::PARSING_HEADERS;
		resp->bodyType  = AppResponse::RBT_NO_BODY;
		resp->wantKeepAlive = false;
		resp->oneHundredContinueSent = false;
		resp->statusCode = 0;
		resp->parserState.headerParser = getHeaderParserStatePool().construct();
		createAppResponseHeaderParser(getContext(), req).initialize();
		resp->aux.bodyInfo.contentLength = 0; // Sets the entire union to 0.
		resp->bodyAlreadyRead = 0;
	}

	void deinitializeAppResponse(Client *client, Request *req) {
		AppResponse *resp = &req->appResponse;

		if (resp->httpState == AppResponse::PARSING_HEADERS
		 && resp->parserState.headerParser != NULL)
		{
			getHeaderParserStatePool().destroy(resp->parserState.headerParser);
			resp->parserState.headerParser = NULL;
		}

		ServerKit::HeaderTable::Iterator it(resp->headers);
		while (*it != NULL) {
			psg_lstr_deinit(&it->header->key);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		it = ServerKit::HeaderTable::Iterator(resp->secureHeaders);
		while (*it != NULL) {
			psg_lstr_deinit(&it->header->key);
			psg_lstr_deinit(&it->header->val);
			it.next();
		}

		resp->headers.clear();
		resp->secureHeaders.clear();
	}

	virtual bool shouldDisconnectClientOnShutdown(Client *client) {
		return client->currentRequest == NULL
			|| client->currentRequest->httpState == Request::PARSING_HEADERS
			|| client->currentRequest->upgraded();
	}

public:
	RequestHandler(ServerKit::Context *context, const VariantMap *_agentsOptions)
		: ParentClass(context),
		  agentsOptions(_agentsOptions),
		  stringPool(psg_create_pool(1024 * 4)),
		  PASSENGER_APP_GROUP_NAME("!~PASSENGER_APP_GROUP_NAME"),
		  PASSENGER_MAX_REQUESTS("!~PASSENGER_MAX_REQUESTS"),
		  PASSENGER_STICKY_SESSIONS("!~PASSENGER_STICKY_SESSIONS"),
		  PASSENGER_STICKY_SESSIONS_COOKIE_NAME("!~PASSENGER_STICKY_SESSIONS_COOKIE_NAME"),
		  PASSENGER_REQUEST_OOB_WORK("!~request-oob-work"),
		  UNION_STATION_SUPPORT("!~UNION_STATION_SUPPORT"),
		  REMOTE_ADDR("!~REMOTE_ADDR"),
		  FLAGS("!~FLAGS"),
		  HTTP_COOKIE("cookie"),
		  HTTP_DATE("date"),
		  HTTP_CONTENT_LENGTH("content-length"),
		  HTTP_EXPECT("expect"),
		  HTTP_CONNECTION("connection"),
		  HTTP_STATUS("status"),
		  HTTP_TRANSFER_ENCODING("transfer-encoding"),
		  HTTP_CONTENT_TYPE_HASH(HashedStaticString("content-type").hash()),
		  poolOptionsCache(4),
		  singleAppMode(false)
	{
		defaultRuby = psg_pstrdup(stringPool,
			agentsOptions->get("default_ruby"));
		loggingAgentAddress = psg_pstrdup(stringPool,
			agentsOptions->get("logging_agent_address", false));
		loggingAgentPassword = psg_pstrdup(stringPool,
			agentsOptions->get("logging_agent_password", false));
		defaultUser = psg_pstrdup(stringPool,
			agentsOptions->get("default_user", false));
		defaultGroup = psg_pstrdup(stringPool,
			agentsOptions->get("default_group", false));

		if (!agentsOptions->getBool("multi_app")) {
			boost::shared_ptr<Options> options = make_shared<Options>();

			singleAppMode = true;
			fillPoolOptionsFromAgentsOptions(*options);

			options->appRoot = psg_pstrdup(stringPool,
				agentsOptions->get("app_root"));
			options->environment = psg_pstrdup(stringPool,
				agentsOptions->get("environment"));
			options->appType = psg_pstrdup(stringPool,
				agentsOptions->get("app_type"));
			options->startupFile = psg_pstrdup(stringPool,
				agentsOptions->get("startup_file"));
			poolOptionsCache.insert(options->getAppGroupName(), options);
		}
	}

	~RequestHandler() {
		psg_destroy_pool(stringPool);
	}

	void initialize() {
		TRACE_POINT();
		if (resourceLocator == NULL) {
			throw RuntimeException("ResourceLocator not initialized");
		}
		if (appPool == NULL) {
			throw RuntimeException("AppPool not initialized");
		}
		if (unionStationCore == NULL) {
			unionStationCore = appPool->getUnionStationCore();
		}
	}

	virtual Json::Value getConfigAsJson() const {
		Json::Value doc = ParentClass::getConfigAsJson();
		doc["single_app_mode"] = singleAppMode;
		return doc;
	}

	virtual Json::Value inspectClientStateAsJson(const Client *client) const {
		Json::Value doc = ParentClass::inspectClientStateAsJson(client);
		doc["connected_at"] = timeToJson(client->connectedAt * 1000000.0);
		return doc;
	}

	virtual Json::Value inspectRequestStateAsJson(const Request *req) const {
		Json::Value doc = ParentClass::inspectRequestStateAsJson(req);

		doc["state"] = req->getStateString();
		doc["started_at"] = timeToJson(req->startedAt * 1000000.0);
		doc["sticky_session"] = req->stickySession;
		if (req->stickySession) {
			doc["sticky_session_id"] = req->options.stickySessionId;
		}

		if (req->session != NULL) {
			Json::Value &sessionDoc = doc["session"] = Json::Value(Json::objectValue);
			const Session *session = req->session.get();
			const AppResponse *resp = &req->appResponse;

			if (req->session->isClosed()) {
				sessionDoc["closed"] = true;
			} else {
				sessionDoc["pid"] = session->getPid();
				sessionDoc["gupid"] = session->getGupid().toString();
			}

			doc["app_response_http_state"] = resp->getHttpStateString();
			doc["app_response_http_major"] = resp->httpMajor;
			doc["app_response_http_minor"] = resp->httpMinor;
			doc["app_response_want_keep_alive"] = resp->wantKeepAlive;
			doc["app_response_body_type"] = resp->getBodyTypeString();
			doc["app_response_body_fully_read"] = resp->bodyFullyRead();
			doc["app_response_body_already_read"] = resp->bodyAlreadyRead;
			if (resp->httpState != AppResponse::ERROR) {
				if (resp->bodyType == AppResponse::RBT_CONTENT_LENGTH) {
					doc["app_response_content_length"] = resp->aux.bodyInfo.contentLength;
				} else if (resp->bodyType == AppResponse::RBT_CHUNKED) {
					doc["app_response_end_chunk_reached"] = resp->aux.bodyInfo.endChunkReached;
				}
			} else {
				doc["parse_error"] = ServerKit::getErrorDesc(resp->aux.parseError);
			}
		}

		return doc;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_REQUEST_HANDLER_H_ */
