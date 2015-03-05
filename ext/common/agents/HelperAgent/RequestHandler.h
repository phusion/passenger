/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion
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

#ifndef _PASSENGER_REQUEST_HANDLER_H_
#define _PASSENGER_REQUEST_HANDLER_H_

//#define DEBUG_RH_EVENT_LOOP_BLOCKING

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
#include <agents/HelperAgent/RequestHandler/TurboCaching.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;
using namespace ApplicationPool2;


namespace ServerKit {
	extern const HashedStaticString HTTP_COOKIE;
	extern const HashedStaticString HTTP_SET_COOKIE;
}


class RequestHandler: public ServerKit::HttpServer<RequestHandler, Client> {
public:
	enum BenchmarkMode {
		BM_NONE,
		BM_AFTER_ACCEPT,
		BM_BEFORE_CHECKOUT,
		BM_AFTER_CHECKOUT,
		BM_RESPONSE_BEGIN,
		BM_UNKNOWN
	};

private:
	typedef ServerKit::HttpServer<RequestHandler, Client> ParentClass;
	typedef ServerKit::Channel Channel;
	typedef ServerKit::FdSinkChannel FdSinkChannel;
	typedef ServerKit::FdSourceChannel FdSourceChannel;
	typedef ServerKit::FileBufferedChannel FileBufferedChannel;
	typedef ServerKit::FileBufferedFdSinkChannel FileBufferedFdSinkChannel;

	static const unsigned int MAX_SESSION_CHECKOUT_TRY = 10;

	unsigned int statThrottleRate;
	unsigned int responseBufferHighWatermark;
	BenchmarkMode benchmarkMode: 3;
	bool singleAppMode: 1;
	bool showVersionInHeader: 1;
	bool stickySessions: 1;
	bool gracefulExit: 1;

	const VariantMap *agentsOptions;
	psg_pool_t *stringPool;
	StringKeyTable< boost::shared_ptr<Options> > poolOptionsCache;

	StaticString defaultRuby;
	StaticString loggingAgentAddress;
	StaticString loggingAgentPassword;
	StaticString defaultUser;
	StaticString defaultGroup;
	StaticString defaultServerName;
	StaticString defaultServerPort;
	StaticString serverSoftware;
	StaticString defaultStickySessionsCookieName;
	StaticString defaultVaryTurbocacheByCookie;

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

	unsigned int threadNumber;
	StaticString serverLogName;

	friend class TurboCaching<Request>;
	friend class ResponseCache<Request>;
	struct ev_check checkWatcher;
	TurboCaching<Request> turboCaching;

	#ifdef DEBUG_RH_EVENT_LOOP_BLOCKING
		struct ev_prepare prepareWatcher;
		ev_tstamp timeBeforeBlocking;
	#endif

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
	RequestHandler(ServerKit::Context *context, const VariantMap *_agentsOptions,
		unsigned int _threadNumber = 1)
		: ParentClass(context),

		  statThrottleRate(_agentsOptions->getInt("stat_throttle_rate")),
		  responseBufferHighWatermark(_agentsOptions->getInt("response_buffer_high_watermark")),
		  benchmarkMode(parseBenchmarkMode(_agentsOptions->get("benchmark_mode", false))),
		  singleAppMode(false),
		  showVersionInHeader(_agentsOptions->getBool("show_version_in_header")),
		  stickySessions(_agentsOptions->getBool("sticky_sessions")),
		  gracefulExit(_agentsOptions->getBool("server_graceful_exit")),

		  agentsOptions(_agentsOptions),
		  stringPool(psg_create_pool(1024 * 4)),
		  poolOptionsCache(4),

		  PASSENGER_APP_GROUP_NAME("!~PASSENGER_APP_GROUP_NAME"),
		  PASSENGER_MAX_REQUESTS("!~PASSENGER_MAX_REQUESTS"),
		  PASSENGER_STICKY_SESSIONS("!~PASSENGER_STICKY_SESSIONS"),
		  PASSENGER_STICKY_SESSIONS_COOKIE_NAME("!~PASSENGER_STICKY_SESSIONS_COOKIE_NAME"),
		  PASSENGER_REQUEST_OOB_WORK("!~Request-OOB-Work"),
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

