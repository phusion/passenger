/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
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

#ifndef _PASSENGER_WATCHDOG_CONFIG_H_
#define _PASSENGER_WATCHDOG_CONFIG_H_

#include <ConfigKit/TableTranslator.h>
#include <ConfigKit/PrefixTranslator.h>
#include <Core/Config.h>
#include <Watchdog/ApiServer.h>
#include <Shared/ApiAccountUtils.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {
namespace Watchdog {

using namespace std;


/*
 * BEGIN ConfigKit schema: Passenger::Watchdog::Schema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   admin_panel_authentication                                               object             -          secret
 *   admin_panel_close_timeout                                                float              -          default(10.0)
 *   admin_panel_connect_timeout                                              float              -          default(30.0)
 *   admin_panel_data_debug                                                   boolean            -          default(false)
 *   admin_panel_ping_interval                                                float              -          default(30.0)
 *   admin_panel_ping_timeout                                                 float              -          default(30.0)
 *   admin_panel_proxy_password                                               string             -          secret
 *   admin_panel_proxy_timeout                                                float              -          default(30.0)
 *   admin_panel_proxy_url                                                    string             -          -
 *   admin_panel_proxy_username                                               string             -          -
 *   admin_panel_reconnect_timeout                                            float              -          default(5.0)
 *   admin_panel_url                                                          string             -          read_only
 *   admin_panel_websocketpp_debug_access                                     boolean            -          default(false)
 *   admin_panel_websocketpp_debug_error                                      boolean            -          default(false)
 *   app_output_log_level                                                     string             -          default("notice")
 *   benchmark_mode                                                           string             -          -
 *   controller_accept_burst_count                                            unsigned integer   -          default(32)
 *   controller_addresses                                                     array of strings   -          default,read_only
 *   controller_client_freelist_limit                                         unsigned integer   -          default(0)
 *   controller_cpu_affine                                                    boolean            -          default(false),read_only
 *   controller_file_buffered_channel_auto_start_mover                        boolean            -          default(true)
 *   controller_file_buffered_channel_auto_truncate_file                      boolean            -          default(true)
 *   controller_file_buffered_channel_buffer_dir                              string             -          default
 *   controller_file_buffered_channel_delay_in_file_mode_switching            unsigned integer   -          default(0)
 *   controller_file_buffered_channel_max_disk_chunk_read_size                unsigned integer   -          default(0)
 *   controller_file_buffered_channel_threshold                               unsigned integer   -          default(131072)
 *   controller_mbuf_block_chunk_size                                         unsigned integer   -          default(4096),read_only
 *   controller_min_spare_clients                                             unsigned integer   -          default(0)
 *   controller_pid_file                                                      string             -          default,read_only
 *   controller_request_freelist_limit                                        unsigned integer   -          default(1024)
 *   controller_socket_backlog                                                unsigned integer   -          default(2048),read_only
 *   controller_start_reading_after_accept                                    boolean            -          default(true)
 *   controller_threads                                                       unsigned integer   -          default,read_only
 *   core_api_server_accept_burst_count                                       unsigned integer   -          default(32)
 *   core_api_server_addresses                                                array of strings   -          default([]),read_only
 *   core_api_server_authorizations                                           array              -          default("[FILTERED]"),secret
 *   core_api_server_client_freelist_limit                                    unsigned integer   -          default(0)
 *   core_api_server_file_buffered_channel_auto_start_mover                   boolean            -          default(true)
 *   core_api_server_file_buffered_channel_auto_truncate_file                 boolean            -          default(true)
 *   core_api_server_file_buffered_channel_buffer_dir                         string             -          default
 *   core_api_server_file_buffered_channel_delay_in_file_mode_switching       unsigned integer   -          default(0)
 *   core_api_server_file_buffered_channel_max_disk_chunk_read_size           unsigned integer   -          default(0)
 *   core_api_server_file_buffered_channel_threshold                          unsigned integer   -          default(131072)
 *   core_api_server_mbuf_block_chunk_size                                    unsigned integer   -          default(4096),read_only
 *   core_api_server_min_spare_clients                                        unsigned integer   -          default(0)
 *   core_api_server_request_freelist_limit                                   unsigned integer   -          default(1024)
 *   core_api_server_start_reading_after_accept                               boolean            -          default(true)
 *   core_pid_file                                                            string             -          read_only
 *   daemonize                                                                boolean            -          default(false)
 *   default_abort_websockets_on_process_shutdown                             boolean            -          default(true)
 *   default_app_file_descriptor_ulimit                                       unsigned integer   -          -
 *   default_environment                                                      string             -          default("production")
 *   default_force_max_concurrent_requests_per_process                        integer            -          default(-1)
 *   default_friendly_error_pages                                             string             -          default("auto")
 *   default_group                                                            string             -          default
 *   default_load_shell_envvars                                               boolean            -          default(false)
 *   default_max_preloader_idle_time                                          unsigned integer   -          default(300)
 *   default_max_request_queue_size                                           unsigned integer   -          default(100)
 *   default_max_requests                                                     unsigned integer   -          default(0)
 *   default_meteor_app_settings                                              string             -          -
 *   default_min_instances                                                    unsigned integer   -          default(1)
 *   default_nodejs                                                           string             -          default("node")
 *   default_python                                                           string             -          default("python")
 *   default_ruby                                                             string             -          default("ruby")
 *   default_server_name                                                      string             -          default
 *   default_server_port                                                      unsigned integer   -          default
 *   default_spawn_method                                                     string             -          default("smart")
 *   default_sticky_sessions                                                  boolean            -          default(false)
 *   default_sticky_sessions_cookie_name                                      string             -          default("_passenger_route")
 *   default_user                                                             string             -          default("nobody")
 *   file_descriptor_log_target                                               any                -          -
 *   file_descriptor_ulimit                                                   unsigned integer   -          default(0),read_only
 *   graceful_exit                                                            boolean            -          default(true)
 *   hook_after_watchdog_initialization                                       string             -          -
 *   hook_after_watchdog_shutdown                                             string             -          -
 *   hook_before_watchdog_initialization                                      string             -          -
 *   hook_before_watchdog_shutdown                                            string             -          -
 *   instance_registry_dir                                                    string             -          default,read_only
 *   integration_mode                                                         string             -          default("standalone")
 *   log_level                                                                string             -          default("notice")
 *   log_target                                                               any                -          default({"stderr": true})
 *   max_pool_size                                                            unsigned integer   -          default(6)
 *   multi_app                                                                boolean            -          default(false),read_only
 *   passenger_root                                                           string             required   read_only
 *   pidfiles_to_delete_on_exit                                               array of strings   -          default([])
 *   pool_idle_time                                                           unsigned integer   -          default(300)
 *   pool_selfchecks                                                          boolean            -          default(false)
 *   prestart_urls                                                            array of strings   -          default([]),read_only
 *   response_buffer_high_watermark                                           unsigned integer   -          default(134217728)
 *   security_update_checker_certificate_path                                 string             -          -
 *   security_update_checker_disabled                                         boolean            -          default(false)
 *   security_update_checker_interval                                         unsigned integer   -          default(86400)
 *   security_update_checker_proxy_url                                        string             -          -
 *   security_update_checker_url                                              string             -          default("https://securitycheck.phusionpassenger.com/v1/check.json")
 *   server_software                                                          string             -          default("Phusion_Passenger/5.1.9")
 *   setsid                                                                   boolean            -          default(false)
 *   show_version_in_header                                                   boolean            -          default(true)
 *   single_app_mode_app_root                                                 string             -          default,read_only
 *   single_app_mode_app_type                                                 string             -          read_only
 *   single_app_mode_startup_file                                             string             -          read_only
 *   standalone_engine                                                        string             -          default
 *   startup_report_file                                                      string             -          -
 *   stat_throttle_rate                                                       unsigned integer   -          default(10)
 *   turbocaching                                                             boolean            -          default(true),read_only
 *   user                                                                     string             -          default,read_only
 *   user_switching                                                           boolean            -          default(true)
 *   ust_router_address                                                       string             -          -
 *   ust_router_password                                                      string             -          secret
 *   vary_turbocache_by_cookie                                                string             -          -
 *   watchdog_api_server_accept_burst_count                                   unsigned integer   -          default(32)
 *   watchdog_api_server_addresses                                            array of strings   -          default([]),read_only
 *   watchdog_api_server_authorizations                                       array              -          default("[FILTERED]"),secret
 *   watchdog_api_server_client_freelist_limit                                unsigned integer   -          default(0)
 *   watchdog_api_server_file_buffered_channel_auto_start_mover               boolean            -          default(true)
 *   watchdog_api_server_file_buffered_channel_auto_truncate_file             boolean            -          default(true)
 *   watchdog_api_server_file_buffered_channel_buffer_dir                     string             -          default
 *   watchdog_api_server_file_buffered_channel_delay_in_file_mode_switching   unsigned integer   -          default(0)
 *   watchdog_api_server_file_buffered_channel_max_disk_chunk_read_size       unsigned integer   -          default(0)
 *   watchdog_api_server_file_buffered_channel_threshold                      unsigned integer   -          default(131072)
 *   watchdog_api_server_mbuf_block_chunk_size                                unsigned integer   -          default(4096),read_only
 *   watchdog_api_server_min_spare_clients                                    unsigned integer   -          default(0)
 *   watchdog_api_server_request_freelist_limit                               unsigned integer   -          default(1024)
 *   watchdog_api_server_start_reading_after_accept                           boolean            -          default(true)
 *   watchdog_pid_file                                                        string             -          read_only
 *   watchdog_pid_file_autodelete                                             boolean            -          default(true)
 *   web_server_module_version                                                string             -          read_only
 *   web_server_version                                                       string             -          read_only
 *
 * END
 */
class Schema: public ConfigKit::Schema {
private:
	/**
	 * Scans `schema` for all options that start with `matchPrefix`. For each matching option,
	 * a translation is inserted in the form of `addPrefix + optionName => optionName`.
	 */
	static void addPrefixTranslationsForKeysThatStartWith(const ConfigKit::Schema &schema,
		ConfigKit::TableTranslator &translator,
		const StaticString &matchPrefix, const StaticString &addPrefix)
	{
		const Json::Value doc = schema.inspect();
		Json::Value::const_iterator it, end = doc.end();
		for (it = doc.begin(); it != end; it++) {
			if (startsWith(it.name(), matchPrefix)) {
				translator.add(addPrefix + it.name(), it.name());
			}
		}
	}

