#include <httpd.h>
#include <http_config.h>
#include "Configuration.h"
#include "Hooks.h"

module AP_MODULE_DECLARE_DATA passenger_module = {
	STANDARD20_MODULE_STUFF,
	passenger_config_create_dir,        /* create per-dir config structs */
	passenger_config_merge_dir,         /* merge per-dir config structs */
	passenger_config_create_server,     /* create per-server config structs */
	passenger_config_merge_server,      /* merge per-server config structs */
	passenger_commands,                 /* table of config file commands */
	passenger_register_hooks,           /* register hooks */
};