		  threadNumber(_threadNumber),
		  turboCaching(getTurboCachingInitialState(_agentsOptions))
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
		defaultStickySessionsCookieName = psg_pstrdup(stringPool,
			agentsOptions->get("sticky_sessions_cookie_name"));

		if (agentsOptions->has("vary_turbocache_by_cookie")) {
			defaultVaryTurbocacheByCookie = psg_pstrdup(stringPool,
				agentsOptions->get("vary_turbocache_by_cookie"));
		}

		generateServerLogName(_threadNumber);

		if (!agentsOptions->getBool("multi_app")) {
			boost::shared_ptr<Options> options = boost::make_shared<Options>();

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

		ev_check_init(&checkWatcher, onEventLoopCheck);
		ev_set_priority(&checkWatcher, EV_MAXPRI);
		ev_check_start(getLoop(), &checkWatcher);
		checkWatcher.data = this;

		#ifdef DEBUG_RH_EVENT_LOOP_BLOCKING
			ev_prepare_init(&prepareWatcher, onEventLoopPrepare);
			ev_prepare_start(getLoop(), &prepareWatcher);
			prepareWatcher.data = this;

			timeBeforeBlocking = 0;
		#endif
	}

	~RequestHandler() {
		psg_destroy_pool(stringPool);
	}

	static BenchmarkMode parseBenchmarkMode(const StaticString mode) {
		if (mode.empty()) {
			return BM_NONE;
		} else if (mode == "after_accept") {
			return BM_AFTER_ACCEPT;
		} else if (mode == "before_checkout") {
			return BM_BEFORE_CHECKOUT;
		} else if (mode == "after_checkout") {
			return BM_AFTER_CHECKOUT;
		} else if (mode == "response_begin") {
			return BM_RESPONSE_BEGIN;
		} else {
			return BM_UNKNOWN;
		}
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

	void disconnectLongRunningConnections(const StaticString &gupid) {
		vector<Client *> clients;
		vector<Client *>::iterator v_it, v_end;
		Client *client;

		// We collect all clients in a vector so that we don't have to worry about
		// `activeClients` being mutated while we work.
		TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
			P_ASSERT_EQ(client->getConnState(), Client::ACTIVE);
			if (client->currentRequest != NULL) {
				Request *req = client->currentRequest;
				if (req->httpState >= Request::COMPLETE
				 && req->upgraded()
				 && req->session != NULL
				 && req->session->getGupid() == gupid)
				{
					if (getLogLevel() >= LVL_INFO) {
						char clientName[32];
						unsigned int size;
						const LString *host;
						StaticString hostStr;

						size = getClientName(client, clientName, sizeof(clientName));
						if (req->host != NULL) {
							host = psg_lstr_make_contiguous(req->host, req->pool);
							hostStr = StaticString(host->start->data, host->size);
						}
						P_INFO("[" << getServerName() << "] Disconnecting client " <<
							StaticString(clientName, size) << ": " <<
							hostStr << StaticString(req->path.start->data, req->path.size));
					}
					refClient(client, __FILE__, __LINE__);
					clients.push_back(client);
				}
			}
		}

		// Disconnect each eligible client.
		v_end = clients.end();
		for (v_it = clients.begin(); v_it != v_end; v_it++) {
			client = *v_it;
			Client *c = client;
			disconnect(&client);
			unrefClient(c, __FILE__, __LINE__);
		}
	}

	virtual Json::Value getConfigAsJson() const {
		Json::Value doc = ParentClass::getConfigAsJson();
		doc["single_app_mode"] = singleAppMode;
		doc["stat_throttle_rate"] = statThrottleRate;
		doc["show_version_in_header"] = showVersionInHeader;
		doc["data_buffer_dir"] = getContext()->defaultFileBufferedChannelConfig.bufferDir;
		return doc;
	}

