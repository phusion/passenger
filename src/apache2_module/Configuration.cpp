/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#include <algorithm>
#include <cstdlib>
#include <climits>

/* ap_config.h checks whether the compiler has support for C99's designated
 * initializers, and defines AP_HAVE_DESIGNATED_INITIALIZER if it does. However,
 * g++ does not support designated initializers, even when ap_config.h thinks
 * it does. Here we undefine the macro to force httpd_config.h to not use
 * designated initializers. This should fix compilation problems on some systems.
 */
#include <ap_config.h>
#undef AP_HAVE_DESIGNATED_INITIALIZER

#include "Configuration.hpp"
#include <Utils.h>
#include <Constants.h>
#include <UnionStationFilterSupport.h>

/* The APR/Apache headers must come after the Passenger headers. See Hooks.cpp
 * to learn why.
 */
#include <apr_strings.h>
// In Apache < 2.4, this macro was necessary for core_dir_config and other structs
#define CORE_PRIVATE
#include <http_core.h>

using namespace Passenger;

extern "C" module AP_MODULE_DECLARE_DATA passenger_module;

namespace Passenger { ServerConfig serverConfig; }

#define MERGE_THREEWAY_CONFIG(field) \
	config->field = (add->field == DirConfig::UNSET) ? base->field : add->field
#define MERGE_STR_CONFIG(field) \
	config->field = (add->field == NULL) ? base->field : add->field
#define MERGE_STRING_CONFIG(field) \
	config->field = (add->field.empty()) ? base->field : add->field
