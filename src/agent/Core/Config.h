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

#ifndef _PASSENGER_CORE_CONFIG_H_
#define _PASSENGER_CORE_CONFIG_H_

#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <LoggingKit/LoggingKit.h>
#include <LoggingKit/Config.h>
#include <LoggingKit/Context.h>
#include <ConfigKit/Schema.h>
#include <ConfigKit/TableTranslator.h>
#include <ConfigKit/PrefixTranslator.h>
#include <ServerKit/Context.h>
#include <ServerKit/HttpServer.h>
#include <WrapperRegistry/Registry.h>
#include <Core/Controller/Config.h>
#include <Core/SecurityUpdateChecker.h>
#include <Core/TelemetryCollector.h>
#include <Core/ApiServer.h>
#include <Core/AdminPanelConnector.h>
#include <Shared/ApiAccountUtils.h>
#include <Constants.h>
#include <Utils.h>
#include <IOTools/IOUtils.h>

namespace Passenger {
namespace Core {

using namespace std;


/*
 * BEGIN ConfigKit schema: Passenger::Core::Schema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   admin_panel_auth_type                                           string             -          default("basic")
 *   admin_panel_close_timeout                                       float              -          default(10.0)
 *   admin_panel_connect_timeout                                     float              -          default(30.0)
 *   admin_panel_data_debug                                          boolean            -          default(false)
 *   admin_panel_password                                            string             -          secret
 *   admin_panel_password_file                                       string             -          -
 *   admin_panel_ping_interval                                       float              -          default(30.0)
 *   admin_panel_ping_timeout                                        float              -          default(30.0)
 *   admin_panel_proxy_password                                      string             -          secret
 *   admin_panel_proxy_timeout                                       float              -          default(30.0)
 *   admin_panel_proxy_url                                           string             -          -
 *   admin_panel_proxy_username                                      string             -          -
 *   admin_panel_reconnect_timeout                                   float              -          default(5.0)
 *   admin_panel_url                                                 string             -          read_only
 *   admin_panel_username                                            string             -          -
 *   admin_panel_websocketpp_debug_access                            boolean            -          default(false)
 *   admin_panel_websocketpp_debug_error                             boolean            -          default(false)
 *   api_server_accept_burst_count                                   unsigned integer   -          default(32)
 *   api_server_addresses                                            array of strings   -          default([]),read_only
 *   api_server_authorizations                                       array              -          default("[FILTERED]"),secret
 *   api_server_client_freelist_limit                                unsigned integer   -          default(0)
 *   api_server_file_buffered_channel_auto_start_mover               boolean            -          default(true)
 *   api_server_file_buffered_channel_auto_truncate_file             boolean            -          default(true)
 *   api_server_file_buffered_channel_buffer_dir                     string             -          default
 *   api_server_file_buffered_channel_delay_in_file_mode_switching   unsigned integer   -          default(0)
 *   api_server_file_buffered_channel_max_disk_chunk_read_size       unsigned integer   -          default(0)
 *   api_server_file_buffered_channel_threshold                      unsigned integer   -          default(131072)
 *   api_server_mbuf_block_chunk_size                                unsigned integer   -          default(4096),read_only
 *   api_server_min_spare_clients                                    unsigned integer   -          default(0)
 *   api_server_request_freelist_limit                               unsigned integer   -          default(1024)
 *   api_server_start_reading_after_accept                           boolean            -          default(true)
 *   app_output_log_level                                            string             -          default("notice")
 *   benchmark_mode                                                  string             -          -
 *   config_manifest                                                 object             -          read_only
 *   controller_accept_burst_count                                   unsigned integer   -          default(32)
 *   controller_addresses                                            array of strings   -          default(["tcp://127.0.0.1:3000"]),read_only
 *   controller_client_freelist_limit                                unsigned integer   -          default(0)
 *   controller_cpu_affine                                           boolean            -          default(false),read_only
 *   controller_file_buffered_channel_auto_start_mover               boolean            -          default(true)
 *   controller_file_buffered_channel_auto_truncate_file             boolean            -          default(true)
 *   controller_file_buffered_channel_buffer_dir                     string             -          default
 *   controller_file_buffered_channel_delay_in_file_mode_switching   unsigned integer   -          default(0)
 *   controller_file_buffered_channel_max_disk_chunk_read_size       unsigned integer   -          default(0)
 *   controller_file_buffered_channel_threshold                      unsigned integer   -          default(131072)
 *   controller_mbuf_block_chunk_size                                unsigned integer   -          default(4096),read_only
 *   controller_min_spare_clients                                    unsigned integer   -          default(0)
 *   controller_request_freelist_limit                               unsigned integer   -          default(1024)
 *   controller_secure_headers_password                              any                -          secret
 *   controller_socket_backlog                                       unsigned integer   -          default(2048),read_only
 *   controller_start_reading_after_accept                           boolean            -          default(true)
 *   controller_threads                                              unsigned integer   -          default,read_only
 *   default_abort_websockets_on_process_shutdown                    boolean            -          default(true)
 *   default_app_file_descriptor_ulimit                              unsigned integer   -          -
 *   default_environment                                             string             -          default("production")
 *   default_force_max_concurrent_requests_per_process               integer            -          default(-1)
 *   default_friendly_error_pages                                    string             -          default("auto")
 *   default_group                                                   string             -          default
 *   default_load_shell_envvars                                      boolean            -          default(false)
 *   default_max_preloader_idle_time                                 unsigned integer   -          default(300)
 *   default_max_request_queue_size                                  unsigned integer   -          default(100)
 *   default_max_requests                                            unsigned integer   -          default(0)
 *   default_meteor_app_settings                                     string             -          -
 *   default_min_instances                                           unsigned integer   -          default(1)
 *   default_nodejs                                                  string             -          default("node")
 *   default_python                                                  string             -          default("python")
 *   default_ruby                                                    string             -          default("ruby")
 *   default_server_name                                             string             -          default
 *   default_server_port                                             unsigned integer   -          default
 *   default_spawn_method                                            string             -          default("smart")
 *   default_sticky_sessions                                         boolean            -          default(false)
 *   default_sticky_sessions_cookie_name                             string             -          default("_passenger_route")
 *   default_user                                                    string             -          default("nobody")
 *   file_descriptor_log_target                                      any                -          -
 *   file_descriptor_ulimit                                          unsigned integer   -          default(0),read_only
 *   graceful_exit                                                   boolean            -          default(true)
 *   instance_dir                                                    string             -          read_only
 *   integration_mode                                                string             -          default("standalone")
 *   log_level                                                       string             -          default("notice")
 *   log_target                                                      any                -          default({"stderr": true})
 *   max_instances_per_app                                           unsigned integer   -          read_only
 *   max_pool_size                                                   unsigned integer   -          default(6)
 *   multi_app                                                       boolean            -          default(false),read_only
 *   oom_score                                                       string             -          read_only
 *   passenger_root                                                  string             required   read_only
 *   pid_file                                                        string             -          read_only
 *   pool_idle_time                                                  unsigned integer   -          default(300)
 *   pool_selfchecks                                                 boolean            -          default(false)
 *   prestart_urls                                                   array of strings   -          default([]),read_only
 *   response_buffer_high_watermark                                  unsigned integer   -          default(134217728)
 *   security_update_checker_certificate_path                        string             -          -
 *   security_update_checker_disabled                                boolean            -          default(false)
 *   security_update_checker_interval                                unsigned integer   -          default(86400)
 *   security_update_checker_proxy_url                               string             -          -
 *   security_update_checker_url                                     string             -          default("https://securitycheck.phusionpassenger.com/v1/check.json")
 *   server_software                                                 string             -          default("Phusion_Passenger/5.3.7")
 *   show_version_in_header                                          boolean            -          default(true)
 *   single_app_mode_app_root                                        string             -          default,read_only
 *   single_app_mode_app_type                                        string             -          read_only
 *   single_app_mode_startup_file                                    string             -          read_only
 *   standalone_engine                                               string             -          default
 *   stat_throttle_rate                                              unsigned integer   -          default(10)
 *   telemetry_collector_ca_certificate_path                         string             -          -
 *   telemetry_collector_debug_curl                                  boolean            -          default(false)
 *   telemetry_collector_disabled                                    boolean            -          default(false)
 *   telemetry_collector_final_run_timeout                           unsigned integer   -          default(5)
 *   telemetry_collector_first_interval                              unsigned integer   -          default(7200)
 *   telemetry_collector_interval                                    unsigned integer   -          default(21600)
 *   telemetry_collector_interval_jitter                             unsigned integer   -          default(7200)
 *   telemetry_collector_proxy_url                                   string             -          -
 *   telemetry_collector_timeout                                     unsigned integer   -          default(180)
 *   telemetry_collector_url                                         string             -          default("https://anontelemetry.phusionpassenger.com/v1/collect.json")
 *   telemetry_collector_verify_server                               boolean            -          default(true)
 *   turbocaching                                                    boolean            -          default(true),read_only
 *   user_switching                                                  boolean            -          default(true)
 *   vary_turbocache_by_cookie                                       string             -          -
 *   watchdog_fd_passing_password                                    string             -          secret
 *   web_server_module_version                                       string             -          read_only
 *   web_server_version                                              string             -          read_only
 *
 * END
 */
class Schema: public ConfigKit::Schema {
private:
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

