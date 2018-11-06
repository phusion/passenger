/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017-2018 Phusion Holding B.V.
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

#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <cerrno>

#include <ConfigKit/ConfigKit.h>
#include <ConfigKit/SchemaUtils.h>
#include <MemoryKit/palloc.h>
#include <ServerKit/HttpServer.h>
#include <SystemTools/UserDatabase.h>
#include <WrapperRegistry/Registry.h>
#include <Constants.h>
#include <Exceptions.h>
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

/*
 * BEGIN ConfigKit schema: Passenger::Core::ControllerSchema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   accept_burst_count                                  unsigned integer   -          default(32)
 *   benchmark_mode                                      string             -          -
 *   client_freelist_limit                               unsigned integer   -          default(0)
 *   default_abort_websockets_on_process_shutdown        boolean            -          default(true)
 *   default_app_file_descriptor_ulimit                  unsigned integer   -          -
 *   default_environment                                 string             -          default("production")
 *   default_force_max_concurrent_requests_per_process   integer            -          default(-1)
 *   default_friendly_error_pages                        string             -          default("auto")
 *   default_group                                       string             -          default
 *   default_load_shell_envvars                          boolean            -          default(false)
 *   default_max_preloader_idle_time                     unsigned integer   -          default(300)
 *   default_max_request_queue_size                      unsigned integer   -          default(100)
 *   default_max_requests                                unsigned integer   -          default(0)
 *   default_meteor_app_settings                         string             -          -
 *   default_min_instances                               unsigned integer   -          default(1)
 *   default_nodejs                                      string             -          default("node")
 *   default_python                                      string             -          default("python")
 *   default_ruby                                        string             -          default("ruby")
 *   default_server_name                                 string             required   -
 *   default_server_port                                 unsigned integer   required   -
 *   default_spawn_method                                string             -          default("smart")
 *   default_sticky_sessions                             boolean            -          default(false)
 *   default_sticky_sessions_cookie_name                 string             -          default("_passenger_route")
 *   default_user                                        string             -          default("nobody")
 *   graceful_exit                                       boolean            -          default(true)
 *   integration_mode                                    string             -          default("standalone"),read_only
 *   max_instances_per_app                               unsigned integer   -          read_only
 *   min_spare_clients                                   unsigned integer   -          default(0)
 *   multi_app                                           boolean            -          default(true),read_only
 *   request_freelist_limit                              unsigned integer   -          default(1024)
 *   response_buffer_high_watermark                      unsigned integer   -          default(134217728)
 *   server_software                                     string             -          default("Phusion_Passenger/5.3.7")
 *   show_version_in_header                              boolean            -          default(true)
 *   start_reading_after_accept                          boolean            -          default(true)
 *   stat_throttle_rate                                  unsigned integer   -          default(10)
 *   thread_number                                       unsigned integer   required   read_only
 *   turbocaching                                        boolean            -          default(true),read_only
 *   user_switching                                      boolean            -          default(true)
 *   vary_turbocache_by_cookie                           string             -          -
 *
 * END
 */