	// Prefix config options that come from the given schema
	template<typename SchemaType>
	static void addSubSchemaPrefixTranslations(ConfigKit::TableTranslator &translator,
		const StaticString &prefix)
	{
		vector<string> keys = SchemaType().inspect().getMemberNames();
		vector<string>::const_iterator it, end = keys.end();
		for (it = keys.begin(); it != end; it++) {
			translator.add(prefix + *it, *it);
		}
	}

	// Some options set their default value to this function to indicate
	// that their actual default values cannot be inferred from a ConfigKit default
	// value getter function. Instead they are determined inside WatchdogMain.cpp.
	static Json::Value dummyDefaultValueGetter(const ConfigKit::Store &store) {
		return Json::Value();
	}

	static Json::Value getDefaultUser(const ConfigKit::Store &store) {
		if (store["user_switching"].asBool()) {
			return Json::nullValue;
		} else {
			return store["default_user"];
		}
	}

	static Json::Value getDefaultInstanceRegistryDir(const ConfigKit::Store &store) {
		return getSystemTempDir();
	}

	static void validateAddresses(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
		typedef ConfigKit::Error Error;

		if (config["watchdog_api_server_addresses"].size() > SERVER_KIT_MAX_SERVER_ENDPOINTS) {
			errors.push_back(Error("'{{watchdog_api_server_addresses}}' may contain at most "
				+ toString(SERVER_KIT_MAX_SERVER_ENDPOINTS) + " items"));
		}
	}

