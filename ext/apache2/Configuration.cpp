#include <apr_strings.h>
#include <algorithm>
#include "Configuration.h"

using namespace std;

static apr_status_t
destroy_dir_config_struct(void *x) {
	delete (RailsConfig *) x;
	return APR_SUCCESS;
}

static RailsConfig *
create_dir_config_struct(apr_pool_t *pool) {
	RailsConfig *config = new RailsConfig();
	apr_pool_cleanup_register(pool, config, destroy_dir_config_struct, apr_pool_cleanup_null);
	return config;
}

extern "C" {

void *
passenger_config_create_dir(apr_pool_t *p, char *dirspec) {
	RailsConfig *config = create_dir_config_struct(p);
	config->base_uri = NULL;
	config->base_uri_with_slash = NULL;
	config->env = NULL;
	return config;
}

void *
passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv) {
	RailsConfig *config = create_dir_config_struct(p);
	RailsConfig *base = (RailsConfig *) basev;
	RailsConfig *add = (RailsConfig *) addv;
	
	config->base_uri = (add->base_uri == NULL) ? base->base_uri : add->base_uri;
	config->base_uri_with_slash = (add->base_uri_with_slash == NULL) ? base->base_uri_with_slash : add->base_uri_with_slash;
	config->env = (add->env == NULL) ? base->env : add->env;
	
	config->base_uris = base->base_uris;
	for (set<string>::const_iterator it(add->base_uris.begin()); it != add->base_uris.end(); it++) {
		config->base_uris.insert(*it);
	}
	
	return config;
}

void *
passenger_config_create_server(apr_pool_t *p, server_rec *s) {
	return passenger_config_create_dir(p, NULL);
}

void *
passenger_config_merge_server(apr_pool_t *p, void *basev, void *overridesv) {
	return passenger_config_merge_dir(p, basev, overridesv);
}

static const char *
cmd_rails_base_uri(cmd_parms *cmd, void *pcfg, const char *arg) {
	RailsConfig *config = (RailsConfig *) pcfg;
	config->base_uri = arg;
	if (strcmp(arg, "/") == 0) {
		config->base_uri_with_slash = "/";
	} else {
		config->base_uri_with_slash = apr_pstrcat(cmd->pool, arg, "/", NULL);
	}
	config->base_uris.insert(arg);
	return NULL;
}

typedef const char * (*Take1Func)(); // Workaround for some weird C++-specific compiler error.

const command_rec passenger_commands[] = {
	AP_INIT_TAKE1("RailsBaseURI", (Take1Func) cmd_rails_base_uri, NULL, OR_OPTIONS,
		"Reserve the given URI to a Rails application."),
	//AP_INIT_TAKE1("RailsEnv", (Take1Func) ap_set_string_slot, (void *) APR_OFFSETOF(RailsConfig, env), RSRC_CONF,
	//	"The environment under which a Rails app must run."),
	{ NULL }
};

} // extern "C"
