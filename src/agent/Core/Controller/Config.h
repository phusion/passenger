/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_CORE_CONTROLLER_CONFIG_H_
#define _PASSENGER_CORE_CONTROLLER_CONFIG_H_

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <string.h>

#include <ConfigKit/ConfigKit.h>
#include <ConfigKit/ValidationUtils.h>
#include <MemoryKit/palloc.h>
#include <ServerKit/HttpServer.h>
#include <Constants.h>
#include <StaticString.h>
#include <Utils.h>

namespace Passenger {
namespace Core {

using namespace std;


enum ControllerBenchmarkMode {
	BM_NONE,
	BM_AFTER_ACCEPT,
	BM_BEFORE_CHECKOUT,
	BM_AFTER_CHECKOUT,
	BM_RESPONSE_BEGIN,
	BM_UNKNOWN
};

inline ControllerBenchmarkMode
parseControllerBenchmarkMode(const StaticString &mode) {
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

class ControllerSchema: public ServerKit::HttpServerSchema {
private:
	void initialize() {
		using namespace ConfigKit;

		add("thread_number", UINT_TYPE, REQUIRED | READ_ONLY);
		add("multi_app", BOOL_TYPE, OPTIONAL | READ_ONLY, true);
		add("turbocaching", BOOL_TYPE, OPTIONAL | READ_ONLY, true);
		add("integration_mode", STRING_TYPE, OPTIONAL | READ_ONLY, DEFAULT_INTEGRATION_MODE);

		add("user_switching", BOOL_TYPE, OPTIONAL, true);
		add("stat_throttle_rate", UINT_TYPE, OPTIONAL, DEFAULT_STAT_THROTTLE_RATE);
		add("show_version_in_header", BOOL_TYPE, OPTIONAL, true);
		add("data_buffer_dir", STRING_TYPE, OPTIONAL, getSystemTempDir());
		add("response_buffer_high_watermark", UINT_TYPE, OPTIONAL, DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK);
		add("sticky_sessions", BOOL_TYPE, OPTIONAL, false);
		add("core_graceful_exit", BOOL_TYPE, OPTIONAL, true);
		add("benchmark_mode", STRING_TYPE, OPTIONAL);

		add("default_ruby", STRING_TYPE, OPTIONAL, DEFAULT_RUBY);
		add("default_python", STRING_TYPE, OPTIONAL, DEFAULT_PYTHON);
		add("default_nodejs", STRING_TYPE, OPTIONAL, DEFAULT_NODEJS);
		add("ust_router_address", STRING_TYPE, OPTIONAL);
		add("ust_router_password", STRING_TYPE, OPTIONAL);
		add("default_user", STRING_TYPE, OPTIONAL, DEFAULT_WEB_APP_USER);
		addWithDynamicDefault(
			"default_group", STRING_TYPE, OPTIONAL | CACHE_DEFAULT_VALUE,
			inferDefaultValueForDefaultGroup);
		add("default_server_name", STRING_TYPE, REQUIRED);
		add("default_server_port", STRING_TYPE, REQUIRED);
		add("server_software", STRING_TYPE, OPTIONAL, SERVER_TOKEN_NAME "/" PASSENGER_VERSION);
		add("sticky_sessions_cookie_name", STRING_TYPE, OPTIONAL, DEFAULT_STICKY_SESSIONS_COOKIE_NAME);
		add("vary_turbocache_by_cookie", STRING_TYPE, OPTIONAL);

		add("friendly_error_pages", STRING_TYPE, OPTIONAL, "auto");
		add("spawn_method", STRING_TYPE, OPTIONAL, DEFAULT_SPAWN_METHOD);
		add("meteor_app_settings", STRING_TYPE, OPTIONAL);
		add("app_file_descriptor_ulimit", UINT_TYPE, OPTIONAL);
		add("min_instances", UINT_TYPE, OPTIONAL, 1);
		add("max_preloader_idle_time", UINT_TYPE, OPTIONAL, DEFAULT_MAX_PRELOADER_IDLE_TIME);
		add("max_request_queue_size", UINT_TYPE, OPTIONAL, DEFAULT_MAX_REQUEST_QUEUE_SIZE);
		add("force_max_concurrent_requests_per_process", INT_TYPE, OPTIONAL, -1);
		add("abort_websockets_on_process_shutdown", BOOL_TYPE, OPTIONAL, true);
		add("load_shell_envvars", BOOL_TYPE, OPTIONAL, false);
		add("max_requests", UINT_TYPE, OPTIONAL, 0);

		// Single app mode options
		add("app_root", STRING_TYPE, OPTIONAL);
		add("environment", STRING_TYPE, OPTIONAL, DEFAULT_APP_ENV);
		add("app_type", STRING_TYPE, OPTIONAL);
		add("startup_file", STRING_TYPE, OPTIONAL);


		/*******************/
		/*******************/


		addValidator(validate);
		addValidator(validateMultiAppMode);
		addValidator(validateSingleAppMode);
		addValidator(ConfigKit::validateIntegrationMode);
	}

	static Json::Value inferDefaultValueForDefaultGroup(const ConfigKit::Store &config) {
		struct passwd *userEntry = getpwnam(config["default_user"].asCString());
		if (userEntry == NULL) {
			throw ConfigurationException(
				"The user that PassengerDefaultUser refers to, '" +
				config["default_user"].asString() + "', does not exist.");
		}
		return getGroupName(userEntry->pw_gid);
	}

	static void validate(const ConfigKit::Store &config,
		vector<ConfigKit::Error> &errors)
	{
		using namespace ConfigKit;

		ControllerBenchmarkMode mode = parseControllerBenchmarkMode(
			config["benchmark_mode"].asString());
		if (mode == BM_UNKNOWN) {
			errors.push_back(Error("'{{benchmark_mode}}' is not set to a valid value"));
		}
	}

	static void validateMultiAppMode(const ConfigKit::Store &config,
		vector<ConfigKit::Error> &errors)
	{
		using namespace ConfigKit;

		if (!config["multi_app"].asBool()) {
			return;
		}

		if (!config["app_root"].isNull()) {
			errors.push_back(Error("If '{{multi_app}}' is set"
				" then '{{app_root}}' may not be set"));
		}
	}

	static void validateSingleAppMode(const ConfigKit::Store &config,
		vector<ConfigKit::Error> &errors)
	{
		using namespace ConfigKit;

		if (config["multi_app"].asBool()) {
			return;
		}

		if (config["app_root"].isNull()) {
			errors.push_back(Error("If '{{multi_app}}' is not set"
				" then '{{app_root}}' is required"));
		}
		if (config["app_type"].isNull()) {
			errors.push_back(Error("If '{{multi_app}}' is not set"
				" then '{{app_type}}' is required"));
		}
		if (config["startup_file"].isNull()) {
			errors.push_back(Error("If '{{multi_app}}' is not set"
				" then '{{startup_file}}' is required"));
		}

		if (!config["app_type"].isNull()) {
			PassengerAppType appType = getAppType(config["app_type"].asString());
			if (appType == PAT_NONE || appType == PAT_ERROR) {
				string message = "'{{app_type}}' is set to '"
					+ config["app_type"].asString() + "', which is not a"
					" valid application type. Supported app types are:";
				const AppTypeDefinition *definition = &appTypeDefinitions[0];
				while (definition->type != PAT_NONE) {
					message.append(1, ' ');
					message.append(definition->name);
					definition++;
				}
				errors.push_back(Error(message));
			}

			if (config["startup_file"].isNull()) {
				errors.push_back(Error("If '{{app_type}}' is set"
					" then '{{startup_file}}' is required"));
			}
		}

		/*******************/
	}

public:
	ControllerSchema()
		: ServerKit::HttpServerSchema(false)
	{
		initialize();
		finalize();
	}

	ControllerSchema(bool _subclassing)
		: ServerKit::HttpServerSchema(false)
	{
		initialize();
	}
};

/**
 * A structure that caches controller configuration which is allowed to
 * change at any time, even during the middle of a request.
 */
class ControllerMainConfig {
private:
	StaticString createServerLogName() {
		string name = "ServerThr." + toString(threadNumber);
		return psg_pstrdup(pool, name);
	}

public:
	psg_pool_t *pool;

	unsigned int threadNumber;
	unsigned int statThrottleRate;
	unsigned int responseBufferHighWatermark;
	StaticString integrationMode;
	StaticString serverLogName;
	ControllerBenchmarkMode benchmarkMode: 3;
	bool userSwitching: 1;
	bool stickySessions: 1;
	bool gracefulExit: 1;

	/*******************/
	/*******************/

	ControllerMainConfig(const ConfigKit::Store &config)
		: pool(psg_create_pool(1024)),

		  threadNumber(config["thread_number"].asUInt()),
		  statThrottleRate(config["stat_throttle_rate"].asUInt()),
		  responseBufferHighWatermark(config["response_buffer_high_watermark"].asUInt()),
		  integrationMode(psg_pstrdup(pool, config["integration_mode"].asString())),
		  serverLogName(createServerLogName()),
		  benchmarkMode(parseControllerBenchmarkMode(config["benchmark_mode"].asString())),
		  userSwitching(config["user_switching"].asBool()),
		  stickySessions(config["sticky_sessions"].asBool()),
		  gracefulExit(config["core_graceful_exit"].asBool())

		  /*******************/
	{
		/*******************/
	}

	~ControllerMainConfig() {
		psg_destroy_pool(pool);
	}

	void swap(ControllerMainConfig &other) BOOST_NOEXCEPT_OR_NOTHROW {
		#define SWAP_BITFIELD(Type, name) \
			do { \
				Type tmp = name; \
				name = other.name; \
				other.name = tmp; \
			} while (false)

		std::swap(pool, other.pool);
		std::swap(threadNumber, other.threadNumber);
		std::swap(statThrottleRate, other.statThrottleRate);
		std::swap(responseBufferHighWatermark, other.responseBufferHighWatermark);
		std::swap(integrationMode, other.integrationMode);
		std::swap(serverLogName, other.serverLogName);
		SWAP_BITFIELD(ControllerBenchmarkMode, benchmarkMode);
		SWAP_BITFIELD(bool, userSwitching);
		SWAP_BITFIELD(bool, stickySessions);
		SWAP_BITFIELD(bool, gracefulExit);

		/*******************/

		#undef SWAP_BITFIELD
	}
};

/**
 * A structure that caches controller configuration that must stay the
 * same for the entire duration of a request.
 *
 * Note that this structure has got nothing to do with per-request config
 * options: options which may be configured by the web server on a
 * per-request basis. That is an orthogonal concept.
 */
class ControllerRequestConfig:
	public boost::intrusive_ref_counter<ControllerRequestConfig,
		boost::thread_unsafe_counter>
{
public:
	psg_pool_t *pool;

	StaticString defaultRuby;
	StaticString defaultPython;
	StaticString defaultNodejs;
	StaticString ustRouterAddress;
	StaticString ustRouterPassword;
	StaticString defaultUser;
	StaticString defaultGroup;
	StaticString defaultServerName;
	StaticString defaultServerPort;
	StaticString serverSoftware;
	StaticString defaultStickySessionsCookieName;
	StaticString defaultVaryTurbocacheByCookie;

	StaticString friendlyErrorPages;
	StaticString spawnMethod;
	StaticString meteorAppSettings;
	unsigned int fileDescriptorUlimit;
	unsigned int minInstances;
	unsigned int maxPreloaderIdleTime;
	unsigned int maxRequestQueueSize;
	unsigned int maxRequests;
	int forceMaxConcurrentRequestsPerProcess;
	bool singleAppMode: 1;
	bool showVersionInHeader: 1;
	bool abortWebsocketsOnProcessShutdown;
	bool loadShellEnvvars;

	/*******************/
	/*******************/


	ControllerRequestConfig(const ConfigKit::Store &config)
		: pool(psg_create_pool(1024 * 4)),

		  defaultRuby(psg_pstrdup(pool, config["default_ruby"].asString())),
		  defaultPython(psg_pstrdup(pool, config["default_python"].asString())),
		  defaultNodejs(psg_pstrdup(pool, config["default_nodejs"].asString())),
		  ustRouterAddress(psg_pstrdup(pool, config["ust_router_address"].asString())),
		  ustRouterPassword(psg_pstrdup(pool, config["ust_router_password"].asString())),
		  defaultUser(psg_pstrdup(pool, config["default_user"].asString())),
		  defaultGroup(psg_pstrdup(pool, config["default_group"].asString())),
		  defaultServerName(psg_pstrdup(pool, config["default_server_name"].asString())),
		  defaultServerPort(psg_pstrdup(pool, config["default_server_port"].asString())),
		  serverSoftware(psg_pstrdup(pool, config["server_software"].asString())),
		  defaultStickySessionsCookieName(psg_pstrdup(pool, config["sticky_sessions_cookie_name"].asString())),
		  defaultVaryTurbocacheByCookie(psg_pstrdup(pool, config["vary_turbocache_by_cookie"].asString())),

		  friendlyErrorPages(psg_pstrdup(pool, config["friendly_error_pages"].asString())),
		  spawnMethod(psg_pstrdup(pool, config["spawn_method"].asString())),
		  meteorAppSettings(psg_pstrdup(pool, config["meteor_app_settings"].asString())),
		  fileDescriptorUlimit(config["app_file_descriptor_ulimit"].asUInt()),
		  minInstances(config["min_instances"].asUInt()),
		  maxPreloaderIdleTime(config["max_preloader_idle_time"].asUInt()),
		  maxRequestQueueSize(config["max_request_queue_size"].asUInt()),
		  maxRequests(config["max_requests"].asUInt()),
		  forceMaxConcurrentRequestsPerProcess(config["force_max_concurrent_requests_per_process"].asInt()),
		  singleAppMode(!config["multi_app"].asBool()),
		  showVersionInHeader(config["show_version_in_header"].asBool()),
		  abortWebsocketsOnProcessShutdown(config["abort_websockets_on_process_shutdown"].asBool()),
		  loadShellEnvvars(config["load_shell_envvars"].asBool())

		  /*******************/
		{ }

	~ControllerRequestConfig() {
		psg_destroy_pool(pool);
	}
};

typedef boost::intrusive_ptr<ControllerRequestConfig> ControllerRequestConfigPtr;


struct ControllerConfigChangeRequest {
	ServerKit::HttpServerConfigChangeRequest forParent;
	boost::scoped_ptr<ControllerMainConfig> mainConfig;
	ControllerRequestConfigPtr requestConfig;
};


} // namespace Core
} // namespace Passenger

#endif /* _PASSENGER_CORE_CONTROLLER_CONFIG_H_ */