	static Json::Value normalizePaths(const Json::Value &effectiveValues) {
		Json::Value updates;
		updates["instance_registry_dir"] = absolutizePath(effectiveValues["instance_registry_dir"].asString());
		if (!effectiveValues["watchdog_pid_file"].isNull()) {
			updates["watchdog_pid_file"] = absolutizePath(effectiveValues["watchdog_pid_file"].asString());
		}
		return updates;
	}

public:
	struct {
		Core::Schema schema;
		ConfigKit::TableTranslator translator;
	} core;
	struct {
		ApiServer::Schema schema;
		ConfigKit::TableTranslator translator;
	} apiServer;
	struct {
		ServerKit::Schema schema;
		ConfigKit::PrefixTranslator translator;
	} apiServerKit;

	Schema() {
		using namespace ConfigKit;

		// Add subschema: core
		addPrefixTranslationsForKeysThatStartWith(core.schema, core.translator,
			"api_server_", "core_");
		core.translator.add("core_authorizations", "authorizations");
		core.translator.add("core_password", "password");
		core.translator.add("core_pid_file", "pid_file");
		core.translator.finalize();
		addSubSchema(core.schema, core.translator);
		erase("controller_secure_headers_password");
		erase("instance_dir");
		erase("watchdog_fd_passing_password");
		/***********/
		/***********/

		// Add subschema: apiServer
		addSubSchemaPrefixTranslations<ServerKit::HttpServerSchema>(
			apiServer.translator, "watchdog_api_server_");
		apiServer.translator.add("watchdog_api_server_authorizations", "authorizations");
		apiServer.translator.finalize();
		addSubSchema(apiServer.schema, apiServer.translator);
		erase("fd_passing_password");

		// Add subschema: apiServerKit
		apiServerKit.translator.setPrefixAndFinalize("watchdog_api_server_");
		addSubSchema(apiServerKit.schema, apiServerKit.translator);
		erase("watchdog_api_server_secure_mode_password");


		overrideWithDynamicDefault("controller_addresses", STRING_ARRAY_TYPE,
			OPTIONAL | READ_ONLY, dummyDefaultValueGetter);
		overrideWithDynamicDefault("controller_pid_file", STRING_TYPE,
			OPTIONAL | READ_ONLY, dummyDefaultValueGetter);

		add("watchdog_pid_file", STRING_TYPE, OPTIONAL | READ_ONLY);
		add("watchdog_pid_file_autodelete", BOOL_TYPE, OPTIONAL, true);
		add("watchdog_api_server_addresses", STRING_ARRAY_TYPE, OPTIONAL | READ_ONLY, Json::arrayValue);
		add("setsid", BOOL_TYPE, OPTIONAL, false);
		add("daemonize", BOOL_TYPE, OPTIONAL, false);
		add("startup_report_file", STRING_TYPE, OPTIONAL);
		add("pidfiles_to_delete_on_exit", STRING_ARRAY_TYPE, OPTIONAL, Json::arrayValue);
		addWithDynamicDefault("user", STRING_TYPE,
			OPTIONAL | READ_ONLY | CACHE_DEFAULT_VALUE, getDefaultUser);
		addWithDynamicDefault("instance_registry_dir", STRING_TYPE,
			OPTIONAL | READ_ONLY | CACHE_DEFAULT_VALUE, getDefaultInstanceRegistryDir);

		// TODO: do something with secure mode password?

		add("hook_before_watchdog_initialization", STRING_TYPE, OPTIONAL);
		add("hook_after_watchdog_initialization", STRING_TYPE, OPTIONAL);
		add("hook_before_watchdog_shutdown", STRING_TYPE, OPTIONAL);
		add("hook_after_watchdog_shutdown", STRING_TYPE, OPTIONAL);

		/***********/
		/***********/

		addValidator(validateAddresses);

		addNormalizer(normalizePaths);

		/***********/
		/***********/

		finalize();
	}
};

// pid_file -> watchdog_pid_file
// delete_pid_file -> watchdog_pid_file_autodelete
// setsid -> same
// daemonize -> same
// report_file -> same
// instance_registry_dir -> same
// cleanup_pidfiles -> pidfiles_to_delete_on_exit
// user -> user
// watchdog_authorizations -> watchdog_api_server_authorizations
// watchdog_api_addresses -> watchdog_api_server_addresses
// hook_before_watchdog_initialization -> same
// hook_after_watchdog_initialization -> same
// hook_before_watchdog_shutdown -> same
// hook_after_watchdog_shutdown -> same
// original_oom_score (internal)
// instance_dir (internal)
// watchdog_fd_passing_password (internal)
// default_user -> same (from core)
// default_group -> same (from core)
// passenger_root -> same (from core)
// server_software -> same (from core)
// user_switching -> same (from core)
// integration_mode -> same (from core)
// standalone_engine -> same (from core)
// core_pid_file -> same (from core)
// core_addresses -> controller_addresses (from core)
// core_password -> controller_secure_headers_password (from core)
// core_api_addresses -> core_api_server_addresses (from core)
// core_authorizations -> core_api_server_authorizations (from core)
// data_buffer_dir -> DELETED

inline Json::Value
prepareWatchdogConfigFromAgentsOptions(const VariantMap &options) {
	#define SET_STR_CONFIG2(configName, optionName) \
		do { \
			if (options.has(optionName)) { \
				config[configName] = options.get(optionName); \
			} \
		} while (false)
	#define SET_INT_CONFIG2(configName, optionName) \
		do { \
			if (options.has(optionName)) { \
				config[configName] = options.getInt(optionName); \
			} \
		} while (false)
	#define SET_UINT_CONFIG2(configName, optionName) \
		do { \
			if (options.has(optionName)) { \
				config[configName] = options.getUint(optionName); \
			} \
		} while (false)
	#define SET_DOUBLE_CONFIG2(configName, optionName) \
		do { \
			if (options.has(optionName)) { \
				config[configName] = options.getDouble(optionName); \
			} \
		} while (false)
	#define SET_BOOL_CONFIG2(configName, optionName) \
		do { \
			if (options.has(optionName)) { \
				config[configName] = options.getBool(optionName); \
			} \
		} while (false)
	#define SET_JSON_OBJECT_CONFIG2(configName, optionName) \
		do { \
			if (options.has(optionName)) { \
				config[configName] = options.getJsonObject(optionName); \
			} \
		} while (false)
	#define SET_STR_CONFIG(name) SET_STR_CONFIG2(name, name)
	#define SET_INT_CONFIG(name) SET_INT_CONFIG2(name, name)
	#define SET_UINT_CONFIG(name) SET_UINT_CONFIG2(name, name)
	#define SET_DOUBLE_CONFIG(name) SET_DOUBLE_CONFIG2(name, name)
	#define SET_BOOL_CONFIG(name) SET_BOOL_CONFIG2(name, name)
	#define SET_JSON_OBJECT_CONFIG(name) SET_JSON_OBJECT_CONFIG2(name, name)

	Json::Value config;

	SET_STR_CONFIG("passenger_root");
	SET_STR_CONFIG("integration_mode");
	SET_STR_CONFIG("watchdog_pid_file");
	SET_INT_CONFIG("log_level");
	SET_STR_CONFIG2("log_target", "log_file");
	SET_STR_CONFIG2("file_descriptor_log_target", "file_descriptor_log_file");
	SET_INT_CONFIG("max_pool_size");
	SET_UINT_CONFIG("pool_idle_time");
	SET_BOOL_CONFIG2("pool_selfchecks", "selfchecks");
	SET_UINT_CONFIG2("controller_threads", "core_threads");
	SET_UINT_CONFIG2("controller_socket_backlog", "socket_backlog");
	SET_BOOL_CONFIG2("controller_cpu_affine", "core_cpu_affine");
	SET_STR_CONFIG("web_server_module_version");
	SET_STR_CONFIG2("web_server_version", "server_version");

	SET_STR_CONFIG2("watchdog_pid_file", "pid_file");
	SET_STR_CONFIG2("watchdog_api_server_authorizations", "watchdog_authorizations");
	SET_STR_CONFIG2("watchdog_api_server_addresses", "watchdog_api_addresses");
	SET_STR_CONFIG("instance_registry_dir");
	SET_STR_CONFIG("user");
	SET_STR_CONFIG("hook_before_watchdog_initialization");
	SET_STR_CONFIG("hook_after_watchdog_initialization");
	SET_STR_CONFIG("hook_before_watchdog_shutdown");
	SET_STR_CONFIG("hook_after_watchdog_shutdown");
	SET_BOOL_CONFIG2("watchdog_pid_file_autodelete", "delete_pid_file");
	SET_BOOL_CONFIG2("startup_report_file", "report_file");
	SET_BOOL_CONFIG("setsid");
	SET_BOOL_CONFIG("daemonize");

	SET_BOOL_CONFIG2("default_abort_websockets_on_process_shutdown", "abort_websockets_on_process_shutdown");
	SET_UINT_CONFIG2("default_app_file_descriptor_ulimit", "app_file_descriptor_ulimit");
	SET_STR_CONFIG("benchmark_mode");
	SET_STR_CONFIG("default_group");
	SET_STR_CONFIG("default_nodejs");
	SET_STR_CONFIG("default_python");
	SET_STR_CONFIG("default_ruby");
	SET_STR_CONFIG("default_server_name");
	SET_UINT_CONFIG("default_server_port");
	SET_STR_CONFIG("default_user");
	SET_STR_CONFIG2("default_environment", "environment");
	SET_INT_CONFIG2("default_force_max_concurrent_requests_per_process", "force_max_concurrent_requests_per_process");
	SET_BOOL_CONFIG2("default_friendly_error_pages", "friendly_error_pages");
	SET_BOOL_CONFIG("graceful_exit");
	SET_BOOL_CONFIG2("default_load_shell_envvars", "load_shell_envvars");
	SET_UINT_CONFIG2("default_max_preloader_idle_time", "max_preloader_idle_time");
	SET_UINT_CONFIG2("default_max_request_queue_size", "max_request_queue_size");
	SET_UINT_CONFIG2("default_max_requests", "max_requests");
	SET_STR_CONFIG2("default_meteor_app_settings", "meteor_app_settings");
	SET_UINT_CONFIG2("default_min_instances", "min_instances");
	SET_BOOL_CONFIG("multi_app");
	SET_UINT_CONFIG("response_buffer_high_watermark");
	SET_STR_CONFIG("server_software");
	SET_BOOL_CONFIG("show_version_in_header");
	SET_STR_CONFIG2("default_spawn_method", "spawn_method");
	SET_STR_CONFIG2("single_app_mode_app_root", "app_root");
	SET_STR_CONFIG2("single_app_mode_app_type", "app_type");
	SET_STR_CONFIG2("single_app_mode_startup_file", "startup_file");
	SET_UINT_CONFIG("stat_throttle_rate");
	SET_BOOL_CONFIG2("default_sticky_sessions", "sticky_sessions");
	SET_STR_CONFIG2("default_sticky_sessions_cookie_name", "sticky_sessions_cookie_name");
	SET_BOOL_CONFIG("turbocaching");
	SET_BOOL_CONFIG("user_switching");
	SET_STR_CONFIG("ust_router_address");
	SET_STR_CONFIG("ust_router_password");
	SET_STR_CONFIG("vary_turbocache_by_cookie");

	SET_UINT_CONFIG2("file_descriptor_ulimit", "core_file_descriptor_ulimit");
	SET_BOOL_CONFIG2("security_update_checker_disabled", "disable_security_update_check");
	SET_STR_CONFIG2("security_update_checker_proxy_url", "security_update_check_proxy");
	SET_JSON_OBJECT_CONFIG("admin_panel_authentication");
	SET_DOUBLE_CONFIG("admin_panel_close_timeout");
	SET_DOUBLE_CONFIG("admin_panel_connect_timeout");
	SET_BOOL_CONFIG("admin_panel_data_debug");
	SET_BOOL_CONFIG("admin_panel_websocketpp_debug_access");
	SET_BOOL_CONFIG("admin_panel_websocketpp_debug_error");
	SET_DOUBLE_CONFIG("admin_panel_ping_interval");
	SET_DOUBLE_CONFIG("admin_panel_ping_timeout");
	SET_STR_CONFIG("admin_panel_proxy_password");
	SET_DOUBLE_CONFIG("admin_panel_proxy_timeout");
	SET_STR_CONFIG("admin_panel_proxy_url");
	SET_STR_CONFIG("admin_panel_proxy_username");
	SET_STR_CONFIG("admin_panel_reconnect_timeout");
	SET_STR_CONFIG("admin_panel_url");

	if (!config.isMember("integration_mode") || config["integration_mode"].asString() == "standalone") {
		config["standalone_engine"] = options.get("standalone_engine", false, "builtin");
	}

	if (options.has("core_password")) {
		if (options.get("core_password") != "-") {
			config["controller_secure_headers_password"] = options.get("core_password");
		}
	} else if (options.has("core_password_file")) {
		config["controller_secure_headers_password"]["path"] = absolutizePath(
			options.get("core_password_file"));
	}

	if (options.has("core_addresses")) {
		Json::Value addresses(Json::arrayValue);
		vector<string> addresses2 = options.getStrSet("core_addresses");
		foreach (string address, addresses2) {
			addresses.append(address);
		}
		config["controller_addresses"] = addresses;
	}

	if (options.has("core_api_addresses")) {
		Json::Value addresses(Json::arrayValue);
		vector<string> addresses2 = options.getStrSet("core_api_addresses");
		foreach (string address, addresses2) {
			addresses.append(address);
		}
		config["core_api_server_addresses"] = addresses;
	}

	if (options.has("core_authorizations")) {
		vector<string> authorizations = options.getStrSet("core_authorizations");
		foreach (string description, authorizations) {
			config["core_api_server_authorizations"].append(ApiAccountUtils::parseApiAccountDescription(
				description));
		}
	}

	if (options.has("prestart_urls")) {
		Json::Value subdoc(Json::arrayValue);
		foreach (string url, options.getStrSet("prestart_urls")) {
			subdoc.append(url);
		}
		config["prestart_urls"] = subdoc;
	}

	if (options.has("cleanup_pidfiles")) {
		Json::Value paths(Json::arrayValue);
		vector<string> paths2 = options.getStrSet("cleanup_pidfiles");
		foreach (string path, paths2) {
			paths.append(path);
		}
		config["pidfiles_to_delete_on_exit"] = paths;
	}

	if (options.has("accept_burst_count")) {
		config["controller_accept_burst_count"] = options.getUint("accept_burst_count");
		config["core_api_server_accept_burst_count"] = options.getUint("accept_burst_count");
		config["watchdog_api_server_accept_burst_count"] = options.getUint("accept_burst_count");
	}
	if (options.has("client_freelist_limit")) {
		config["controller_client_freelist_limit"] = options.getUint("client_freelist_limit");
		config["core_api_server_client_freelist_limit"] = options.getUint("client_freelist_limit");
		config["watchdog_api_server_client_freelist_limit"] = options.getUint("client_freelist_limit");
	}
	if (options.has("data_buffer_dir")) {
		config["controller_file_buffered_channel_buffer_dir"] = options.get("data_buffer_dir");
		config["core_api_server_file_buffered_channel_buffer_dir"] = options.get("data_buffer_dir");
		config["watchdog_api_server_file_buffered_channel_buffer_dir"] = options.get("data_buffer_dir");
	}
	if (options.has("file_buffer_threshold")) {
		config["controller_file_buffered_channel_threshold"] = options.get("file_buffer_threshold");
		config["core_api_server_file_buffered_channel_threshold"] = options.get("file_buffer_threshold");
		config["watchdog_server_file_buffered_channel_threshold"] = options.get("file_buffer_threshold");
	}
	if (options.has("min_spare_clients")) {
		config["controller_min_spare_clients"] = options.getUint("min_spare_clients");
		config["core_api_server_min_spare_clients"] = options.getUint("min_spare_clients");
		config["watchdog_server_min_spare_clients"] = options.getUint("min_spare_clients");
	}
	if (options.has("request_freelist_limit")) {
		config["controller_request_freelist_limit"] = options.getUint("request_freelist_limit");
		config["core_api_server_request_freelist_limit"] = options.getUint("request_freelist_limit");
		config["watchdog_server_request_freelist_limit"] = options.getUint("request_freelist_limit");
	}
	if (options.has("start_reading_after_accept")) {
		config["controller_start_reading_after_accept"] = options.getBool("start_reading_after_accept");
		config["core_api_server_start_reading_after_accept"] = options.getBool("start_reading_after_accept");
		config["watchdog_server_start_reading_after_accept"] = options.getBool("start_reading_after_accept");
	}

	/*****************/
	/*****************/

	P_DEBUG("Watchdog config JSON: " << config.toStyledString());
	return config;

	#undef SET_STR_CONFIG2
	#undef SET_INT_CONFIG2
	#undef SET_UINT_CONFIG2
	#undef SET_BOOL_CONFIG2
	#undef SET_STR_CONFIG
	#undef SET_INT_CONFIG
	#undef SET_UINT_CONFIG
	#undef SET_BOOL_CONFIG
}

inline void
createWatchdogConfigFromAgentsOptions(const VariantMap &options, const Json::Value &config,
	ConfigKit::Store **store, Schema **schema)
{
	*schema = new Schema();
	*store = new ConfigKit::Store(**schema, config);
}


} // namespace Watchdog
} // namespace Passenger

#endif /* _PASSENGER_WATCHDOG_CONFIG_H_ */