class ControllerSchema: public ServerKit::HttpServerSchema {
private:
	void initialize() {
		using namespace ConfigKit;

		add("max_instances_per_app", UINT_TYPE, OPTIONAL | READ_ONLY);

		add("thread_number", UINT_TYPE, REQUIRED | READ_ONLY);
		add("multi_app", BOOL_TYPE, OPTIONAL | READ_ONLY, true);
		add("turbocaching", BOOL_TYPE, OPTIONAL | READ_ONLY, true);
		add("integration_mode", STRING_TYPE, OPTIONAL | READ_ONLY, DEFAULT_INTEGRATION_MODE);

		add("user_switching", BOOL_TYPE, OPTIONAL, true);
		add("stat_throttle_rate", UINT_TYPE, OPTIONAL, DEFAULT_STAT_THROTTLE_RATE);
		add("show_version_in_header", BOOL_TYPE, OPTIONAL, true);
		add("response_buffer_high_watermark", UINT_TYPE, OPTIONAL, DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK);
		add("graceful_exit", BOOL_TYPE, OPTIONAL, true);
		add("benchmark_mode", STRING_TYPE, OPTIONAL);

		add("default_ruby", STRING_TYPE, OPTIONAL, DEFAULT_RUBY);
		add("default_python", STRING_TYPE, OPTIONAL, DEFAULT_PYTHON);
		add("default_nodejs", STRING_TYPE, OPTIONAL, DEFAULT_NODEJS);
		add("default_user", STRING_TYPE, OPTIONAL, DEFAULT_WEB_APP_USER);
		addWithDynamicDefault(
			"default_group", STRING_TYPE, OPTIONAL | CACHE_DEFAULT_VALUE,
			inferDefaultValueForDefaultGroup);
		add("default_server_name", STRING_TYPE, REQUIRED);
		add("default_server_port", UINT_TYPE, REQUIRED);
		add("default_sticky_sessions", BOOL_TYPE, OPTIONAL, false);
		add("default_sticky_sessions_cookie_name", STRING_TYPE, OPTIONAL, DEFAULT_STICKY_SESSIONS_COOKIE_NAME);
		add("server_software", STRING_TYPE, OPTIONAL, SERVER_TOKEN_NAME "/" PASSENGER_VERSION);
		add("vary_turbocache_by_cookie", STRING_TYPE, OPTIONAL);

		add("default_friendly_error_pages", STRING_TYPE, OPTIONAL, "auto");
		add("default_environment", STRING_TYPE, OPTIONAL, DEFAULT_APP_ENV);
		add("default_spawn_method", STRING_TYPE, OPTIONAL, DEFAULT_SPAWN_METHOD);
		add("default_load_shell_envvars", BOOL_TYPE, OPTIONAL, false);
		add("default_meteor_app_settings", STRING_TYPE, OPTIONAL);
		add("default_app_file_descriptor_ulimit", UINT_TYPE, OPTIONAL);
		add("default_min_instances", UINT_TYPE, OPTIONAL, 1);
		add("default_max_preloader_idle_time", UINT_TYPE, OPTIONAL, DEFAULT_MAX_PRELOADER_IDLE_TIME);
		add("default_max_request_queue_size", UINT_TYPE, OPTIONAL, DEFAULT_MAX_REQUEST_QUEUE_SIZE);
		add("default_force_max_concurrent_requests_per_process", INT_TYPE, OPTIONAL, -1);
		add("default_abort_websockets_on_process_shutdown", BOOL_TYPE, OPTIONAL, true);
		add("default_max_requests", UINT_TYPE, OPTIONAL, 0);


		/*******************/
		/*******************/


		addValidator(validate);
		addValidator(ConfigKit::validateIntegrationMode);
	}

