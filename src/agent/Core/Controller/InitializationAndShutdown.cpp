/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2015 Phusion Holding B.V.
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
 * Initialization and shutdown-related code for Core::Controller
 *
 *************************************************************************/

namespace Passenger {
namespace Core {

using namespace std;
using namespace boost;


/****************************
 *
 * Public methods
 *
 ****************************/


Controller::Controller(ServerKit::Context *context, const VariantMap *_agentsOptions,
	unsigned int _threadNumber)
	: ParentClass(context),

	  statThrottleRate(_agentsOptions->getInt("stat_throttle_rate")),
	  responseBufferHighWatermark(_agentsOptions->getInt("response_buffer_high_watermark")),
	  benchmarkMode(parseBenchmarkMode(_agentsOptions->get("benchmark_mode", false))),
	  singleAppMode(false),
	  showVersionInHeader(_agentsOptions->getBool("show_version_in_header")),
	  stickySessions(_agentsOptions->getBool("sticky_sessions")),
	  gracefulExit(_agentsOptions->getBool("core_graceful_exit")),

	  agentsOptions(_agentsOptions),
	  stringPool(psg_create_pool(1024 * 4)),
	  poolOptionsCache(4),

	  PASSENGER_APP_GROUP_NAME("!~PASSENGER_APP_GROUP_NAME"),
	  PASSENGER_ENV_VARS("!~PASSENGER_ENV_VARS"),
	  PASSENGER_MAX_REQUESTS("!~PASSENGER_MAX_REQUESTS"),
	  PASSENGER_SHOW_VERSION_IN_HEADER("!~PASSENGER_SHOW_VERSION_IN_HEADER"),
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
	ustRouterAddress = psg_pstrdup(stringPool,
		agentsOptions->get("ust_router_address", false));
	ustRouterPassword = psg_pstrdup(stringPool,
		agentsOptions->get("ust_router_password", false));
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

	#ifdef DEBUG_CC_EVENT_LOOP_BLOCKING
		ev_prepare_init(&prepareWatcher, onEventLoopPrepare);
		ev_prepare_start(getLoop(), &prepareWatcher);
		prepareWatcher.data = this;

		timeBeforeBlocking = 0;
	#endif
}

Controller::~Controller() {
	ev_check_stop(getLoop(), &checkWatcher);
	psg_destroy_pool(stringPool);
}

void
Controller::initialize() {
	TRACE_POINT();
	if (resourceLocator == NULL) {
		throw RuntimeException("ResourceLocator not initialized");
	}
	if (appPool == NULL) {
		throw RuntimeException("AppPool not initialized");
	}
	if (unionStationContext == NULL) {
		unionStationContext = appPool->getUnionStationContext();
	}
}


} // namespace Core
} // namespace Passenger