	static Json::Value getDefaultServerName(const ConfigKit::Store &store) {
		Json::Value addresses = store["controller_addresses"];
		if (addresses.size() > 0) {
			string firstAddress = addresses[0].asString();
			if (getSocketAddressType(firstAddress) == SAT_TCP) {
				string host;
				unsigned short port;

				parseTcpSocketAddress(firstAddress, host, port);
				return host;
			}
		}
		return "localhost";
	}

	static Json::Value getDefaultServerPort(const ConfigKit::Store &store) {
		Json::Value addresses = store["controller_addresses"];
		if (addresses.size() > 0) {
			string firstAddress = addresses[0].asString();
			if (getSocketAddressType(firstAddress) == SAT_TCP) {
				string host;
				unsigned short port;

				parseTcpSocketAddress(firstAddress, host, port);
				return port;
			}
		}
		return 80;
	}

	static Json::Value getDefaultThreads(const ConfigKit::Store &store) {
		return Json::UInt(boost::thread::hardware_concurrency());
	}

	static Json::Value getDefaultControllerAddresses() {
		Json::Value doc;
		doc.append(DEFAULT_HTTP_SERVER_LISTEN_ADDRESS);
		return doc;
	}

	static void validateMultiAppMode(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
		typedef ConfigKit::Error Error;

		if (!config["multi_app"].asBool()) {
			return;
		}

		if (!config["single_app_mode_app_type"].isNull()) {
			errors.push_back(Error("If '{{multi_app_mode}}' is set,"
				" then '{{single_app_mode_app_type}}' may not be set"));
		}
		if (!config["single_app_mode_startup_file"].isNull()) {
			errors.push_back(Error("If '{{multi_app_mode}}' is set,"
				" then '{{single_app_mode_startup_file}}' may not be set"));
		}
	}