#define MERGE_INT_CONFIG(field) \
	config->field = (add->field ## Specified) ? add->field : base->field; \
	config->field ## Specified = base->field ## Specified || add->field ## Specified

#define DEFINE_DIR_STR_CONFIG_SETTER(functionName, fieldName)                    \
	static const char *                                                      \
	functionName(cmd_parms *cmd, void *pcfg, const char *arg) {              \
		DirConfig *config = (DirConfig *) pcfg;                          \
		config->fieldName = arg;                                         \
		return NULL;                                                     \
	}
#define DEFINE_DIR_INT_CONFIG_SETTER(functionName, fieldName, integerType, minValue)            \
	static const char *                                                                     \
	functionName(cmd_parms *cmd, void *pcfg, const char *arg) {                             \
		DirConfig *config = (DirConfig *) pcfg;                                         \
		char *end;                                                                      \
		long int result;                                                                \
		                                                                                \
		result = strtol(arg, &end, 10);                                                 \
		if (*end != '\0') {                                                             \
			string message = "Invalid number specified for ";                       \
			message.append(cmd->directive->directive);                              \
			message.append(".");                                                    \
			                                                                        \
			char *messageStr = (char *) apr_palloc(cmd->temp_pool,                  \
				message.size() + 1);                                            \
			memcpy(messageStr, message.c_str(), message.size() + 1);                \
			return messageStr;                                                      \
		} else if (result < minValue) {                                                 \
			string message = "Value for ";                                          \
			message.append(cmd->directive->directive);                              \
			message.append(" must be greater than or equal to " #minValue ".");     \
			                                                                        \
			char *messageStr = (char *) apr_palloc(cmd->temp_pool,                  \
				message.size() + 1);                                            \
			memcpy(messageStr, message.c_str(), message.size() + 1);                \
			return messageStr;                                                      \
		} else {                                                                        \
			config->fieldName = (integerType) result;                               \
			config->fieldName ## Specified = true;                                  \
			return NULL;                                                            \
		}                                                                               \
	}
#define DEFINE_DIR_THREEWAY_CONFIG_SETTER(functionName, fieldName)           \
	static const char *                                                  \
	functionName(cmd_parms *cmd, void *pcfg, const char *arg) {          \
		DirConfig *config = (DirConfig *) pcfg;                      \
		if (arg) {                                                   \
			config->fieldName = DirConfig::ENABLED;              \
		} else {                                      	             \
			config->fieldName = DirConfig::DISABLED;             \
		}                                                            \
		return NULL;                                                 \
	}

#define DEFINE_SERVER_STR_CONFIG_SETTER(functionName, fieldName)                 \
	static const char *                                                      \
	functionName(cmd_parms *cmd, void *dummy, const char *arg) {             \
		serverConfig.fieldName = arg;                                    \
		return NULL;                                                     \
	}
#define DEFINE_SERVER_BOOLEAN_CONFIG_SETTER(functionName, fieldName)             \
	static const char *                                                      \
	functionName(cmd_parms *cmd, void *dummy, int arg) {                     \
		serverConfig.fieldName = arg;                                    \
		return NULL;                                                     \
	}
#define DEFINE_SERVER_INT_CONFIG_SETTER(functionName, fieldName, integerType, minValue)         \
	static const char *                                                                     \
	functionName(cmd_parms *cmd, void *pcfg, const char *arg) {                             \
		char *end;                                                                      \
		long int result;                                                                \
		                                                                                \
		result = strtol(arg, &end, 10);                                                 \
		if (*end != '\0') {                                                             \
			string message = "Invalid number specified for ";                       \
			message.append(cmd->directive->directive);                              \
			message.append(".");                                                    \
			                                                                        \
			char *messageStr = (char *) apr_palloc(cmd->temp_pool,                  \
				message.size() + 1);                                            \
			memcpy(messageStr, message.c_str(), message.size() + 1);                \
			return messageStr;                                                      \
		} else if (result < minValue) {                                                 \
			string message = "Value for ";                                          \
			message.append(cmd->directive->directive);                              \
			message.append(" must be greater than or equal to " #minValue ".");     \
			                                                                        \
			char *messageStr = (char *) apr_palloc(cmd->temp_pool,                  \
				message.size() + 1);                                            \
			memcpy(messageStr, message.c_str(), message.size() + 1);                \
			return messageStr;                                                      \
		} else {                                                                        \
			serverConfig.fieldName = (integerType) result;                          \
			return NULL;                                                            \
		}                                                                               \
	}


template<typename T> static apr_status_t
destroy_config_struct(void *x) {
	delete (T *) x;
	return APR_SUCCESS;
}

template<typename Collection, typename T> static bool
contains(const Collection &coll, const T &item) {
	typename Collection::const_iterator it;
	for (it = coll.begin(); it != coll.end(); it++) {
		if (*it == item) {
			return true;
		}
	}
	return false;
}


extern "C" {

static DirConfig *
create_dir_config_struct(apr_pool_t *pool) {
	DirConfig *config = new DirConfig();
	apr_pool_cleanup_register(pool, config, destroy_config_struct<DirConfig>, apr_pool_cleanup_null);
	return config;
}

void *
passenger_config_create_dir(apr_pool_t *p, char *dirspec) {
	DirConfig *config = create_dir_config_struct(p);

	#include "CreateDirConfig.cpp"

	config->appRoot = NULL;
	config->resolveSymlinksInDocRoot = DirConfig::UNSET;
	config->allowEncodedSlashes = DirConfig::UNSET;
	config->unionStationSupport = DirConfig::UNSET;
	config->bufferResponse = DirConfig::UNSET;
	/*************************************/
	return config;
}

void *
passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv) {
	DirConfig *config = create_dir_config_struct(p);
	DirConfig *base = (DirConfig *) basev;
	DirConfig *add = (DirConfig *) addv;

	#include "MergeDirConfig.cpp"

	config->baseURIs = base->baseURIs;
	for (set<string>::const_iterator it(add->baseURIs.begin()); it != add->baseURIs.end(); it++) {
		config->baseURIs.insert(*it);
	}

	MERGE_STR_CONFIG(appRoot);
	MERGE_STRING_CONFIG(unionStationKey);
	config->unionStationFilters = base->unionStationFilters;
	for (vector<string>::const_iterator it = add->unionStationFilters.begin(); it != add->unionStationFilters.end(); it++) {
		if (!contains(config->unionStationFilters, *it)) {
			config->unionStationFilters.push_back(*it);
		}
	}
	MERGE_THREEWAY_CONFIG(resolveSymlinksInDocRoot);
	MERGE_THREEWAY_CONFIG(allowEncodedSlashes);
	MERGE_THREEWAY_CONFIG(unionStationSupport);
	MERGE_THREEWAY_CONFIG(bufferResponse);
	/*************************************/
	return config;
}

static void
postprocessDirConfig(server_rec *s, core_dir_config *core_dconf,
	DirConfig *psg_dconf, bool isTopLevel = false)
{
	if (psg_dconf->unionStationSupport == DirConfig::ENABLED) {
		serverConfig.unionStationSupport = true;
	}
}

#ifndef ap_get_core_module_config
	#define ap_get_core_module_config(s) ap_get_module_config(s, &core_module)
#endif

void
passenger_postprocess_config(server_rec *s) {
	core_server_config *sconf;
	core_dir_config *core_dconf;
	DirConfig *psg_dconf;
	int nelts;
    ap_conf_vector_t **elts;
    int i;

	serverConfig.finalize();

	for (; s != NULL; s = s->next) {
		sconf = (core_server_config *) ap_get_core_module_config(s->module_config);
		core_dconf = (core_dir_config *) ap_get_core_module_config(s->lookup_defaults);
		psg_dconf = (DirConfig *) ap_get_module_config(s->lookup_defaults, &passenger_module);
		postprocessDirConfig(s, core_dconf, psg_dconf, true);

		nelts = sconf->sec_dir->nelts;
		elts  = (ap_conf_vector_t **) sconf->sec_dir->elts;
		for (i = 0; i < nelts; ++i) {
			core_dconf = (core_dir_config *) ap_get_core_module_config(elts[i]);
			psg_dconf = (DirConfig *) ap_get_module_config(elts[i], &passenger_module);
			if (core_dconf != NULL && psg_dconf != NULL) {
				postprocessDirConfig(s, core_dconf, psg_dconf);
			}
		}

		nelts = sconf->sec_url->nelts;
		elts  = (ap_conf_vector_t **) sconf->sec_url->elts;
		for (i = 0; i < nelts; ++i) {
			core_dconf = (core_dir_config *) ap_get_core_module_config(elts[i]);
			psg_dconf = (DirConfig *) ap_get_module_config(elts[i], &passenger_module);
			if (core_dconf != NULL && psg_dconf != NULL) {
				postprocessDirConfig(s, core_dconf, psg_dconf);
			}
		}
	}
}


/*************************************************
 * Passenger settings
 *************************************************/

DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_root, root)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_default_ruby, defaultRuby)
DEFINE_SERVER_INT_CONFIG_SETTER(cmd_passenger_log_level, logLevel, unsigned int, 0)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_log_file, logFile)
DEFINE_SERVER_INT_CONFIG_SETTER(cmd_passenger_socket_backlog, socketBacklog, unsigned int, 0)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_file_descriptor_log_file, fileDescriptorLogFile)
DEFINE_SERVER_INT_CONFIG_SETTER(cmd_passenger_max_pool_size, maxPoolSize, unsigned int, 1)
DEFINE_SERVER_INT_CONFIG_SETTER(cmd_passenger_pool_idle_time, poolIdleTime, unsigned int, 0)
DEFINE_SERVER_INT_CONFIG_SETTER(cmd_passenger_response_buffer_high_watermark, responseBufferHighWatermark, unsigned int, 0)
DEFINE_SERVER_INT_CONFIG_SETTER(cmd_passenger_stat_throttle_rate, statThrottleRate, unsigned int, 0)
DEFINE_SERVER_BOOLEAN_CONFIG_SETTER(cmd_passenger_user_switching, userSwitching)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_default_user, defaultUser)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_default_group, defaultGroup)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_data_buffer_dir, dataBufferDir)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_instance_registry_dir, instanceRegistryDir)
DEFINE_SERVER_BOOLEAN_CONFIG_SETTER(cmd_passenger_disable_security_update_check, disableSecurityUpdateCheck)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_security_update_check_proxy, securityUpdateCheckProxy)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_union_station_gateway_address, unionStationGatewayAddress)
DEFINE_SERVER_INT_CONFIG_SETTER(cmd_union_station_gateway_port, unionStationGatewayPort, int, 1)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_union_station_gateway_cert, unionStationGatewayCert)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_union_station_proxy_address, unionStationProxyAddress)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_analytics_log_user, analyticsLogUser)
DEFINE_SERVER_STR_CONFIG_SETTER(cmd_passenger_analytics_log_group, analyticsLogGroup)
DEFINE_SERVER_BOOLEAN_CONFIG_SETTER(cmd_passenger_turbocaching, turbocaching)

