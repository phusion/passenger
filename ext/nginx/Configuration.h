#ifndef _PASSENGER_NGINX_CONFIGURATION_H_
#define _PASSENGER_NGINX_CONFIGURATION_H_

#include <ngx_config.h>

typedef struct {
    ngx_flag_t                     enabled;
    ngx_http_upstream_conf_t       upstream;

    ngx_str_t                      index;

    ngx_array_t                   *flushes;
    ngx_array_t                   *vars_len;
    ngx_array_t                   *vars;
    ngx_array_t                   *vars_source;
} ngx_http_scgi_loc_conf_t;

extern const ngx_command_t ngx_http_passenger_commands[];

#endif /* _PASSENGER_NGINX_CONFIGURATION_H_ */