	static void validateSingleAppMode(const ConfigKit::Store &config,
		const WrapperRegistry::Registry *wrapperRegistry, vector<ConfigKit::Error> &errors)
	{
		typedef ConfigKit::Error Error;

		if (config["multi_app"].asBool()) {
			return;
		}

		// single_app_mode_app_type and single_app_mode_startup_file are
		// autodetected in initializeSingleAppMode()

		ControllerSingleAppModeSchema::validateAppType("single_app_mode_app_type",
			wrapperRegistry, config, errors);
	}

	static void validateControllerSecureHeadersPassword(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
		typedef ConfigKit::Error Error;

		Json::Value password = config["controller_secure_headers_password"];
		if (password.isNull()) {
			return;
		}

		if (!password.isString() && !password.isObject()) {
			errors.push_back(Error("'{{controller_secure_headers_password}}' must be a string or an object"));
			return;
		}

		if (password.isObject()) {
			if (!password.isMember("path")) {
				errors.push_back(Error("If '{{controller_secure_headers_password}}' is an object, then it must contain a 'path' option"));
			} else if (!password["path"].isString()) {
				errors.push_back(Error("If '{{controller_secure_headers_password}}' is an object, then its 'path' option must be a string"));
			}
		}
	}

	static void validateApplicationPool(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
		typedef ConfigKit::Error Error;

		if (config["max_pool_size"].asUInt() < 1) {
			errors.push_back(Error("'{{max_pool_size}}' must be at least 1"));
		}
	}

	static void validateController(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
		typedef ConfigKit::Error Error;

		if (config["controller_threads"].asUInt() < 1) {
			errors.push_back(Error("'{{controller_threads}}' must be at least 1"));
		}
	}