static const char *
cmd_passenger_ctl(cmd_parms *cmd, void *dummy, const char *name, const char *value) {
	serverConfig.ctl.set(name, value);
	return NULL;
}

static const char *
cmd_passenger_pre_start(cmd_parms *cmd, void *pcfg, const char *arg) {
	serverConfig.prestartURLs.insert(arg);
	return NULL;
}

#include "ConfigurationSetters.cpp"

DEFINE_DIR_STR_CONFIG_SETTER(cmd_passenger_app_root, appRoot)
DEFINE_DIR_STR_CONFIG_SETTER(cmd_union_station_key, unionStationKey)
DEFINE_DIR_THREEWAY_CONFIG_SETTER(cmd_passenger_resolve_symlinks_in_document_root, resolveSymlinksInDocRoot)
DEFINE_DIR_THREEWAY_CONFIG_SETTER(cmd_passenger_allow_encoded_slashes, allowEncodedSlashes)
DEFINE_DIR_THREEWAY_CONFIG_SETTER(cmd_union_station_support, unionStationSupport)
DEFINE_DIR_THREEWAY_CONFIG_SETTER(cmd_passenger_buffer_response, bufferResponse)

#ifndef PASSENGER_IS_ENTERPRISE
static const char *
cmd_passenger_enterprise_only(cmd_parms *cmd, void *pcfg, const char *arg) {
	return "this feature is only available in Phusion Passenger Enterprise. "
		"You are currently running the open source Phusion Passenger Enterprise. "
		"Please learn more about and/or buy Phusion Passenger Enterprise at https://www.phusionpassenger.com/enterprise";
}
#endif

