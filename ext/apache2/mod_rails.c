#include "ap_config.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_request.h"
#include "http_log.h"
#include "http_protocol.h"
#include "mpm_common.h"

#define MOD_RAILS_VERSION "1.0.0"

module AP_MODULE_DECLARE_DATA rails_module;

typedef enum {
	UNSET,
	ENABLED,
	DISABLED
} Threeway;

typedef struct {
	Threeway state;
} RailsConfig;


static void
log_err(const char *file, int line, request_rec *r,
                    apr_status_t status, const char *msg)
{
    char buf[256] = "";
    apr_strerror(status, buf, sizeof(buf));
    ap_log_rerror(file, line, APLOG_ERR, status, r, "rails: %s: %s", buf, msg);
}

static void
log_debug(const char *file, int line, request_rec *r, const
                      char *msg)
{
    ap_log_rerror(file, line, APLOG_ERR, APR_SUCCESS, r, msg);
}

static RailsConfig *
get_config(request_rec *r) {
	return (RailsConfig *) ap_get_module_config(r->per_dir_config, &rails_module);
}

static int
file_exists(apr_pool_t *pool, const char *filename) {
	apr_finfo_t info;
	return apr_stat(&info, filename, APR_FINFO_NORM, pool) == APR_SUCCESS;
}

static int
mod_rails_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *base_server) {
	ap_add_version_component(p, "mod_rails/" MOD_RAILS_VERSION);
	return OK;
}

/* The main request handler hook function. */
static int
mod_rails_handler(request_rec *r) {
	RailsConfig *config = get_config(r);
	if (config->state != ENABLED || file_exists(r->pool, r->filename)) {
		return DECLINED;
	}
	
	char message[1024];
	apr_snprintf(message, sizeof(message), "mod_rails %s, %s, %s", r->uri, r->filename, r->path_info);
	log_debug(APLOG_MARK, r, message);
	/* TODO */
	return DECLINED;
}

static void *
create_dir_config(apr_pool_t *p, char *dirspec) {
	RailsConfig *config = apr_palloc(p, sizeof(RailsConfig));
	config->state = UNSET;
	return config;
}

static void *
merge_dir_config(apr_pool_t *p, void *basev, void *newv) {
	RailsConfig *config = apr_palloc(p, sizeof(RailsConfig));
	RailsConfig *base_config = (RailsConfig *) basev;
	RailsConfig *new_config = (RailsConfig *) newv;
	
	#define MERGE(b, n, a) (n->a == UNSET ? b->a : n->a)
	config->state = MERGE(base_config, new_config, state);
	return config;
}

static void *
create_server_config(apr_pool_t *p, server_rec *s) {
	return create_dir_config(p, NULL);
}

static void *
merge_server_config(apr_pool_t *p, void *basev, void *overridesv) {
	return merge_dir_config(p, basev, overridesv);
}

static const char *
cmd_rails_app(cmd_parms* cmd, void* pcfg, int flag) {
	RailsConfig *config = (RailsConfig *) pcfg;
	if (flag) {
		config->state = ENABLED;
	} else {
		config->state = DISABLED;
	}
	return NULL;
}

static const command_rec mod_rails_cmds[] = {
	AP_INIT_FLAG("RailsApp", cmd_rails_app, NULL, ACCESS_CONF,
		"Set to On to indicate that the DocumentRoot is a Rails application."),
	{NULL}
};

static void mod_rails_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(mod_rails_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(mod_rails_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA rails_module = {
	STANDARD20_MODULE_STUFF,
	create_dir_config,             /* create per-dir config structs */
	merge_dir_config,              /* merge per-dir config structs */
	create_server_config,          /* create per-server config structs */
	merge_server_config,           /* merge per-server config structs */
	mod_rails_cmds,                /* table of config file commands */
	mod_rails_register_hooks,      /* register hooks */
};