	static void validateAddresses(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
		typedef ConfigKit::Error Error;

		if (config["controller_addresses"].empty()) {
			errors.push_back(Error("'{{controller_addresses}}' must contain at least 1 item"));
		} else if (config["controller_addresses"].size() > SERVER_KIT_MAX_SERVER_ENDPOINTS) {
			errors.push_back(Error("'{{controller_addresses}}' may contain at most "
				+ toString(SERVER_KIT_MAX_SERVER_ENDPOINTS) + " items"));
		}

		if (config["api_server_addresses"].size() > SERVER_KIT_MAX_SERVER_ENDPOINTS) {
			errors.push_back(Error("'{{api_server_addresses}}' may contain at most "
				+ toString(SERVER_KIT_MAX_SERVER_ENDPOINTS) + " items"));
		}
	}

	/*****************/
	/*****************/

	static Json::Value normalizeSingleAppMode(const Json::Value &effectiveValues) {
		if (effectiveValues["multi_app"].asBool()) {
			return Json::Value();
		}

		Json::Value updates;
		updates["single_app_mode_app_root"] = absolutizePath(
			effectiveValues["single_app_mode_app_root"].asString());
		if (!effectiveValues["single_app_mode_startup_file"].isNull()) {
			updates["single_app_mode_startup_file"] = absolutizePath(
				effectiveValues["single_app_mode_startup_file"].asString());
		}
		return updates;
	}

	static Json::Value normalizeServerSoftware(const Json::Value &effectiveValues) {
		string serverSoftware = effectiveValues["server_software"].asString();
		if (serverSoftware.find(SERVER_TOKEN_NAME) == string::npos
		 && serverSoftware.find(FLYING_PASSENGER_NAME) == string::npos)
		{
			serverSoftware.append(" " SERVER_TOKEN_NAME "/" PASSENGER_VERSION);
		}

		Json::Value updates;
		updates["server_software"] = serverSoftware;
		return updates;
	}

public:
	struct {
		LoggingKit::Schema schema;
		ConfigKit::TableTranslator translator;
	} loggingKit;
	struct {
		ControllerSchema schema;
		ConfigKit::TableTranslator translator;
	} controller;
	struct ControllerSingleAppModeSubschemaContainer {
		ControllerSingleAppModeSchema schema;
		ConfigKit::PrefixTranslator translator;

		ControllerSingleAppModeSubschemaContainer(const WrapperRegistry::Registry *registry)
			: schema(registry)
			{ }
	} controllerSingleAppMode;
	struct {
		ServerKit::Schema schema;
		ConfigKit::PrefixTranslator translator;
	} controllerServerKit;
	struct {
		SecurityUpdateChecker::Schema schema;
		ConfigKit::PrefixTranslator translator;
	} securityUpdateChecker;
	struct {
		TelemetryCollector::Schema schema;
		ConfigKit::PrefixTranslator translator;
	} telemetryCollector;
	struct {
		ApiServer::Schema schema;
		ConfigKit::TableTranslator translator;
	} apiServer;
	struct {
		ServerKit::Schema schema;
		ConfigKit::PrefixTranslator translator;
	} apiServerKit;
	struct {
		AdminPanelConnector::Schema schema;
		ConfigKit::TableTranslator translator;
	} adminPanelConnector;