	virtual void configure(const Json::Value &doc) {
		ParentClass::configure(doc);
		if (doc.isMember("show_version_in_header")) {
			showVersionInHeader = doc["show_version_in_header"].asBool();
		}
		if (doc.isMember("data_buffer_dir")) {
			getContext()->defaultFileBufferedChannelConfig.bufferDir =
				doc["data_buffer_dir"].asString();
		}
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = ParentClass::inspectStateAsJson();
		if (turboCaching.isEnabled()) {
			Json::Value subdoc;
			subdoc["fetches"] = turboCaching.responseCache.getFetches();
			subdoc["hits"] = turboCaching.responseCache.getHits();
			subdoc["hit_ratio"] = turboCaching.responseCache.getHitRatio();
			subdoc["stores"] = turboCaching.responseCache.getStores();
			subdoc["store_successes"] = turboCaching.responseCache.getStoreSuccesses();
			subdoc["store_success_ratio"] = turboCaching.responseCache.getStoreSuccessRatio();
			doc["turbocaching"] = subdoc;
		}
		return doc;
	}

	virtual Json::Value inspectClientStateAsJson(const Client *client) const {
		Json::Value doc = ParentClass::inspectClientStateAsJson(client);
		doc["connected_at"] = timeToJson(client->connectedAt * 1000000.0);
		return doc;
	}

	virtual Json::Value inspectRequestStateAsJson(const Request *req) const {
		Json::Value doc = ParentClass::inspectRequestStateAsJson(req);
		Json::Value flags;
		const AppResponse *resp = &req->appResponse;

		if (req->startedAt != 0) {
			doc["started_at"] = timeToJson(req->startedAt * 1000000.0);
		}
		doc["state"] = req->getStateString();
		if (req->stickySession) {
			doc["sticky_session_id"] = req->options.stickySessionId;
		}
		doc["sticky_session"] = req->stickySession;
		doc["session_checkout_try"] = req->sessionCheckoutTry;

		flags["dechunk_response"] = req->dechunkResponse;
		flags["request_body_buffering"] = req->requestBodyBuffering;
		flags["https"] = req->https;
		doc["flags"] = flags;

		if (req->requestBodyBuffering) {
			doc["body_bytes_buffered"] = byteSizeToJson(req->bodyBytesBuffered);
		}

		if (req->session != NULL) {
			Json::Value &sessionDoc = doc["session"] = Json::Value(Json::objectValue);
			const Session *session = req->session.get();

			if (req->session->isClosed()) {
				sessionDoc["closed"] = true;
			} else {
				sessionDoc["pid"] = session->getPid();
				sessionDoc["gupid"] = session->getGupid().toString();
			}
		}

		if (req->session != NULL || resp->httpState != AppResponse::PARSING_HEADERS) {
			doc["app_response_http_state"] = resp->getHttpStateString();
			doc["app_response_http_major"] = resp->httpMajor;
			doc["app_response_http_minor"] = resp->httpMinor;
			doc["app_response_want_keep_alive"] = resp->wantKeepAlive;
			doc["app_response_body_type"] = resp->getBodyTypeString();
			doc["app_response_body_fully_read"] = resp->bodyFullyRead();
			doc["app_response_body_already_read"] = byteSizeToJson(
				resp->bodyAlreadyRead);
			if (resp->httpState != AppResponse::ERROR) {
				if (resp->bodyType == AppResponse::RBT_CONTENT_LENGTH) {
					doc["app_response_content_length"] = byteSizeToJson(
						resp->aux.bodyInfo.contentLength);
				} else if (resp->bodyType == AppResponse::RBT_CHUNKED) {
					doc["app_response_end_chunk_reached"] = resp->aux.bodyInfo.endChunkReached;
				}
			} else {
				doc["app_response_parse_error"] = ServerKit::getErrorDesc(resp->aux.parseError);
			}
		}

		doc["app_source_state"] = req->appSource.inspectAsJson();
		doc["app_sink_state"] = req->appSink.inspectAsJson();

		return doc;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_REQUEST_HANDLER_H_ */
