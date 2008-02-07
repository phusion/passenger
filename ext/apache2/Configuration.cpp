#include <apr_strings.h>
#include <algorithm>
#include "Configuration.h"

using namespace Passenger;

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
	return create_dir_config_struct(p);
}

void *
passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv) {
	DirConfig *config = create_dir_config_struct(p);
	DirConfig *base = (DirConfig *) basev;
	DirConfig *add = (DirConfig *) addv;
	
	//config->env = (add->env == NULL) ? base->env : add->env;
	
	config->base_uris = base->base_uris;
	for (set<string>::const_iterator it(add->base_uris.begin()); it != add->base_uris.end(); it++) {
		config->base_uris.insert(*it);
	}
	
	return config;
}

void *
passenger_config_create_server(apr_pool_t *p, server_rec *s) {
	ServerConfig *config = create_server_config_struct(p);
	config->ruby = NULL;
	config->env = NULL;
	return config;
}

void *
passenger_config_merge_server(apr_pool_t *p, void *basev, void *overridesv) {
	DirConfig *config = create_dir_config_struct(p);
	DirConfig *base = (DirConfig *) basev;
	DirConfig *add = (DirConfig *) addv;
	
	config->ruby = (add->ruby == NULL) ? base->ruby : add->ruby;
	config->env = (add->env == NULL) ? base->env : add->env;
	return config;
}

static const char *
cmd_rails_base_uri(cmd_parms *cmd, void *pcfg, const char *arg) {
	DirConfig *config = (DirConfig *) pcfg;
	config->base_uris.insert(arg);
	return NULL;
}

typedef const char * (*Take1Func)(); // Workaround for some weird C++-specific compiler error.

const command_rec passenger_commands[] = {
	AP_INIT_TAKE1("RailsBaseURI", (Take1Func) cmd_rails_base_uri, NULL, OR_OPTIONS,
		"Reserve the given URI to a Rails application."),
	AP_INIT_TAKE1("RailsRuby", (Take1Func) ap_set_string_slot, (void *) APR_OFFSETOF(ServerConfig, ruby), RSRC_CONF,
		"The Ruby interpreter to use."),
	AP_INIT_TAKE1("RailsEnv", (Take1Func) ap_set_string_slot, (void *) APR_OFFSETOF(ServerConfig, env), RSRC_CONF,
		"The environment under which a Rails app must run."),
	{ NULL }
};

} // extern "C"