	Schema(const WrapperRegistry::Registry *wrapperRegistry = NULL)
		: controllerSingleAppMode(wrapperRegistry)
	{
		using namespace ConfigKit;

		// Add subschema: loggingKit
		loggingKit.translator.add("log_level", "level");
		loggingKit.translator.add("log_target", "target");
		loggingKit.translator.finalize();
		addSubSchema(loggingKit.schema, loggingKit.translator);
		erase("redirect_stderr");
		erase("buffer_logs");

		// Add subschema: controller
		addSubSchemaPrefixTranslations<ServerKit::HttpServerSchema>(
			controller.translator, "controller_");
		controller.translator.finalize();
		addSubSchema(controller.schema, controller.translator);
		erase("thread_number");

		// Add subschema: controller (single app mode)
		controllerSingleAppMode.translator.setPrefixAndFinalize("single_app_mode_");
		addWithDynamicDefault("single_app_mode_app_root",
			STRING_TYPE, OPTIONAL | READ_ONLY | CACHE_DEFAULT_VALUE,
			ControllerSingleAppModeSchema::getDefaultAppRoot);
		add("single_app_mode_app_type", STRING_TYPE, OPTIONAL | READ_ONLY);
		add("single_app_mode_startup_file", STRING_TYPE, OPTIONAL | READ_ONLY);

		// Add subschema: controllerServerKit
		controllerServerKit.translator.setPrefixAndFinalize("controller_");
		addSubSchema(controllerServerKit.schema, controllerServerKit.translator);
		erase("controller_secure_mode_password");

		// Add subschema: securityUpdateChecker
		securityUpdateChecker.translator.setPrefixAndFinalize("security_update_checker_");
		addSubSchema(securityUpdateChecker.schema, securityUpdateChecker.translator);
		erase("security_update_checker_server_identifier");
		erase("security_update_checker_web_server_version");

		// Add subschema: telemetryCollector
		telemetryCollector.translator.setPrefixAndFinalize("telemetry_collector_");
		addSubSchema(telemetryCollector.schema, telemetryCollector.translator);

		// Add subschema: apiServer
		apiServer.translator.add("api_server_authorizations", "authorizations");
		addSubSchemaPrefixTranslations<ServerKit::HttpServerSchema>(
			apiServer.translator, "api_server_");
		apiServer.translator.finalize();
		addSubSchema(apiServer.schema, apiServer.translator);

		// Add subschema: apiServerKit
		apiServerKit.translator.setPrefixAndFinalize("api_server_");
		addSubSchema(apiServerKit.schema, apiServerKit.translator);
		erase("api_server_secure_mode_password");

		// Add subschema: adminPanelConnector
		addSubSchemaPrefixTranslations<WebSocketCommandReverseServer::Schema>(
			adminPanelConnector.translator, "admin_panel_");
		adminPanelConnector.translator.finalize();
		addSubSchema(adminPanelConnector.schema, adminPanelConnector.translator);
		erase("admin_panel_log_prefix");
		erase("ruby");

		override("admin_panel_url", STRING_TYPE, OPTIONAL | READ_ONLY);
		override("instance_dir", STRING_TYPE, OPTIONAL | READ_ONLY);
		override("multi_app", BOOL_TYPE, OPTIONAL | READ_ONLY, false);
		overrideWithDynamicDefault("default_server_name", STRING_TYPE, OPTIONAL, getDefaultServerName);
		overrideWithDynamicDefault("default_server_port", UINT_TYPE, OPTIONAL, getDefaultServerPort);

		add("passenger_root", STRING_TYPE, REQUIRED | READ_ONLY);
		add("config_manifest", OBJECT_TYPE, OPTIONAL | READ_ONLY);
		add("pid_file", STRING_TYPE, OPTIONAL | READ_ONLY);
		add("web_server_version", STRING_TYPE, OPTIONAL | READ_ONLY);
		add("oom_score", STRING_TYPE, OPTIONAL | READ_ONLY);
		addWithDynamicDefault("controller_threads", UINT_TYPE, OPTIONAL | READ_ONLY, getDefaultThreads);
		add("max_pool_size", UINT_TYPE, OPTIONAL, DEFAULT_MAX_POOL_SIZE);
		add("pool_idle_time", UINT_TYPE, OPTIONAL, Json::UInt(DEFAULT_POOL_IDLE_TIME));
		add("pool_selfchecks", BOOL_TYPE, OPTIONAL, false);
		add("prestart_urls", STRING_ARRAY_TYPE, OPTIONAL | READ_ONLY, Json::arrayValue);
		add("controller_secure_headers_password", ANY_TYPE, OPTIONAL | SECRET);
		add("controller_socket_backlog", UINT_TYPE, OPTIONAL | READ_ONLY, DEFAULT_SOCKET_BACKLOG);
		add("controller_addresses", STRING_ARRAY_TYPE, OPTIONAL | READ_ONLY, getDefaultControllerAddresses());
		add("api_server_addresses", STRING_ARRAY_TYPE, OPTIONAL | READ_ONLY, Json::arrayValue);
		add("controller_cpu_affine", BOOL_TYPE, OPTIONAL | READ_ONLY, false);
		add("file_descriptor_ulimit", UINT_TYPE, OPTIONAL | READ_ONLY, 0);

		addValidator(validateMultiAppMode);
		addValidator(boost::bind(validateSingleAppMode, boost::placeholders::_1,
			wrapperRegistry, boost::placeholders::_2));
		addValidator(validateControllerSecureHeadersPassword);
		addValidator(validateApplicationPool);
		addValidator(validateController);
		addValidator(validateAddresses);
		addNormalizer(normalizeSingleAppMode);
		addNormalizer(normalizeServerSoftware);

		/*******************/
		/*******************/

		finalize();
	}
};


} // namespace Core
} // namespace Passenger

#endif /* _PASSENGER_CORE_CONFIG_H_ */
