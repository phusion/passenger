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
	typedef ServerKit::FdSourceChannel FdSourceChannel;
	typedef ServerKit::FileBufferedChannel FileBufferedChannel;
	typedef ServerKit::FileBufferedFdSinkChannel FileBufferedFdSinkChannel;

	static const unsigned int MAX_SESSION_CHECKOUT_TRY = 10;

	const VariantMap *agentsOptions;
	psg_pool_t *stringPool;
	StaticString defaultRuby;
	StaticString loggingAgentAddress;
	StaticString loggingAgentPassword;
	StaticString defaultUser;
	StaticString defaultGroup;
	StaticString defaultServerName;
	StaticString defaultServerPort;
	StaticString serverSoftware;
	HashedStaticString PASSENGER_APP_GROUP_NAME;
	HashedStaticString PASSENGER_MAX_REQUESTS;
	HashedStaticString PASSENGER_STICKY_SESSIONS;
	HashedStaticString PASSENGER_STICKY_SESSIONS_COOKIE_NAME;
	HashedStaticString PASSENGER_REQUEST_OOB_WORK;
	HashedStaticString UNION_STATION_SUPPORT;
	HashedStaticString REMOTE_ADDR;
	HashedStaticString REMOTE_PORT;
	HashedStaticString REMOTE_USER;
	HashedStaticString FLAGS;
	HashedStaticString HTTP_COOKIE;
	HashedStaticString HTTP_DATE;
	HashedStaticString HTTP_HOST;
	HashedStaticString HTTP_CONTENT_LENGTH;
	HashedStaticString HTTP_CONTENT_TYPE;
	HashedStaticString HTTP_EXPECT;
	HashedStaticString HTTP_CONNECTION;
	HashedStaticString HTTP_STATUS;
	HashedStaticString HTTP_TRANSFER_ENCODING;

	StringKeyTable< boost::shared_ptr<Options> > poolOptionsCache;
	bool singleAppMode: 1;
	bool showVersionInHeader: 1;

public:
	ResourceLocator *resourceLocator;
	PoolPtr appPool;
	UnionStation::CorePtr unionStationCore;

protected:
	#include <agents/HelperAgent/RequestHandler/Utils.cpp>
	#include <agents/HelperAgent/RequestHandler/Hooks.cpp>
	#include <agents/HelperAgent/RequestHandler/InitRequest.cpp>
	#include <agents/HelperAgent/RequestHandler/BufferBody.cpp>
	#include <agents/HelperAgent/RequestHandler/CheckoutSession.cpp>
	#include <agents/HelperAgent/RequestHandler/SendRequest.cpp>
	#include <agents/HelperAgent/RequestHandler/ForwardResponse.cpp>

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
		  REMOTE_PORT("!~REMOTE_PORT"),
		  REMOTE_USER("!~REMOTE_USER"),
		  FLAGS("!~FLAGS"),
		  HTTP_COOKIE("cookie"),
		  HTTP_DATE("date"),
		  HTTP_HOST("host"),
		  HTTP_CONTENT_LENGTH("content-length"),
		  HTTP_CONTENT_TYPE("content-type"),
		  HTTP_EXPECT("expect"),
		  HTTP_CONNECTION("connection"),
		  HTTP_STATUS("status"),
		  HTTP_TRANSFER_ENCODING("transfer-encoding"),
		  poolOptionsCache(4),
		  singleAppMode(false),
		  showVersionInHeader(false)
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
		defaultServerName = psg_pstrdup(stringPool,
			agentsOptions->get("default_server_name"));
		defaultServerPort = psg_pstrdup(stringPool,
			agentsOptions->get("default_server_port"));
		serverSoftware = psg_pstrdup(stringPool,
			agentsOptions->get("server_software"));

		showVersionInHeader = agentsOptions->getBool(
			"show_version_in_header");

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

		doc["started_at"] = timeToJson(req->startedAt * 1000000.0);
		doc["state"] = req->getStateString();
		doc["dechunk_response"] = req->dechunkResponse;
		doc["https"] = req->https;
		doc["sticky_session"] = req->stickySession;
		if (req->stickySession) {
			doc["sticky_session_id"] = req->options.stickySessionId;
		}
		doc["session_checkout_try"] = req->sessionCheckoutTry;

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