static const char *
cmd_passenger_spawn_method(cmd_parms *cmd, void *pcfg, const char *arg) {
	DirConfig *config = (DirConfig *) pcfg;
	if (strcmp(arg, "smart") == 0 || strcmp(arg, "smart-lv2") == 0) {
		config->spawnMethod = "smart";
	} else if (strcmp(arg, "conservative") == 0 || strcmp(arg, "direct") == 0) {
		config->spawnMethod = "direct";
	} else {
		return "PassengerSpawnMethod may only be 'smart', 'direct'.";
	}
	return NULL;
}

static const char *
cmd_passenger_base_uri(cmd_parms *cmd, void *pcfg, const char *arg) {
	DirConfig *config = (DirConfig *) pcfg;
	if (strlen(arg) == 0) {
		return "PassengerBaseURI may not be set to the empty string";
	} else if (arg[0] != '/') {
		return "PassengerBaseURI must start with a slash (/)";
	} else if (strlen(arg) > 1 && arg[strlen(arg) - 1] == '/') {
		return "PassengerBaseURI must not end with a slash (/)";
	} else {
		config->baseURIs.insert(arg);
		return NULL;
	}
}

static const char *
cmd_union_station_filter(cmd_parms *cmd, void *pcfg, const char *arg) {
	DirConfig *config = (DirConfig *) pcfg;
	if (strlen(arg) == 0) {
		return "UnionStationFilter may not be set to the empty string";
	} else {
		try {
			FilterSupport::Filter f(arg);
			config->unionStationFilters.push_back(arg);
			return NULL;
		} catch (const SyntaxError &e) {
			string message = "Syntax error in Union Station filter: ";
			message.append(e.what());
			return strdup(message.c_str());
		}
	}
}


/*************************************************
 * Obsolete settings
 *************************************************/

static const char *
cmd_rails_spawn_server(cmd_parms *cmd, void *pcfg, const char *arg) {
	fprintf(stderr, "WARNING: The 'RailsSpawnServer' option is obsolete. "
		"Please specify 'PassengerRoot' instead. The correct value was "
		"given to you by 'passenger-install-apache2-module'.\n");
	fflush(stderr);
	return NULL;
}

static const char *
cmd_rails_allow_mod_rewrite(cmd_parms *cmd, void *pcfg, int arg) {
	fprintf(stderr, "WARNING: The 'RailsAllowModRewrite' option is obsolete: "
		"Phusion Passenger now fully supports mod_rewrite. "
		"Please remove this option from your configuration file.\n");
	fflush(stderr);
	return NULL;
}

