/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (c) 2010-2016 Phusion Holding B.V.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PASSENGER_NGINX_CONFIGURATION_H_
#define _PASSENGER_NGINX_CONFIGURATION_H_

#include <ngx_config.h>
#include <ngx_http.h>

#include "LocationConfig.h"

typedef struct {
    ngx_str_t    root_dir;
    ngx_array_t *ctl;
    ngx_str_t    default_ruby;
    ngx_int_t    log_level;
    ngx_str_t    log_file;
    ngx_str_t    file_descriptor_log_file;
    ngx_uint_t   socket_backlog;
    ngx_str_t    data_buffer_dir;
    ngx_str_t    instance_registry_dir;
    ngx_flag_t   disable_security_update_check;
    ngx_str_t    security_update_check_proxy;
    ngx_flag_t   abort_on_startup_error;
    ngx_uint_t   max_pool_size;
    ngx_uint_t   pool_idle_time;
    ngx_uint_t   response_buffer_high_watermark;
    ngx_uint_t   stat_throttle_rate;
    ngx_uint_t   core_file_descriptor_ulimit;
    ngx_flag_t   turbocaching;
    ngx_flag_t   show_version_in_header;
    ngx_flag_t   user_switching;
    ngx_str_t    default_user;
    ngx_str_t    default_group;
    ngx_str_t    analytics_log_user;
    ngx_str_t    analytics_log_group;
    ngx_int_t    union_station_support;
    ngx_str_t    union_station_gateway_address;
    ngx_uint_t   union_station_gateway_port;
    ngx_str_t    union_station_gateway_cert;
    ngx_str_t    union_station_proxy_address;
    ngx_array_t *prestart_uris;
} passenger_main_conf_t;

extern const ngx_command_t   passenger_commands[];
extern passenger_main_conf_t passenger_main_conf;

void *passenger_create_main_conf(ngx_conf_t *cf);
char *passenger_init_main_conf(ngx_conf_t *cf, void *conf_pointer);
void *passenger_create_loc_conf(ngx_conf_t *cf);
char *passenger_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
ngx_int_t passenger_postprocess_config(ngx_conf_t *cf);

#endif /* _PASSENGER_NGINX_CONFIGURATION_H_ */

