#ifndef _PASSENGER_CONFIGURATION_H_
#define _PASSENGER_CONFIGURATION_H_

#include <apr_pools.h>
#include <httpd.h>
#include <http_config.h>

#define PASSENGER_VERSION "1.0.0"

struct RailsConfig {
	const char *base_uri;
	char *base_uri_with_slash;
	const char *env;
};

#ifdef __cplusplus
extern "C" {
#endif

void *passenger_config_create_dir(apr_pool_t *p, char *dirspec);
void *passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv);
void *passenger_config_merge_server(apr_pool_t *p, void *basev, void *overridesv);
void *passenger_config_create_server(apr_pool_t *p, server_rec *s);
void *passenger_config_merge_server(apr_pool_t *p, void *basev, void *overridesv);
extern const command_rec passenger_commands[];

#ifdef __cplusplus
}
#endif

#endif /* _PASSENGER_CONFIGURATION_H_ */