static const char *
cmd_rails_framework_spawner_idle_time(cmd_parms *cmd, void *pcfg, const char *arg) {
	fprintf(stderr, "WARNING: The 'RailsFrameworkSpawnerIdleTime' option is obsolete. "
		"Please use 'PassengerMaxPreloaderIdleTime' instead.\n");
	fflush(stderr);
	return NULL;
}

static const char *
cmd_passenger_use_global_queue(cmd_parms *cmd, void *pcfg, int arg) {
	fprintf(stderr, "WARNING: The 'PassengerUseGlobalQueue' option is obsolete: "
		"global queueing is now always turned on. "
		"Please remove this option from your configuration file.\n");
	fflush(stderr);
	return NULL;
}


typedef const char * (*Take1Func)();
typedef const char * (*Take2Func)();
typedef const char * (*FlagFunc)();

const command_rec passenger_commands[] = {
	// Passenger settings.
	AP_INIT_TAKE1("PassengerRoot",
		(Take1Func) cmd_passenger_root,
		NULL,
		RSRC_CONF,
		"The Passenger root folder."),
	AP_INIT_TAKE2("PassengerCtl",
		(Take2Func) cmd_passenger_ctl,
		NULL,
		RSRC_CONF,
		"Set advanced options."),
	AP_INIT_TAKE1("PassengerDefaultRuby",
		(Take1Func) cmd_passenger_default_ruby,
		NULL,
		RSRC_CONF,
		"The default Ruby interpreter to use."),
	AP_INIT_TAKE1("PassengerLogLevel",
		(Take1Func) cmd_passenger_log_level,
		NULL,
		RSRC_CONF,
		"Passenger log verbosity."),
	AP_INIT_TAKE1("PassengerLogFile",
		(Take1Func) cmd_passenger_log_file,
		NULL,
		RSRC_CONF,
		"Passenger log file."),
	AP_INIT_TAKE1("PassengerSocketBacklog",
		(Take1Func) cmd_passenger_socket_backlog,
		NULL,
		RSRC_CONF,
		"Override size of the socket backlog."),
	AP_INIT_TAKE1("PassengerFileDescriptorLogFile",
		(Take1Func) cmd_passenger_file_descriptor_log_file,
		NULL,
		RSRC_CONF,
		"Passenger file descriptor log file."),
	AP_INIT_TAKE1("PassengerMaxPoolSize",
		(Take1Func) cmd_passenger_max_pool_size,
		NULL,
		RSRC_CONF,
		"The maximum number of simultaneously alive application instances."),
	AP_INIT_TAKE1("PassengerPoolIdleTime",
		(Take1Func) cmd_passenger_pool_idle_time,
		NULL,
		RSRC_CONF,
		"The maximum number of seconds that an application may be idle before it gets terminated."),
	AP_INIT_TAKE1("PassengerResponseBufferHighWatermark",
		(Take1Func) cmd_passenger_response_buffer_high_watermark,
		NULL,
		RSRC_CONF,
		"The maximum size of the response buffer."),
	AP_INIT_FLAG("PassengerUserSwitching",
		(FlagFunc) cmd_passenger_user_switching,
		NULL,
		RSRC_CONF,
		"Whether to enable user switching support."),
	AP_INIT_TAKE1("PassengerDefaultUser",
		(Take1Func) cmd_passenger_default_user,
		NULL,
		RSRC_CONF,
		"The user that Ruby applications must run as when user switching fails or is disabled."),
	AP_INIT_TAKE1("PassengerDefaultGroup",
		(Take1Func) cmd_passenger_default_group,
		NULL,
		RSRC_CONF,
		"The group that Ruby applications must run as when user switching fails or is disabled."),
	AP_INIT_TAKE1("PassengerDataBufferDir",
		(Take1Func) cmd_passenger_data_buffer_dir,
		NULL,
		RSRC_CONF,
		"The directory that data buffers should be stored into."),
	AP_INIT_TAKE1("PassengerInstanceRegistryDir",
		(Take1Func) cmd_passenger_instance_registry_dir,
		NULL,
		RSRC_CONF,
		"The directory to register the instance to."),
	AP_INIT_FLAG("PassengerDisableSecurityUpdateCheck",
		(FlagFunc) cmd_passenger_disable_security_update_check,
		NULL,
		RSRC_CONF,
		"Whether to enable the security update check & notify."),
	AP_INIT_TAKE1("PassengerSecurityUpdateCheckProxy",
		(Take1Func) cmd_passenger_security_update_check_proxy,
		NULL,
		RSRC_CONF,
		"Use specified http/SOCKS proxy for the security update check."),
	AP_INIT_TAKE1("PassengerMaxPreloaderIdleTime",
		(Take1Func) cmd_passenger_max_preloader_idle_time,
		NULL,
		RSRC_CONF,
		"The maximum number of seconds that a preloader process may be idle before it is shutdown."),
	AP_INIT_TAKE1("PassengerStatThrottleRate",
		(Take1Func) cmd_passenger_stat_throttle_rate,
		NULL,
		RSRC_CONF,
		"Limit the number of stat calls to once per given seconds."),
	AP_INIT_TAKE1("UnionStationGatewayAddress",
		(Take1Func) cmd_union_station_gateway_address,
		NULL,
		RSRC_CONF,
		"The Union Station Gateway host name."),
	AP_INIT_TAKE1("UnionStationGatewayPort",
		(Take1Func) cmd_union_station_gateway_port,
		NULL,
		RSRC_CONF,
		"The Union Station Gateway port number."),
	AP_INIT_TAKE1("UnionStationGatewayCert",
		(Take1Func) cmd_union_station_gateway_cert,
		NULL,
		RSRC_CONF,
		"The Union Station Gateway certificate."),
	AP_INIT_TAKE1("UnionStationProxyAddress",
		(Take1Func) cmd_union_station_proxy_address,
		NULL,
		RSRC_CONF,
		"The address of the proxy that should be used for sending data to Union Station."),
	AP_INIT_TAKE1("PassengerAnalyticsLogUser",
		(Take1Func) cmd_passenger_analytics_log_user,
		NULL,
		RSRC_CONF,
		"The owner of analytics files."),
	AP_INIT_TAKE1("PassengerAnalyticsLogGroup",
		(Take1Func) cmd_passenger_analytics_log_group,
		NULL,
		RSRC_CONF,
		"The group of analytics files."),
	AP_INIT_TAKE1("PassengerPreStart",
		(Take1Func) cmd_passenger_pre_start,
		NULL,
		RSRC_CONF,
		"Prestart the given web applications during startup."),
	AP_INIT_FLAG("PassengerTurbocaching",
		(FlagFunc) cmd_passenger_turbocaching,
		NULL,
		RSRC_CONF,
		"Whether to enable turbocaching."),

	#include "ConfigurationCommands.cpp"

	AP_INIT_TAKE1("PassengerAppRoot",
		(Take1Func) cmd_passenger_app_root,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"The application's root directory."),
	AP_INIT_TAKE1("UnionStationKey",
		(Take1Func) cmd_union_station_key,
		NULL,
		OR_ALL,
		"The Union Station key."),
	AP_INIT_TAKE1("UnionStationFilter",
		(Take1Func) cmd_union_station_filter,
		NULL,
		OR_ALL,
		"A filter for Union Station data."),
	AP_INIT_FLAG("PassengerBufferResponse",
		(FlagFunc) cmd_passenger_buffer_response,
		NULL,
		OR_ALL,
		"Whether to enable buffering response."),
	AP_INIT_FLAG("PassengerResolveSymlinksInDocumentRoot",
		(FlagFunc) cmd_passenger_resolve_symlinks_in_document_root,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Whether to resolve symlinks in the DocumentRoot path"),
	AP_INIT_FLAG("PassengerAllowEncodedSlashes",
		(FlagFunc) cmd_passenger_allow_encoded_slashes,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Whether to support encoded slashes in the URL"),
	AP_INIT_TAKE1("PassengerBaseURI",
		(Take1Func) cmd_passenger_base_uri,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Declare the given base URI as belonging to an application."),
	AP_INIT_FLAG("UnionStationSupport",
		(Take1Func) cmd_union_station_support,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Whether to enable logging through Union Station."),

	/*****************************/
	AP_INIT_TAKE1("PassengerMemoryLimit",
		(Take1Func) cmd_passenger_enterprise_only,
		NULL,
		OR_LIMIT | ACCESS_CONF | RSRC_CONF,
		"The maximum amount of memory in MB that an application instance may use."),
	AP_INIT_TAKE1("PassengerMaxInstances",
		(Take1Func) cmd_passenger_enterprise_only,
		NULL,
		OR_LIMIT | ACCESS_CONF | RSRC_CONF,
		"The maximum number of instances for the current application that Passenger may spawn."),
	AP_INIT_TAKE1("PassengerMaxRequestTime",
		(Take1Func) cmd_passenger_enterprise_only,
		NULL,
		OR_ALL,
		"The maximum time (in seconds) that the current application may spend on a request."),
	AP_INIT_FLAG("PassengerRollingRestarts",
		(FlagFunc) cmd_passenger_enterprise_only,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Whether to turn on rolling restarts"),
	AP_INIT_FLAG("PassengerResistDeploymentErrors",
		(FlagFunc) cmd_passenger_enterprise_only,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Whether to turn on deployment error resistance"),
	AP_INIT_FLAG("PassengerDebugger",
		(FlagFunc) cmd_passenger_enterprise_only,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Whether to turn on debugger support"),
	AP_INIT_TAKE1("PassengerConcurrencyModel",
		(Take1Func) cmd_passenger_enterprise_only,
		NULL,
		OR_ALL,
		"The concurrency model that should be used for applications."),
	AP_INIT_TAKE1("PassengerThreadCount",
		(Take1Func) cmd_passenger_enterprise_only,
		NULL,
		OR_ALL,
		"The number of threads that Phusion Passenger should spawn per application."),

	// Backwards compatibility options.
	AP_INIT_TAKE1("PassengerDebugLogFile",
		(Take1Func) cmd_passenger_log_file,
		NULL,
		RSRC_CONF,
		"Passenger log file."),
	AP_INIT_TAKE1("RailsRuby",
		(Take1Func) cmd_passenger_ruby,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Deprecated option."),
	AP_INIT_TAKE1("RailsMaxPoolSize",
		(Take1Func) cmd_passenger_max_pool_size,
		NULL,
		RSRC_CONF,
		"Deprecated option."),
	AP_INIT_TAKE1("RailsMaxInstancesPerApp",
		(Take1Func) cmd_passenger_max_instances_per_app,
		NULL,
		RSRC_CONF,
		"Deprecated option"),
	AP_INIT_TAKE1("RailsPoolIdleTime",
		(Take1Func) cmd_passenger_pool_idle_time,
		NULL,
		RSRC_CONF,
		"Deprecated option."),
	AP_INIT_FLAG("RailsUserSwitching",
		(FlagFunc) cmd_passenger_user_switching,
		NULL,
		RSRC_CONF,
		"Deprecated option."),
	AP_INIT_TAKE1("RailsDefaultUser",
		(Take1Func) cmd_passenger_default_user,
		NULL,
		RSRC_CONF,
		"Deprecated option."),
	AP_INIT_TAKE1("RailsAppSpawnerIdleTime",
		(Take1Func) cmd_passenger_max_preloader_idle_time,
		NULL,
		RSRC_CONF,
		"Deprecated option."),
	AP_INIT_TAKE1("RailsBaseURI",
		(Take1Func) cmd_passenger_base_uri,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Deprecated option."),
	AP_INIT_TAKE1("RackBaseURI",
		(Take1Func) cmd_passenger_base_uri,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Deprecated option."),

	// Obsolete options.
	AP_INIT_TAKE1("RailsSpawnServer",
		(Take1Func) cmd_rails_spawn_server,
		NULL,
		RSRC_CONF,
		"Obsolete option."),
	AP_INIT_FLAG("RailsAllowModRewrite",
		(FlagFunc) cmd_rails_allow_mod_rewrite,
		NULL,
		RSRC_CONF,
		"Obsolete option."),
	AP_INIT_TAKE1("RailsFrameworkSpawnerIdleTime",
		(Take1Func) cmd_rails_framework_spawner_idle_time,
		NULL,
		RSRC_CONF,
		"Obsolete option."),
	AP_INIT_FLAG("PassengerUseGlobalQueue",
		(FlagFunc) cmd_passenger_use_global_queue,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Obsolete option."),

	{ NULL }
};

} // extern "C"
