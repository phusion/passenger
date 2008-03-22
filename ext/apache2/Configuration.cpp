#include <apr_strings.h>
#include <algorithm>
#include <cstdlib>
#include "Configuration.h"
#include "Utils.h"

using namespace Passenger;

extern "C" module AP_MODULE_DECLARE_DATA passenger_module;

#define DEFAULT_MAX_POOL_SIZE 20
#define DEFAULT_POOL_IDLE_TIME 120


template<typename T> static apr_status_t
destroy_config_struct(void *x) {
	delete (T *) x;
	return APR_SUCCESS;
}

static DirConfig *
create_dir_config_struct(apr_pool_t *pool) {
	DirConfig *config = new DirConfig();
	apr_pool_cleanup_register(pool, config, destroy_config_struct<DirConfig>, apr_pool_cleanup_null);
	return config;
}

static ServerConfig *
create_server_config_struct(apr_pool_t *pool) {
	ServerConfig *config = new ServerConfig();
	apr_pool_cleanup_register(pool, config, destroy_config_struct<ServerConfig>, apr_pool_cleanup_null);
	return config;
}

extern "C" {

void *
passenger_config_create_dir(apr_pool_t *p, char *dirspec) {
	DirConfig *config = create_dir_config_struct(p);
	config->autoDetect = DirConfig::UNSET;
	return config;
}

void *
passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv) {
	DirConfig *config = create_dir_config_struct(p);
	DirConfig *base = (DirConfig *) basev;
	DirConfig *add = (DirConfig *) addv;
	
	config->base_uris = base->base_uris;
	for (set<string>::const_iterator it(add->base_uris.begin()); it != add->base_uris.end(); it++) {
		config->base_uris.insert(*it);
	}
	
	config->autoDetect = (add->autoDetect == DirConfig::UNSET) ? base->autoDetect : add->autoDetect;
	return config;
}

void *
passenger_config_create_server(apr_pool_t *p, server_rec *s) {
	ServerConfig *config = create_server_config_struct(p);
	config->ruby = NULL;
	config->env = NULL;
	config->spawnServer = NULL;
	config->maxPoolSize = DEFAULT_MAX_POOL_SIZE;
	config->maxPoolSizeSpecified = false;
	config->poolIdleTime = DEFAULT_POOL_IDLE_TIME;
	config->poolIdleTimeSpecified = false;
	config->userSwitching = true;
	config->userSwitchingSpecified = false;
	config->user = NULL;
	return config;
}

void *
passenger_config_merge_server(apr_pool_t *p, void *basev, void *addv) {
	ServerConfig *config = create_server_config_struct(p);
	ServerConfig *base = (ServerConfig *) basev;
	ServerConfig *add = (ServerConfig *) addv;
	
	config->ruby = (add->ruby == NULL) ? base->ruby : add->ruby;
	config->env = (add->env == NULL) ? base->env : add->env;
	config->spawnServer = (add->spawnServer == NULL) ? base->spawnServer : add->spawnServer;
	config->maxPoolSize = (add->maxPoolSizeSpecified) ? base->maxPoolSize : add->maxPoolSize;
	config->maxPoolSizeSpecified = base->maxPoolSizeSpecified || add->maxPoolSizeSpecified;
	config->poolIdleTime = (add->poolIdleTime) ? base->poolIdleTime : add->poolIdleTime;
	config->poolIdleTimeSpecified = base->poolIdleTimeSpecified || add->poolIdleTimeSpecified;
	config->userSwitching = (add->userSwitchingSpecified) ? add->userSwitching : base->userSwitching;
	config->userSwitchingSpecified = base->userSwitchingSpecified || add->userSwitchingSpecified;
	config->user = (add->user == NULL) ? base->user : add->user;
	return config;
}

void
passenger_config_merge_all_servers(apr_pool_t *pool, server_rec *main_server) {
	ServerConfig *final = (ServerConfig *) passenger_config_create_server(pool, main_server);
	server_rec *s;
	
	for (s = main_server; s != NULL; s = s->next) {
		ServerConfig *config = (ServerConfig *) ap_get_module_config(s->module_config, &passenger_module);
		final->ruby = (final->ruby != NULL) ? final->ruby : config->ruby;
		final->env = (final->env != NULL) ? final->env : config->env;
		final->spawnServer = (final->spawnServer != NULL) ? final->spawnServer : config->spawnServer;
		final->maxPoolSize = (final->maxPoolSizeSpecified) ? final->maxPoolSize : config->maxPoolSize;
		final->maxPoolSizeSpecified = final->maxPoolSizeSpecified || config->maxPoolSizeSpecified;
		final->poolIdleTime = (final->poolIdleTimeSpecified) ? final->poolIdleTime : config->poolIdleTime;
		final->poolIdleTimeSpecified = final->poolIdleTimeSpecified || config->poolIdleTimeSpecified;
		final->userSwitching = (config->userSwitchingSpecified) ? config->userSwitching : final->userSwitching;
		final->userSwitchingSpecified = final->userSwitchingSpecified || config->userSwitchingSpecified;
		final->user = (final->user != NULL) ? final->user : config->user;
	}
	for (s = main_server; s != NULL; s = s->next) {
		ServerConfig *config = (ServerConfig *) ap_get_module_config(s->module_config, &passenger_module);
		*config = *final;
	}
}