	static Json::Value inferDefaultValueForDefaultGroup(const ConfigKit::Store &config) {
		OsUser osUser;
		if (!lookupSystemUserByName(config["default_user"].asString(), osUser)) {
			throw ConfigurationException(
				"The user that PassengerDefaultUser refers to, '" +
				config["default_user"].asString() + "', does not exist.");
		}
		return lookupSystemGroupnameByGid(osUser.pwd.pw_gid, P_STATIC_STRING("%d"));
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

/*
 * BEGIN ConfigKit schema: Passenger::Core::ControllerSingleAppModeSchema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   app_root       string   -          default,read_only
 *   app_type       string   required   read_only
 *   startup_file   string   required   read_only
 *
 * END
 */
struct ControllerSingleAppModeSchema: public ConfigKit::Schema {
	ControllerSingleAppModeSchema(const WrapperRegistry::Registry *wrapperRegistry = NULL) {
		using namespace ConfigKit;

		addWithDynamicDefault("app_root", STRING_TYPE, OPTIONAL | READ_ONLY | CACHE_DEFAULT_VALUE,
			getDefaultAppRoot);
		add("app_type", STRING_TYPE, REQUIRED | READ_ONLY);
		add("startup_file", STRING_TYPE, REQUIRED | READ_ONLY);

		addValidator(boost::bind(validateAppType, "app_type", wrapperRegistry,
			boost::placeholders::_1, boost::placeholders::_2));
		addNormalizer(normalizeAppRoot);
		addNormalizer(normalizeStartupFile);

		finalize();
	}

	static Json::Value getDefaultAppRoot(const ConfigKit::Store &config) {
		char buf[MAXPATHLEN];
		const char *path = getcwd(buf, sizeof(buf));
		if (path == NULL) {
			int e = errno;
			throw SystemException("Unable to obtain current working directory", e);
		}
		string result = path;
		return result;
	}

	static void validateAppType(const string &appTypeKey,
		const WrapperRegistry::Registry *wrapperRegistry,
		const ConfigKit::Store &config, vector<ConfigKit::Error> &errors)
	{
		typedef ConfigKit::Error Error;

		if (!config[appTypeKey].isNull() && wrapperRegistry != NULL) {
			const WrapperRegistry::Entry &entry =
				wrapperRegistry->lookup(config[appTypeKey].asString());
			if (entry.isNull()) {
				string message = "'{{" + appTypeKey + "}}' is set to '"
					+ config[appTypeKey].asString() + "', which is not a"
					" valid application type. Supported app types are:";
				WrapperRegistry::Registry::ConstIterator it(
					wrapperRegistry->getIterator());
				while (*it != NULL) {
					message.append(1, ' ');
					message.append(it.getValue().language);
					it.next();
				}
				errors.push_back(Error(message));
			}
		}
	}

	static Json::Value normalizeAppRoot(const Json::Value &effectiveValues) {
		Json::Value updates;
		updates["app_root"] = absolutizePath(effectiveValues["app_root"].asString());
		return updates;
	}

	static Json::Value normalizeStartupFile(const Json::Value &effectiveValues) {
		Json::Value updates;
		updates["startup_file"] = absolutizePath(effectiveValues["startup_file"].asString());
		return updates;
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
	unsigned int maxInstancesPerApp;
	ControllerBenchmarkMode benchmarkMode: 3;
	bool singleAppMode: 1;
	bool userSwitching: 1;
	bool defaultStickySessions: 1;
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
		  maxInstancesPerApp(config["max_instances_per_app"].asUInt()),
		  benchmarkMode(parseControllerBenchmarkMode(config["benchmark_mode"].asString())),
		  singleAppMode(!config["multi_app"].asBool()),
		  userSwitching(config["user_switching"].asBool()),
		  defaultStickySessions(config["default_sticky_sessions"].asBool()),
		  gracefulExit(config["graceful_exit"].asBool())

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
		SWAP_BITFIELD(bool, singleAppMode);
		SWAP_BITFIELD(bool, userSwitching);
		SWAP_BITFIELD(bool, defaultStickySessions);
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
	StaticString defaultUser;
	StaticString defaultGroup;
	StaticString defaultServerName;
	StaticString defaultServerPort;
	StaticString serverSoftware;
	StaticString defaultStickySessionsCookieName;
	StaticString defaultVaryTurbocacheByCookie;

	StaticString defaultFriendlyErrorPages;
	StaticString defaultEnvironment;
	StaticString defaultSpawnMethod;
	StaticString defaultMeteorAppSettings;
	unsigned int defaultAppFileDescriptorUlimit;
	unsigned int defaultMinInstances;
	unsigned int defaultMaxPreloaderIdleTime;
	unsigned int defaultMaxRequestQueueSize;
	unsigned int defaultMaxRequests;
	int defaultForceMaxConcurrentRequestsPerProcess;
	bool showVersionInHeader: 1;
	bool defaultAbortWebsocketsOnProcessShutdown;
	bool defaultLoadShellEnvvars;

	/*******************/
	/*******************/


	ControllerRequestConfig(const ConfigKit::Store &config)
		: pool(psg_create_pool(1024 * 4)),

		  defaultRuby(psg_pstrdup(pool, config["default_ruby"].asString())),
		  defaultPython(psg_pstrdup(pool, config["default_python"].asString())),
		  defaultNodejs(psg_pstrdup(pool, config["default_nodejs"].asString())),
		  defaultUser(psg_pstrdup(pool, config["default_user"].asString())),
		  defaultGroup(psg_pstrdup(pool, config["default_group"].asString())),
		  defaultServerName(psg_pstrdup(pool, config["default_server_name"].asString())),
		  defaultServerPort(psg_pstrdup(pool, config["default_server_port"].asString())),
		  serverSoftware(psg_pstrdup(pool, config["server_software"].asString())),
		  defaultStickySessionsCookieName(psg_pstrdup(pool, config["default_sticky_sessions_cookie_name"].asString())),
		  defaultVaryTurbocacheByCookie(psg_pstrdup(pool, config["vary_turbocache_by_cookie"].asString())),

		  defaultFriendlyErrorPages(psg_pstrdup(pool, config["default_friendly_error_pages"].asString())),
		  defaultEnvironment(psg_pstrdup(pool, config["default_environment"].asString())),
		  defaultSpawnMethod(psg_pstrdup(pool, config["default_spawn_method"].asString())),
		  defaultMeteorAppSettings(psg_pstrdup(pool, config["default_meteor_app_settings"].asString())),
		  defaultAppFileDescriptorUlimit(config["default_app_file_descriptor_ulimit"].asUInt()),
		  defaultMinInstances(config["default_min_instances"].asUInt()),
		  defaultMaxPreloaderIdleTime(config["default_max_preloader_idle_time"].asUInt()),
		  defaultMaxRequestQueueSize(config["default_max_request_queue_size"].asUInt()),
		  defaultMaxRequests(config["default_max_requests"].asUInt()),
		  defaultForceMaxConcurrentRequestsPerProcess(config["default_force_max_concurrent_requests_per_process"].asInt()),
		  showVersionInHeader(config["show_version_in_header"].asBool()),
		  defaultAbortWebsocketsOnProcessShutdown(config["default_abort_websockets_on_process_shutdown"].asBool()),
		  defaultLoadShellEnvvars(config["default_load_shell_envvars"].asBool())

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
