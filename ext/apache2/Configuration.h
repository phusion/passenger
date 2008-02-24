#ifndef _PASSENGER_CONFIGURATION_H_
#define _PASSENGER_CONFIGURATION_H_

#include <apr_pools.h>
#include <httpd.h>
#include <http_config.h>

/**
 * @defgroup Configuration Apache module configuration
 * @ingroup Core
 * @{
 */

/** Module version number. */
#define PASSENGER_VERSION "0.9.1"

#ifdef __cplusplus
	#include <set>
	#include <string>

	namespace Passenger {
	
		using namespace std;

		/**
		* Per-directory configuration information.
		*/
		struct DirConfig {
			std::set<std::string> base_uris;
			enum { ENABLED, DISABLED, UNSET } autoDetect;
		};
		
		struct ServerConfig {
			const char *ruby;
			
			/** The environment (i.e. value for RAILS_ENV) under which the Rails application should operate. */
			const char *env;
			
			const char *spawnServer;
		};
	}

	extern "C" {
#endif

/** Configuration hook for per-directory configuration structure creation. */
void *passenger_config_create_dir(apr_pool_t *p, char *dirspec);

/** Configuration hook for per-directory configuration structure merging. */
void *passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv);

/** Configuration hook for per-server configuration structure creation. */
void *passenger_config_create_server(apr_pool_t *p, server_rec *s);

/** Configuration hook for per-server configuration structure merging. */
void *passenger_config_merge_server(apr_pool_t *p, void *basev, void *overridesv);

void passenger_config_merge_all_servers(apr_pool_t *pool, server_rec *main_server);

/** Apache module commands array. */
extern const command_rec passenger_commands[];

#ifdef __cplusplus
	}
#endif

/**
 * @}
 */

#endif /* _PASSENGER_CONFIGURATION_H_ */