static const char *
cmd_rails_base_uri(cmd_parms *cmd, void *pcfg, const char *arg) {
	DirConfig *config = (DirConfig *) pcfg;
	config->base_uris.insert(arg);
	return NULL;
}

static const char *
cmd_rails_auto_detect(cmd_parms *cmd, void *pcfg, int arg) {
	DirConfig *config = (DirConfig *) pcfg;
	config->autoDetect = (arg) ? DirConfig::ENABLED : DirConfig::DISABLED;
	return NULL;
}

static const char *
cmd_rails_ruby(cmd_parms *cmd, void *pcfg, const char *arg) {
	ServerConfig *config = (ServerConfig *) ap_get_module_config(
		cmd->server->module_config, &passenger_module);
	config->ruby = arg;
	return NULL;
}

static const char *
cmd_rails_env(cmd_parms *cmd, void *pcfg, const char *arg) {
	ServerConfig *config = (ServerConfig *) ap_get_module_config(
		cmd->server->module_config, &passenger_module);
	config->env = arg;
	return NULL;
}

static const char *
cmd_rails_spawn_server(cmd_parms *cmd, void *pcfg, const char *arg) {
	ServerConfig *config = (ServerConfig *) ap_get_module_config(
		cmd->server->module_config, &passenger_module);
	config->spawnServer = arg;
	return NULL;
}

static const char *
cmd_rails_max_pool_size(cmd_parms *cmd, void *pcfg, const char *arg) {
	ServerConfig *config = (ServerConfig *) ap_get_module_config(
		cmd->server->module_config, &passenger_module);
	char *end;
	long int result;
	
	result = strtol(arg, &end, 10);
	if (*end != '\0') {
		return "Invalid number specified for RailsMaxPoolSize.";
	} else if (result <= 0) {
		return "Value for RailsMaxPoolSize must be greater than 0.";
	} else {
		config->maxPoolSize = (unsigned int) result;
		config->maxPoolSizeSpecified = true;
		return NULL;
	}
}

static const char *
cmd_rails_pool_idle_time(cmd_parms *cmd, void *pcfg, const char *arg) {
	ServerConfig *config = (ServerConfig *) ap_get_module_config(
		cmd->server->module_config, &passenger_module);
	char *end;
	long int result;
	
	result = strtol(arg, &end, 10);
	if (*end != '\0') {
		return "Invalid number specified for RailsPoolIdleTime.";
	} else if (result <= 0) {
		return "Value for RailsPoolIdleTime must be greater than 0.";
	} else {
		config->poolIdleTime = (unsigned int) result;
		config->poolIdleTimeSpecified = true;
		return NULL;
	}
}

static const char *
cmd_rails_user_switching(cmd_parms *cmd, void *pcfg, int arg) {
	ServerConfig *config = (ServerConfig *) ap_get_module_config(
		cmd->server->module_config, &passenger_module);
	config->userSwitching = arg;
	config->userSwitchingSpecified = true;
	P_DEBUG("\n\n\nUser Switching!!\n\n\n");
	return NULL;
}

static const char *
cmd_user(cmd_parms *cmd, void *dummy, const char *arg) {
	ServerConfig *config = (ServerConfig *) ap_get_module_config(
		cmd->server->module_config, &passenger_module);
	config->user = arg;
	return NULL;
}


typedef const char * (*Take1Func)(); // Workaround for some weird C++-specific compiler error.

const command_rec passenger_commands[] = {
	AP_INIT_TAKE1("RailsBaseURI",
		(Take1Func) cmd_rails_base_uri,
		NULL,
		RSRC_CONF,
		"Reserve the given URI to a Rails application."),
	AP_INIT_FLAG("RailsAutoDetect",
		(Take1Func) cmd_rails_auto_detect,
		NULL,
		RSRC_CONF,
		"Whether auto-detection of Ruby on Rails applications should be enabled."),
	AP_INIT_TAKE1("RailsRuby",
		(Take1Func) cmd_rails_ruby,
		NULL,
		RSRC_CONF,
		"The Ruby interpreter to use."),
	AP_INIT_TAKE1("RailsEnv",
		(Take1Func) cmd_rails_env,
		NULL,
		RSRC_CONF,
		"The environment under which a Rails app must run."),
	AP_INIT_TAKE1("RailsSpawnServer",
		(Take1Func) cmd_rails_spawn_server,
		NULL,
		RSRC_CONF,
		"The filename of the spawn server to use."),
	AP_INIT_TAKE1("RailsMaxPoolSize",
		(Take1Func) cmd_rails_max_pool_size,
		NULL,
		RSRC_CONF,
		"The maximum number of simultaneously alive Rails application instances."),
	AP_INIT_TAKE1("RailsPoolIdleTime",
		(Take1Func) cmd_rails_pool_idle_time,
		NULL,
		RSRC_CONF,
		"The maximum number of seconds that a Rails application may be idle before it gets terminated."),
	AP_INIT_FLAG("RailsUserSwitching",
		(Take1Func) cmd_rails_user_switching,
		NULL,
		RSRC_CONF,
		"Whether to enable user switching support."),
	
	AP_INIT_TAKE1("User",
		(Take1Func) cmd_user,
		NULL,
		RSRC_CONF,
		"Effective user id for this server"),

	{ NULL }
};

} // extern "C"
