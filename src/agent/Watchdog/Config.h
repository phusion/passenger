/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace Watchdog {

using namespace std;


/*
 * BEGIN ConfigKit schema: Passenger::Watchdog::Schema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   admin_panel_auth_type                                                    string             -          default("basic")
 *   admin_panel_close_timeout                                                float              -          default(10.0)
 *   admin_panel_connect_timeout                                              float              -          default(30.0)
 *   admin_panel_data_debug                                                   boolean            -          default(false)
 *   admin_panel_password                                                     string             -          secret
 *   admin_panel_password_file                                                string             -          -
 *   admin_panel_ping_interval                                                float              -          default(30.0)
 *   admin_panel_ping_timeout                                                 float              -          default(30.0)
 *   admin_panel_proxy_password                                               string             -          secret
 *   admin_panel_proxy_timeout                                                float              -          default(30.0)
 *   admin_panel_proxy_url                                                    string             -          -
 *   admin_panel_proxy_username                                               string             -          -
 *   admin_panel_reconnect_timeout                                            float              -          default(5.0)
 *   admin_panel_url                                                          string             -          read_only
 *   admin_panel_username                                                     string             -          -
 *   admin_panel_websocketpp_debug_access                                     boolean            -          default(false)
 *   admin_panel_websocketpp_debug_error                                      boolean            -          default(false)
 *   app_output_log_level                                                     string             -          default("notice")
 *   benchmark_mode                                                           string             -          -
 *   config_manifest                                                          object             -          read_only
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
 *   controller_secure_headers_password                                       string             -          default,secret
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
 *   core_file_descriptor_ulimit                                              unsigned integer   -          default(0),read_only
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
 *   graceful_exit                                                            boolean            -          default(true)
 *   hook_after_watchdog_initialization                                       string             -          -
 *   hook_after_watchdog_shutdown                                             string             -          -
 *   hook_before_watchdog_initialization                                      string             -          -
 *   hook_before_watchdog_shutdown                                            string             -          -
 *   instance_registry_dir                                                    string             -          default,read_only
 *   integration_mode                                                         string             -          default("standalone")
 *   log_level                                                                string             -          default("notice")
 *   log_target                                                               any                -          default({"stderr": true})
 *   max_instances_per_app                                                    unsigned integer   -          read_only
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
 *   server_software                                                          string             -          default("Phusion_Passenger/5.3.7")
 *   setsid                                                                   boolean            -          default(false)
 *   show_version_in_header                                                   boolean            -          default(true)
 *   single_app_mode_app_root                                                 string             -          default,read_only
 *   single_app_mode_app_type                                                 string             -          read_only
 *   single_app_mode_startup_file                                             string             -          read_only
 *   standalone_engine                                                        string             -          default
 *   startup_report_file                                                      string             -          -
 *   stat_throttle_rate                                                       unsigned integer   -          default(10)
 *   telemetry_collector_ca_certificate_path                                  string             -          -
 *   telemetry_collector_debug_curl                                           boolean            -          default(false)
 *   telemetry_collector_disabled                                             boolean            -          default(false)
 *   telemetry_collector_final_run_timeout                                    unsigned integer   -          default(5)
 *   telemetry_collector_first_interval                                       unsigned integer   -          default(7200)
 *   telemetry_collector_interval                                             unsigned integer   -          default(21600)
 *   telemetry_collector_interval_jitter                                      unsigned integer   -          default(7200)
 *   telemetry_collector_proxy_url                                            string             -          -
 *   telemetry_collector_timeout                                              unsigned integer   -          default(180)
 *   telemetry_collector_url                                                  string             -          default("https://anontelemetry.phusionpassenger.com/v1/collect.json")
 *   telemetry_collector_verify_server                                        boolean            -          default(true)
 *   turbocaching                                                             boolean            -          default(true),read_only
 *   user                                                                     string             -          default,read_only
 *   user_switching                                                           boolean            -          default(true)
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

	static Json::Value getDefaultControllerSecureHeadersPassword(const ConfigKit::Store &store) {
		return RandomGenerator().generateAsciiString(24);
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
	struct CoreSubschemaContainer {
		Core::Schema schema;
		ConfigKit::TableTranslator translator;

		CoreSubschemaContainer(const WrapperRegistry::Registry *wrapperRegistry)
			: schema(wrapperRegistry)
			{ }
	} core;
	struct {
		ApiServer::Schema schema;
		ConfigKit::TableTranslator translator;
	} apiServer;
	struct {
		ServerKit::Schema schema;
		ConfigKit::PrefixTranslator translator;
	} apiServerKit;

	Schema(const WrapperRegistry::Registry *wrapperRegistry = NULL)
		: core(wrapperRegistry)
	{
		using namespace ConfigKit;

		// Add subschema: core
		addPrefixTranslationsForKeysThatStartWith(core.schema, core.translator,
			"api_server_", "core_");
		core.translator.add("core_authorizations", "authorizations");
		core.translator.add("core_password", "password");
		core.translator.add("core_pid_file", "pid_file");
		core.translator.add("core_file_descriptor_ulimit", "file_descriptor_ulimit");
		core.translator.finalize();
		addSubSchema(core.schema, core.translator);
		erase("instance_dir");
		erase("oom_score");
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

		overrideWithDynamicDefault("controller_secure_headers_password", STRING_TYPE,
			OPTIONAL | SECRET | CACHE_DEFAULT_VALUE, getDefaultControllerSecureHeadersPassword);
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


} // namespace Watchdog
} // namespace Passenger

#endif /* _PASSENGER_WATCHDOG_CONFIG_H_ */
