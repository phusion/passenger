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

module AP_MODULE_DECLARE_DATA rails_module;
#define MOD_RAILS_VERSION "1.0.0"
#define INSIDE_MOD_RAILS
#include "types.h"
#include "utils.c"
#include "config.c"
#include "hooks.c"

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
