#include <apr_strings.h>
#include <algorithm>
#include "Configuration.h"

using namespace std;

static apr_status_t
destroy_dir_config_struct(void *x) {
	delete (RailsDirConfig *) x;
	return APR_SUCCESS;
}

static RailsDirConfig *
create_dir_config_struct(apr_pool_t *pool) {
	RailsDirConfig *config = new RailsDirConfig();
	apr_pool_cleanup_register(pool, config, destroy_dir_config_struct, apr_pool_cleanup_null);
	return config;
}

extern "C" {

void *
passenger_config_create_dir(apr_pool_t *p, char *dirspec) {
	RailsDirConfig *config = create_dir_config_struct(p);
	config->env = NULL;
	return config;
}

void *
passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv) {
	RailsDirConfig *config = create_dir_config_struct(p);
	RailsDirConfig *base = (RailsDirConfig *) basev;
	RailsDirConfig *add = (RailsDirConfig *) addv;
	
	config->env = (add->env == NULL) ? base->env : add->env;
	
	config->base_uris = base->base_uris;
	for (set<string>::const_iterator it(add->base_uris.begin()); it != add->base_uris.end(); it++) {
		config->base_uris.insert(*it);
	}
	
	return config;
}

void *
passenger_config_create_server(apr_pool_t *p, server_rec *s) {
	//return passenger_config_create_dir(p, NULL);
	return NULL;
}

void *
passenger_config_merge_server(apr_pool_t *p, void *basev, void *overridesv) {
	//return passenger_config_merge_dir(p, basev, overridesv);
	return NULL;
}

static const char *
cmd_rails_base_uri(cmd_parms *cmd, void *pcfg, const char *arg) {
	RailsDirConfig *config = (RailsDirConfig *) pcfg;
	config->base_uris.insert(arg);
	return NULL;
}

typedef const char * (*Take1Func)(); // Workaround for some weird C++-specific compiler error.

const command_rec passenger_commands[] = {
	AP_INIT_TAKE1("RailsBaseURI", (Take1Func) cmd_rails_base_uri, NULL, OR_OPTIONS,
		"Reserve the given URI to a Rails application."),
	//AP_INIT_TAKE1("RailsEnv", (Take1Func) ap_set_string_slot, (void *) APR_OFFSETOF(RailsDirConfig, env), RSRC_CONF,
	//	"The environment under which a Rails app must run."),
	{ NULL }
};

} // extern "C"
