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

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <assert.h>

#include "ngx_http_passenger_module.h"
#include "Configuration.h"
#include "ContentHandler.h"
#include "cxx_supportlib/Constants.h"
#include "cxx_supportlib/UnionStationFilterSupport.h"
#include "cxx_supportlib/vendor-modified/modp_b64.h"


static ngx_str_t headers_to_hide[] = {
    /* NOTE: Do not hide the "Status" header; some broken HTTP clients
     * expect this header. http://code.google.com/p/phusion-passenger/issues/detail?id=177
     */
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_null_string
};

passenger_main_conf_t passenger_main_conf;

typedef struct postprocess_ctx_s postprocess_ctx_t;

static ngx_path_init_t  ngx_http_proxy_temp_path = {
    ngx_string(NGX_HTTP_PROXY_TEMP_PATH), { 1, 2, 0 }
};

static ngx_int_t postprocess_location_conf(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_core_loc_conf_t *location_conf,
    int is_toplevel,
    postprocess_ctx_t *ctx);
static ngx_int_t traverse_location_confs_nested_in_server_conf(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_core_loc_conf_t *location_conf,
    postprocess_ctx_t *ctx);
static ngx_int_t traverse_static_location_tree(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_location_tree_node_t *node,
    postprocess_ctx_t *ctx);
#ifdef NGX_PCRE
static ngx_int_t traverse_regex_locations(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_core_loc_conf_t **regex_locations,
    postprocess_ctx_t *ctx);
#endif
static ngx_int_t merge_headers(ngx_conf_t *cf, passenger_loc_conf_t *conf,
    passenger_loc_conf_t *prev);
static ngx_int_t merge_string_array(ngx_conf_t *cf, ngx_array_t **prev,
    ngx_array_t **conf);
static ngx_int_t merge_string_keyval_table(ngx_conf_t *cf, ngx_array_t **prev,
    ngx_array_t **conf);


void *
passenger_create_main_conf(ngx_conf_t *cf)
{
    passenger_main_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(passenger_main_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->ctl = ngx_array_create(cf->pool, 1, sizeof(ngx_keyval_t));
    if (conf->ctl == NULL) {
        return NGX_CONF_ERROR;
    }
    conf->default_ruby.data = NULL;
    conf->default_ruby.len = 0;
    conf->log_level = (ngx_int_t) NGX_CONF_UNSET;
    conf->log_file.data = NULL;
    conf->log_file.len = 0;
    conf->socket_backlog = NGX_CONF_UNSET_UINT;
    conf->file_descriptor_log_file.data = NULL;
    conf->file_descriptor_log_file.len = 0;
    conf->data_buffer_dir.data = NULL;
    conf->data_buffer_dir.len = 0;
    conf->instance_registry_dir.data = NULL;
    conf->instance_registry_dir.len = 0;
    conf->disable_security_update_check = NGX_CONF_UNSET;
    conf->security_update_check_proxy.data = NULL;
    conf->security_update_check_proxy.len = 0;
    conf->abort_on_startup_error = NGX_CONF_UNSET;
    conf->max_pool_size = NGX_CONF_UNSET_UINT;
    conf->pool_idle_time = NGX_CONF_UNSET_UINT;
    conf->response_buffer_high_watermark = NGX_CONF_UNSET_UINT;
    conf->stat_throttle_rate = NGX_CONF_UNSET_UINT;
    conf->core_file_descriptor_ulimit = NGX_CONF_UNSET_UINT;
    conf->user_switching = NGX_CONF_UNSET;
    conf->show_version_in_header = NGX_CONF_UNSET;
    conf->turbocaching = NGX_CONF_UNSET;
    conf->default_user.data = NULL;
    conf->default_user.len  = 0;
    conf->default_group.data = NULL;
    conf->default_group.len  = 0;
    conf->analytics_log_user.data = NULL;
    conf->analytics_log_user.len  = 0;
    conf->analytics_log_group.data = NULL;
    conf->analytics_log_group.len  = 0;
    conf->union_station_support = 0;
    conf->union_station_gateway_address.data = NULL;
    conf->union_station_gateway_address.len = 0;
    conf->union_station_gateway_port = (ngx_uint_t) NGX_CONF_UNSET;
    conf->union_station_gateway_cert.data = NULL;
    conf->union_station_gateway_cert.len = 0;
    conf->union_station_proxy_address.data = NULL;
    conf->union_station_proxy_address.len = 0;

    conf->prestart_uris = ngx_array_create(cf->pool, 1, sizeof(ngx_str_t));
    if (conf->prestart_uris == NULL) {
        return NGX_CONF_ERROR;
    }

    return conf;
}

char *
passenger_init_main_conf(ngx_conf_t *cf, void *conf_pointer)
{
    passenger_main_conf_t *conf;
    struct passwd         *user_entry;
    struct group          *group_entry;
    char buf[128];

    conf = &passenger_main_conf;
    *conf = *((passenger_main_conf_t *) conf_pointer);

    if (conf->default_ruby.len == 0) {
        conf->default_ruby.data = (u_char *) DEFAULT_RUBY;
        conf->default_ruby.len = strlen(DEFAULT_RUBY);
    }

    if (conf->log_level == (ngx_int_t) NGX_CONF_UNSET) {
        conf->log_level = DEFAULT_LOG_LEVEL;
    }

    if (conf->log_file.len == 0) {
        conf->log_file.data = (u_char *) "";
    }

    if (conf->file_descriptor_log_file.len == 0) {
        conf->file_descriptor_log_file.data = (u_char *) "";
    }

    if (conf->socket_backlog == NGX_CONF_UNSET_UINT) {
        conf->socket_backlog = DEFAULT_SOCKET_BACKLOG;
    }

    if (conf->data_buffer_dir.len == 0) {
        conf->data_buffer_dir.data = (u_char *) "";
    }

    if (conf->instance_registry_dir.len == 0) {
        conf->instance_registry_dir.data = (u_char *) "";
    }

    if (conf->disable_security_update_check == NGX_CONF_UNSET) {
		conf->disable_security_update_check = 0;
	}

    if (conf->security_update_check_proxy.len == 0) {
		conf->security_update_check_proxy.data = (u_char *) "";
	}

    if (conf->abort_on_startup_error == NGX_CONF_UNSET) {
        conf->abort_on_startup_error = 0;
    }

    if (conf->max_pool_size == NGX_CONF_UNSET_UINT) {
        conf->max_pool_size = DEFAULT_MAX_POOL_SIZE;
    }

    if (conf->pool_idle_time == NGX_CONF_UNSET_UINT) {
        conf->pool_idle_time = DEFAULT_POOL_IDLE_TIME;
    }

    if (conf->response_buffer_high_watermark == NGX_CONF_UNSET_UINT) {
        conf->response_buffer_high_watermark = DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK;
    }

    if (conf->stat_throttle_rate == NGX_CONF_UNSET_UINT) {
        conf->stat_throttle_rate = DEFAULT_STAT_THROTTLE_RATE;
    }

    if (conf->user_switching == NGX_CONF_UNSET) {
        conf->user_switching = 1;
    }

    if (conf->show_version_in_header == NGX_CONF_UNSET) {
        conf->show_version_in_header = 1;
    }

    if (conf->turbocaching == NGX_CONF_UNSET) {
        conf->turbocaching = 1;
    }

    if (conf->default_user.len == 0) {
        conf->default_user.len  = sizeof(DEFAULT_WEB_APP_USER) - 1;
        conf->default_user.data = (u_char *) DEFAULT_WEB_APP_USER;
    }
    if (conf->default_user.len > sizeof(buf) - 1) {
        return "Value for 'default_user' is too long.";
    }
    memcpy(buf, conf->default_user.data, conf->default_user.len);
    buf[conf->default_user.len] = '\0';
    user_entry = getpwnam(buf);
    if (user_entry == NULL) {
        return "The user specified by the 'default_user' option does not exist.";
    }

    if (conf->default_group.len > 0) {
        if (conf->default_group.len > sizeof(buf) - 1) {
            return "Value for 'default_group' is too long.";
        }
        memcpy(buf, conf->default_group.data, conf->default_group.len);
        buf[conf->default_group.len] = '\0';
        group_entry = getgrnam(buf);
        if (group_entry == NULL) {
            return "The group specified by the 'default_group' option does not exist.";
        }
    }

    if (conf->analytics_log_user.len == 0) {
        conf->analytics_log_user.len  = sizeof(DEFAULT_ANALYTICS_LOG_USER) - 1;
        conf->analytics_log_user.data = (u_char *) DEFAULT_ANALYTICS_LOG_USER;
    }

    if (conf->analytics_log_group.len == 0) {
        conf->analytics_log_group.len  = sizeof(DEFAULT_ANALYTICS_LOG_GROUP) - 1;
        conf->analytics_log_group.data = (u_char *) DEFAULT_ANALYTICS_LOG_GROUP;
    }

    if (conf->union_station_gateway_address.len == 0) {
        conf->union_station_gateway_address.len = sizeof(DEFAULT_UNION_STATION_GATEWAY_ADDRESS) - 1;
        conf->union_station_gateway_address.data = (u_char *) DEFAULT_UNION_STATION_GATEWAY_ADDRESS;
    }

    if (conf->union_station_gateway_port == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->union_station_gateway_port = DEFAULT_UNION_STATION_GATEWAY_PORT;
    }

    if (conf->union_station_gateway_cert.len == 0) {
        conf->union_station_gateway_cert.data = (u_char *) "";
    }

    if (conf->union_station_proxy_address.len == 0) {
        conf->union_station_proxy_address.data = (u_char *) "";
    }

    return NGX_CONF_OK;
}

#include "CreateLocationConfig.c"

void *
passenger_create_loc_conf(ngx_conf_t *cf)
{
    passenger_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(passenger_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream_config.bufs.num = 0;
     *     conf->upstream_config.next_upstream = 0;
     *     conf->upstream_config.temp_path = NULL;
     *     conf->upstream_config.hide_headers_hash = { NULL, 0 };
     *     conf->upstream_config.hide_headers = NULL;
     *     conf->upstream_config.pass_headers = NULL;
     *     conf->upstream_config.uri = { 0, NULL };
     *     conf->upstream_config.location = NULL;
     *     conf->upstream_config.store_lengths = NULL;
     *     conf->upstream_config.store_values = NULL;
     */

    generated_set_conf_part(conf);

    /******************************/
    /******************************/

    conf->upstream_config.pass_headers = NGX_CONF_UNSET_PTR;
    conf->upstream_config.hide_headers = NGX_CONF_UNSET_PTR;

    conf->upstream_config.store = NGX_CONF_UNSET;
    conf->upstream_config.store_access = NGX_CONF_UNSET_UINT;
    #if NGINX_VERSION_NUM >= 1007005
        conf->upstream_config.next_upstream_tries = NGX_CONF_UNSET_UINT;
    #endif
    conf->upstream_config.buffering = NGX_CONF_UNSET;
    conf->upstream_config.ignore_client_abort = NGX_CONF_UNSET;
    #if NGINX_VERSION_NUM >= 1007007
        conf->upstream_config.force_ranges = NGX_CONF_UNSET;
    #endif

    conf->upstream_config.local = NGX_CONF_UNSET_PTR;

    conf->upstream_config.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream_config.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream_config.read_timeout = NGX_CONF_UNSET_MSEC;
    #if NGINX_VERSION_NUM >= 1007005
        conf->upstream_config.next_upstream_timeout = NGX_CONF_UNSET_MSEC;
    #endif

    conf->upstream_config.send_lowat = NGX_CONF_UNSET_SIZE;
    conf->upstream_config.buffer_size = NGX_CONF_UNSET_SIZE;
    #if NGINX_VERSION_NUM >= 1007007
        conf->upstream_config.limit_rate = NGX_CONF_UNSET_SIZE;
    #endif

    conf->upstream_config.busy_buffers_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream_config.max_temp_file_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream_config.temp_file_write_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream_config.pass_request_headers = NGX_CONF_UNSET;
    conf->upstream_config.pass_request_body = NGX_CONF_UNSET;

#if (NGX_HTTP_CACHE)
    #if NGINX_VERSION_NUM >= 1007009
        conf->upstream_config.cache = NGX_CONF_UNSET;
    #else
        conf->upstream_config.cache = NGX_CONF_UNSET_PTR;
    #endif
    conf->upstream_config.cache_min_uses = NGX_CONF_UNSET_UINT;
    conf->upstream_config.cache_bypass = NGX_CONF_UNSET_PTR;
    conf->upstream_config.no_cache = NGX_CONF_UNSET_PTR;
    conf->upstream_config.cache_valid = NGX_CONF_UNSET_PTR;
    conf->upstream_config.cache_lock = NGX_CONF_UNSET;
    conf->upstream_config.cache_lock_timeout = NGX_CONF_UNSET_MSEC;
    #if NGINX_VERSION_NUM >= 1007008
        conf->upstream_config.cache_lock_age = NGX_CONF_UNSET_MSEC;
    #endif
    #if NGINX_VERSION_NUM >= 1006000
        conf->upstream_config.cache_revalidate = NGX_CONF_UNSET;
    #endif
#endif

    conf->upstream_config.intercept_errors = NGX_CONF_UNSET;

    conf->upstream_config.cyclic_temp_file = 0;
    conf->upstream_config.change_buffering = 1;

    ngx_str_set(&conf->upstream_config.module, "passenger");

    conf->options_cache.data  = NULL;
    conf->options_cache.len   = 0;
    conf->env_vars_cache.data = NULL;
    conf->env_vars_cache.len  = 0;

    return conf;
}

#include "CacheLocationConfig.c"

static ngx_int_t
cache_loc_conf_options(ngx_conf_t *cf, passenger_loc_conf_t *conf)
{
    ngx_uint_t     i;
    ngx_keyval_t  *env_vars;
    size_t         unencoded_len;
    u_char        *unencoded_buf;

    if (generated_cache_location_part(cf, conf) == 0) {
        return NGX_ERROR;
    }

    if (conf->env_vars != NULL) {
        size_t len = 0;
        u_char *buf;
        u_char *pos;

        /* Cache env vars data as base64-serialized string.
         * First, calculate the length of the unencoded data.
         */

        unencoded_len = 0;
        env_vars = (ngx_keyval_t *) conf->env_vars->elts;

        for (i = 0; i < conf->env_vars->nelts; i++) {
            unencoded_len += env_vars[i].key.len + 1 + env_vars[i].value.len + 1;
        }

        /* Create the unecoded data. */

        unencoded_buf = pos = (u_char *) malloc(unencoded_len);
        if (unencoded_buf == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot allocate buffer of %z bytes for environment variables data",
                               unencoded_len);
            return NGX_ERROR;
        }

        for (i = 0; i < conf->env_vars->nelts; i++) {
            pos = ngx_copy(pos, env_vars[i].key.data, env_vars[i].key.len);
            *pos = '\0';
            pos++;

            pos = ngx_copy(pos, env_vars[i].value.data, env_vars[i].value.len);
            *pos = '\0';
            pos++;
        }

        assert((size_t) (pos - unencoded_buf) == unencoded_len);

        /* Create base64-serialized string. */

        buf = ngx_palloc(cf->pool, modp_b64_encode_len(unencoded_len));
        if (buf == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot allocate buffer of %z bytes for base64 encoding",
                               modp_b64_encode_len(unencoded_len));
            return NGX_ERROR;
        }
        len = modp_b64_encode((char *) buf, (const char *) unencoded_buf, unencoded_len);
        if (len == (size_t) -1) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "error during base64 encoding");
            free(unencoded_buf);
            return NGX_ERROR;
        }

        conf->env_vars_cache.data = buf;
        conf->env_vars_cache.len = len;
        free(unencoded_buf);
    }

    return NGX_OK;
}

#include "MergeLocationConfig.c"

char *
passenger_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    passenger_loc_conf_t         *prev = parent;
    passenger_loc_conf_t         *conf = child;
    ngx_http_core_loc_conf_t     *clcf;

    size_t                        size;
    ngx_hash_init_t               hash;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    if (generated_merge_part(conf, prev, cf) == 0) {
        return NGX_CONF_ERROR;
    }

    if (prev->options_cache.data == NULL) {
        if (cache_loc_conf_options(cf, prev) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "cannot create " PROGRAM_NAME " configuration cache");
            return NGX_CONF_ERROR;
        }
    }

    /******************************/
    /******************************/

    #if (NGX_HTTP_CACHE) && NGINX_VERSION_NUM >= 1007009
        if (conf->upstream_config.store > 0) {
            conf->upstream_config.cache = 0;
        }
        if (conf->upstream_config.cache > 0) {
            conf->upstream_config.store = 0;
        }
    #endif

    #if NGINX_VERSION_NUM >= 1007009
        if (conf->upstream_config.store == NGX_CONF_UNSET) {
            ngx_conf_merge_value(conf->upstream_config.store,
                                      prev->upstream_config.store, 0);

            conf->upstream_config.store_lengths = prev->upstream_config.store_lengths;
            conf->upstream_config.store_values = prev->upstream_config.store_values;
        }
    #else
        if (conf->upstream_config.store != 0) {
            ngx_conf_merge_value(conf->upstream_config.store,
                                      prev->upstream_config.store, 0);

            if (conf->upstream_config.store_lengths == NULL) {
                conf->upstream_config.store_lengths = prev->upstream_config.store_lengths;
                conf->upstream_config.store_values = prev->upstream_config.store_values;
            }
        }
    #endif

    ngx_conf_merge_uint_value(conf->upstream_config.store_access,
                              prev->upstream_config.store_access, 0600);

    #if NGINX_VERSION_NUM >= 1007005
        ngx_conf_merge_uint_value(conf->upstream_config.next_upstream_tries,
                                  prev->upstream_config.next_upstream_tries, 0);
    #endif

    ngx_conf_merge_value(conf->upstream_config.buffering,
                         prev->upstream_config.buffering, 0);

    ngx_conf_merge_value(conf->upstream_config.ignore_client_abort,
                         prev->upstream_config.ignore_client_abort, 0);

    #if NGINX_VERSION_NUM >= 1007007
        ngx_conf_merge_value(conf->upstream_config.force_ranges,
                             prev->upstream_config.force_ranges, 0);
    #endif

    ngx_conf_merge_ptr_value(conf->upstream_config.local,
                             prev->upstream_config.local, NULL);

    ngx_conf_merge_msec_value(conf->upstream_config.connect_timeout,
                              prev->upstream_config.connect_timeout, 12000000);

    ngx_conf_merge_msec_value(conf->upstream_config.send_timeout,
                              prev->upstream_config.send_timeout, 12000000);

    ngx_conf_merge_msec_value(conf->upstream_config.read_timeout,
                              prev->upstream_config.read_timeout, 12000000);

    #if NGINX_VERSION_NUM >= 1007005
        ngx_conf_merge_msec_value(conf->upstream_config.next_upstream_timeout,
                                  prev->upstream_config.next_upstream_timeout, 0);
    #endif

    ngx_conf_merge_size_value(conf->upstream_config.send_lowat,
                              prev->upstream_config.send_lowat, 0);

    ngx_conf_merge_size_value(conf->upstream_config.buffer_size,
                              prev->upstream_config.buffer_size,
                              16 * 1024);

    #if NGINX_VERSION_NUM >= 1007007
        ngx_conf_merge_size_value(conf->upstream_config.limit_rate,
                                  prev->upstream_config.limit_rate, 0);
    #endif


    ngx_conf_merge_bufs_value(conf->upstream_config.bufs, prev->upstream_config.bufs,
                              8, 16 * 1024);

    if (conf->upstream_config.bufs.num < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "there must be at least 2 \"passenger_buffers\"");
        return NGX_CONF_ERROR;
    }


    size = conf->upstream_config.buffer_size;
    if (size < conf->upstream_config.bufs.size) {
        size = conf->upstream_config.bufs.size;
    }


    ngx_conf_merge_size_value(conf->upstream_config.busy_buffers_size_conf,
                              prev->upstream_config.busy_buffers_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream_config.busy_buffers_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream_config.busy_buffers_size = 2 * size;
    } else {
        conf->upstream_config.busy_buffers_size =
                                         conf->upstream_config.busy_buffers_size_conf;
    }

    if (conf->upstream_config.busy_buffers_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"passenger_busy_buffers_size\" must be equal to or greater "
             "than the maximum of the value of \"passenger_buffer_size\" and "
             "one of the \"passenger_buffers\"");

        return NGX_CONF_ERROR;
    }

    if (conf->upstream_config.busy_buffers_size
        > (conf->upstream_config.bufs.num - 1) * conf->upstream_config.bufs.size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"passenger_busy_buffers_size\" must be less than "
             "the size of all \"passenger_buffers\" minus one buffer");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream_config.temp_file_write_size_conf,
                              prev->upstream_config.temp_file_write_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream_config.temp_file_write_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream_config.temp_file_write_size = 2 * size;
    } else {
        conf->upstream_config.temp_file_write_size =
                                      conf->upstream_config.temp_file_write_size_conf;
    }

    if (conf->upstream_config.temp_file_write_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"passenger_temp_file_write_size\" must be equal to or greater than "
             "the maximum of the value of \"passenger_buffer_size\" and "
             "one of the \"passenger_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream_config.max_temp_file_size_conf,
                              prev->upstream_config.max_temp_file_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream_config.max_temp_file_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream_config.max_temp_file_size = 1024 * 1024 * 1024;
    } else {
        conf->upstream_config.max_temp_file_size =
                                        conf->upstream_config.max_temp_file_size_conf;
    }

    if (conf->upstream_config.max_temp_file_size != 0
        && conf->upstream_config.max_temp_file_size < size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"passenger_max_temp_file_size\" must be equal to zero to disable "
             "temporary files usage or must be equal to or greater than "
             "the maximum of the value of \"passenger_buffer_size\" and "
             "one of the \"passenger_buffers\"");

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_bitmask_value(conf->upstream_config.ignore_headers,
                                 prev->upstream_config.ignore_headers,
                                 NGX_CONF_BITMASK_SET);

    ngx_conf_merge_bitmask_value(conf->upstream_config.next_upstream,
                              prev->upstream_config.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream_config.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream_config.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    ngx_conf_merge_path_value(cf,
                              &conf->upstream_config.temp_path,
                              prev->upstream_config.temp_path,
                              &ngx_http_proxy_temp_path);

#if (NGX_HTTP_CACHE)

    #if NGINX_VERSION_NUM >= 1007009
        if (conf->upstream_config.cache == NGX_CONF_UNSET) {
           ngx_conf_merge_value(conf->upstream_config.cache,
                                prev->upstream_config.cache, 0);

           conf->upstream_config.cache_zone = prev->upstream_config.cache_zone;
           conf->upstream_config.cache_value = prev->upstream_config.cache_value;
        }

        if (conf->upstream_config.cache_zone && conf->upstream_config.cache_zone->data == NULL) {
            ngx_shm_zone_t  *shm_zone;

            shm_zone = conf->upstream_config.cache_zone;

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"scgi_cache\" zone \"%V\" is unknown",
                               &shm_zone->shm.name);

            return NGX_CONF_ERROR;
        }
    #else
        ngx_conf_merge_ptr_value(conf->upstream_config.cache,
                                 prev->upstream_config.cache, NULL);

        if (conf->upstream_config.cache && conf->upstream_config.cache->data == NULL) {
            ngx_shm_zone_t  *shm_zone;

            shm_zone = conf->upstream_config.cache;

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"scgi_cache\" zone \"%V\" is unknown",
                               &shm_zone->shm.name);

            return NGX_CONF_ERROR;
        }
    #endif

    ngx_conf_merge_uint_value(conf->upstream_config.cache_min_uses,
                              prev->upstream_config.cache_min_uses, 1);

    ngx_conf_merge_bitmask_value(conf->upstream_config.cache_use_stale,
                              prev->upstream_config.cache_use_stale,
                              (NGX_CONF_BITMASK_SET
                               | NGX_HTTP_UPSTREAM_FT_OFF));

    if (conf->upstream_config.cache_use_stale & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream_config.cache_use_stale = NGX_CONF_BITMASK_SET
                                                | NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream_config.cache_use_stale & NGX_HTTP_UPSTREAM_FT_ERROR) {
        conf->upstream_config.cache_use_stale |= NGX_HTTP_UPSTREAM_FT_NOLIVE;
    }

    if (conf->upstream_config.cache_methods == 0) {
        conf->upstream_config.cache_methods = prev->upstream_config.cache_methods;
    }

    conf->upstream_config.cache_methods |= NGX_HTTP_GET | NGX_HTTP_HEAD;

    ngx_conf_merge_ptr_value(conf->upstream_config.cache_bypass,
                             prev->upstream_config.cache_bypass, NULL);

    ngx_conf_merge_ptr_value(conf->upstream_config.no_cache,
                             prev->upstream_config.no_cache, NULL);

    ngx_conf_merge_ptr_value(conf->upstream_config.cache_valid,
                             prev->upstream_config.cache_valid, NULL);

    if (conf->cache_key.value.data == NULL) {
        conf->cache_key = prev->cache_key;
    }

    ngx_conf_merge_value(conf->upstream_config.cache_lock,
                         prev->upstream_config.cache_lock, 0);

    ngx_conf_merge_msec_value(conf->upstream_config.cache_lock_timeout,
                              prev->upstream_config.cache_lock_timeout, 5000);

    ngx_conf_merge_value(conf->upstream_config.cache_revalidate,
                         prev->upstream_config.cache_revalidate, 0);

    #if NGINX_VERSION_NUM >= 1007008
        ngx_conf_merge_msec_value(conf->upstream_config.cache_lock_age,
                                  prev->upstream_config.cache_lock_age, 5000);
    #endif

    #if NGINX_VERSION_NUM >= 1006000
        ngx_conf_merge_value(conf->upstream_config.cache_revalidate,
                             prev->upstream_config.cache_revalidate, 0);
    #endif

#endif

    ngx_conf_merge_value(conf->upstream_config.pass_request_headers,
                         prev->upstream_config.pass_request_headers, 1);
    ngx_conf_merge_value(conf->upstream_config.pass_request_body,
                         prev->upstream_config.pass_request_body, 1);

    ngx_conf_merge_value(conf->upstream_config.intercept_errors,
                         prev->upstream_config.intercept_errors, 0);


    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "passenger_hide_headers_hash";

    if (ngx_http_upstream_hide_headers_hash(cf, &conf->upstream_config,
            &prev->upstream_config, headers_to_hide, &hash)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (conf->upstream_config.upstream == NULL) {
        conf->upstream_config.upstream = prev->upstream_config.upstream;
    }

    if (conf->enabled == 1 /* and not NGX_CONF_UNSET */
     && passenger_main_conf.root_dir.len != 0
     && clcf->handler == NULL /* no handler set by other modules */)
    {
        clcf->handler = passenger_content_handler;
    }

    conf->headers_hash_bucket_size = ngx_align(conf->headers_hash_bucket_size,
                                               ngx_cacheline_size);
    hash.max_size = conf->headers_hash_max_size;
    hash.bucket_size = conf->headers_hash_bucket_size;
    hash.name = "passenger_headers_hash";

    if (merge_headers(cf, conf, prev) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (cache_loc_conf_options(cf, conf) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "cannot create " PROGRAM_NAME " configuration cache");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static ngx_int_t
merge_headers(ngx_conf_t *cf, passenger_loc_conf_t *conf, passenger_loc_conf_t *prev)
{
    u_char                       *p;
    size_t                        size;
    uintptr_t                    *code;
    ngx_uint_t                    i;
    ngx_array_t                   headers_names, headers_merged;
    ngx_keyval_t                 *src, *s;
    ngx_hash_key_t               *hk;
    ngx_hash_init_t               hash;
    ngx_http_script_compile_t     sc;
    ngx_http_script_copy_code_t  *copy;

    if (conf->headers_source == NULL) {
        conf->flushes = prev->flushes;
        conf->headers_set_len = prev->headers_set_len;
        conf->headers_set = prev->headers_set;
        conf->headers_set_hash = prev->headers_set_hash;
        conf->headers_source = prev->headers_source;
    }

    if (conf->headers_set_hash.buckets
#if (NGX_HTTP_CACHE)
    #if NGINX_VERSION_NUM >= 1007009
        && ((conf->upstream_config.cache == NGX_CONF_UNSET) == (prev->upstream_config.cache == NGX_CONF_UNSET))
    #else
        && ((conf->upstream_config.cache == NGX_CONF_UNSET_PTR) == (prev->upstream_config.cache == NGX_CONF_UNSET_PTR))
    #endif
#endif
       )
    {
        return NGX_OK;
    }


    if (ngx_array_init(&headers_names, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&headers_merged, cf->temp_pool, 4, sizeof(ngx_keyval_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (conf->headers_source == NULL) {
        conf->headers_source = ngx_array_create(cf->pool, 4,
                                                sizeof(ngx_keyval_t));
        if (conf->headers_source == NULL) {
            return NGX_ERROR;
        }
    }

    conf->headers_set_len = ngx_array_create(cf->pool, 64, 1);
    if (conf->headers_set_len == NULL) {
        return NGX_ERROR;
    }

    conf->headers_set = ngx_array_create(cf->pool, 512, 1);
    if (conf->headers_set == NULL) {
        return NGX_ERROR;
    }


    src = conf->headers_source->elts;
    for (i = 0; i < conf->headers_source->nelts; i++) {

        s = ngx_array_push(&headers_merged);
        if (s == NULL) {
            return NGX_ERROR;
        }

        *s = src[i];
    }


    src = headers_merged.elts;
    for (i = 0; i < headers_merged.nelts; i++) {

        hk = ngx_array_push(&headers_names);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = src[i].key;
        hk->key_hash = ngx_hash_key_lc(src[i].key.data, src[i].key.len);
        hk->value = (void *) 1;

        if (src[i].value.len == 0) {
            continue;
        }

        if (ngx_http_script_variables_count(&src[i].value) == 0) {
            copy = ngx_array_push_n(conf->headers_set_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = src[i].key.len + sizeof(": ") - 1
                        + src[i].value.len + sizeof(CRLF) - 1;


            size = (sizeof(ngx_http_script_copy_code_t)
                       + src[i].key.len + sizeof(": ") - 1
                       + src[i].value.len + sizeof(CRLF) - 1
                       + sizeof(uintptr_t) - 1)
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->headers_set, size);
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = src[i].key.len + sizeof(": ") - 1
                        + src[i].value.len + sizeof(CRLF) - 1;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);

            p = ngx_cpymem(p, src[i].key.data, src[i].key.len);
            *p++ = ':'; *p++ = ' ';
            p = ngx_cpymem(p, src[i].value.data, src[i].value.len);
            *p++ = CR; *p = LF;

        } else {
            copy = ngx_array_push_n(conf->headers_set_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = src[i].key.len + sizeof(": ") - 1;


            size = (sizeof(ngx_http_script_copy_code_t)
                    + src[i].key.len + sizeof(": ") - 1 + sizeof(uintptr_t) - 1)
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->headers_set, size);
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = src[i].key.len + sizeof(": ") - 1;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
            p = ngx_cpymem(p, src[i].key.data, src[i].key.len);
            *p++ = ':'; *p = ' ';


            ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

            sc.cf = cf;
            sc.source = &src[i].value;
            sc.flushes = &conf->flushes;
            sc.lengths = &conf->headers_set_len;
            sc.values = &conf->headers_set;

            if (ngx_http_script_compile(&sc) != NGX_OK) {
                return NGX_ERROR;
            }


            copy = ngx_array_push_n(conf->headers_set_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = sizeof(CRLF) - 1;


            size = (sizeof(ngx_http_script_copy_code_t)
                    + sizeof(CRLF) - 1 + sizeof(uintptr_t) - 1)
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->headers_set, size);
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = sizeof(CRLF) - 1;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
            *p++ = CR; *p = LF;
        }

        code = ngx_array_push_n(conf->headers_set_len, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;

        code = ngx_array_push_n(conf->headers_set, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;
    }

    code = ngx_array_push_n(conf->headers_set_len, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_ERROR;
    }

    *code = (uintptr_t) NULL;


    hash.hash = &conf->headers_set_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = conf->headers_hash_max_size;
    hash.bucket_size = conf->headers_hash_bucket_size;
    hash.name = "passenger_headers_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    return ngx_hash_init(&hash, headers_names.elts, headers_names.nelts);
}

static ngx_int_t
merge_string_array(ngx_conf_t *cf, ngx_array_t **prev, ngx_array_t **conf)
{
    ngx_str_t  *prev_elems, *elem;
    ngx_uint_t  i;

    if (*prev != NGX_CONF_UNSET_PTR) {
        if (*conf == NGX_CONF_UNSET_PTR) {
            *conf = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
            if (*conf == NULL) {
                return NGX_ERROR;
            }
        }

        prev_elems = (ngx_str_t *) (*prev)->elts;
        for (i = 0; i < (*prev)->nelts; i++) {
            elem = (ngx_str_t *) ngx_array_push(*conf);
            if (elem == NULL) {
                return NGX_ERROR;
            }
            *elem = prev_elems[i];
        }
    }

    return NGX_OK;
}

struct postprocess_ctx_s {
    /* Reserved for future use */
};

ngx_int_t
passenger_postprocess_config(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_core_srv_conf_t   **server_confs, *server_conf;
    ngx_http_core_loc_conf_t    *base_location_conf;
    ngx_uint_t                  i;
    ngx_int_t                   result;
    postprocess_ctx_t           ctx;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    server_confs = (ngx_http_core_srv_conf_t **) cmcf->servers.elts;

    ngx_memzero(&ctx, sizeof(ctx));

    for (i = 0; i < cmcf->servers.nelts; i++) {
        server_conf = server_confs[i];
        base_location_conf = server_conf->ctx->loc_conf[ngx_http_core_module.ctx_index];

        result = postprocess_location_conf(cf, server_conf, base_location_conf, 1, &ctx);
        if (result != NGX_OK) {
            return result;
        }
    }

    return NGX_OK;
}

static ngx_int_t
postprocess_location_conf(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_core_loc_conf_t *location_conf,
    int is_toplevel,
    postprocess_ctx_t *ctx)
{
    passenger_loc_conf_t *plconf = server_conf->ctx->loc_conf[
        ngx_http_passenger_module.ctx_index];

    if (plconf->union_station_support != NGX_CONF_UNSET && plconf->union_station_support) {
        passenger_main_conf.union_station_support = 1;
    }

    return traverse_location_confs_nested_in_server_conf(cf, server_conf, location_conf, ctx);
}

static ngx_int_t
traverse_location_confs_nested_in_server_conf(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_core_loc_conf_t *location_conf,
    postprocess_ctx_t *ctx)
{
    ngx_int_t result;

    result = traverse_static_location_tree(cf, server_conf,
        location_conf->static_locations, ctx);
    if (result != NGX_OK) {
        return result;
    }

#ifdef NGX_PCRE
    return traverse_regex_locations(cf, server_conf,
        location_conf->regex_locations, ctx);
#else
    return result;
#endif
}

static ngx_int_t
traverse_static_location_tree(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_location_tree_node_t *node,
    postprocess_ctx_t *ctx)
{
    ngx_int_t result;

    if (node == NULL) {
        return NGX_OK;
    }

    if (node->left != NULL) {
        result = traverse_static_location_tree(cf, server_conf, node->left, ctx);
        if (result != NGX_OK) {
            return result;
        }
    }

    if (node->right != NULL) {
        result = traverse_static_location_tree(cf, server_conf, node->right, ctx);
        if (result != NGX_OK) {
            return result;
        }
    }

    if (node->exact != NULL) {
        result = postprocess_location_conf(cf, server_conf,
            node->exact, 0, ctx);
    } else {
        result = postprocess_location_conf(cf, server_conf,
            node->inclusive, 0, ctx);
    }
    if (result != NGX_OK) {
        return result;
    }

    return NGX_OK;
}

#ifdef NGX_PCRE
static ngx_int_t
traverse_regex_locations(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *server_conf,
    ngx_http_core_loc_conf_t **regex_locations,
    postprocess_ctx_t *ctx)
{
    ngx_uint_t i;
    ngx_int_t  result;

    if (regex_locations == NULL) {
        return NGX_OK;
    }

    for (i = 0; regex_locations[i] != NULL; i++) {
        result = postprocess_location_conf(cf, server_conf,
            regex_locations[i], 0, ctx);
        if (result != NGX_OK) {
            return result;
        }
    }

    return NGX_OK;
}
#endif

static int
string_keyval_has_key(ngx_array_t *table, ngx_str_t *key)
{
    ngx_keyval_t  *elems;
    ngx_uint_t     i;

    elems = (ngx_keyval_t *) table->elts;
    for (i = 0; i < table->nelts; i++) {
        if (elems[i].key.len == key->len
         && memcmp(elems[i].key.data, key->data, key->len) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static ngx_int_t
merge_string_keyval_table(ngx_conf_t *cf, ngx_array_t **prev, ngx_array_t **conf)
{
    ngx_keyval_t  *prev_elems, *elem;
    ngx_uint_t     i;

    if (*prev != NULL) {
        if (*conf == NULL) {
            *conf = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
            if (*conf == NULL) {
                return NGX_ERROR;
            }
        }

        prev_elems = (ngx_keyval_t *) (*prev)->elts;
        for (i = 0; i < (*prev)->nelts; i++) {
            if (!string_keyval_has_key(*conf, &prev_elems[i].key)) {
                elem = (ngx_keyval_t *) ngx_array_push(*conf);
                if (elem == NULL) {
                    return NGX_ERROR;
                }
                *elem = prev_elems[i];
            }
        }
    }

    return NGX_OK;
}

#ifndef PASSENGER_IS_ENTERPRISE
static char *
passenger_enterprise_only(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    return ": this feature is only available in Phusion Passenger Enterprise. "
        "You are currently running the open source Phusion Passenger. "
        "Please learn more about and/or buy Phusion Passenger Enterprise at https://www.phusionpassenger.com/enterprise ;";
}
#endif

static char *
passenger_enabled(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    passenger_loc_conf_t        *passenger_conf = conf;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_str_t                   *value;
    ngx_url_t                    upstream_url;

    value = cf->args->elts;
    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        passenger_conf->enabled = 1;

        /* Register a placeholder value as upstream address. The real upstream
         * address (the Passenger core socket filename) will be set while processing
         * requests, because we can't start the watchdog (and thus the Passenger core)
         * until config loading is done.
         */
        ngx_memzero(&upstream_url, sizeof(ngx_url_t));
        upstream_url.url = pp_placeholder_upstream_address;
        upstream_url.no_resolve = 1;
        passenger_conf->upstream_config.upstream = ngx_http_upstream_add(cf, &upstream_url, 0);
        if (passenger_conf->upstream_config.upstream == NULL) {
            return NGX_CONF_ERROR;
        }

        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        clcf->handler = passenger_content_handler;

        if (clcf->name.data != NULL
         && clcf->name.data[clcf->name.len - 1] == '/') {
            clcf->auto_redirect = 1;
        }
    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        passenger_conf->enabled = 0;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "\"passenger_enabled\" must be either set to \"on\" "
            "or \"off\"");

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

char *
union_station_filter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value, *s;
    ngx_array_t      **a;
    ngx_conf_post_t   *post;
    char              *message;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NGX_CONF_UNSET_PTR) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    s = ngx_array_push(*a);
    if (s == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    *s = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, s);
    }

    message = passenger_filter_validate((const char *) value[1].data, value[1].len);
    if (message != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Union Station filter syntax error: %s; ",
            message);
        free(message);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
rails_framework_spawner_idle_time(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_conf_log_error(NGX_LOG_ALERT, cf, 0, "The 'rails_framework_spawner_idle_time' "
        "directive is deprecated; please set 'passenger_max_preloader_idle_time' instead");
    return NGX_CONF_OK;
}

static char *
passenger_use_global_queue(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_conf_log_error(NGX_LOG_ALERT, cf, 0, "The 'passenger_use_global_queue' "
        "directive is obsolete and doesn't do anything anymore. Global queuing "
        "is now always enabled. Please remove this configuration directive.");
    return NGX_CONF_OK;
}

static char *
set_null_terminated_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value;
    ngx_array_t      **a;
    ngx_keyval_t      *kv;
    ngx_conf_post_t   *post;
    u_char            *last;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NULL) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    kv = ngx_array_push(*a);
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    kv->key.data = ngx_palloc(cf->pool, value[1].len + 1);
    kv->key.len  = value[1].len + 1;
    last = ngx_copy(kv->key.data, value[1].data, value[1].len);
    *last = '\0';

    kv->value.data = ngx_palloc(cf->pool, value[2].len + 1);
    kv->value.len  = value[2].len + 1;
    last = ngx_copy(kv->value.data, value[2].data, value[2].len);
    *last = '\0';

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, kv);
    }

    return NGX_CONF_OK;
}


const ngx_command_t passenger_commands[] = {

    /******** Main config ********/

    { ngx_string("passenger_root"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, root_dir),
      NULL },

    { ngx_string("passenger_ctl"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE2,
      set_null_terminated_keyval_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, ctl),
      NULL },

    { ngx_string("passenger_ruby"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, default_ruby),
      NULL },

    { ngx_string("passenger_log_level"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, log_level),
      NULL },

    { ngx_string("passenger_log_file"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, log_file),
      NULL },

    { ngx_string("passenger_file_descriptor_log_file"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, file_descriptor_log_file),
      NULL },

    { ngx_string("passenger_data_buffer_dir"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, data_buffer_dir),
      NULL },

    { ngx_string("passenger_instance_registry_dir"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, instance_registry_dir),
      NULL },

    { ngx_string("passenger_pre_start"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, prestart_uris),
      NULL },

    { ngx_string("passenger_disable_security_update_check"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, disable_security_update_check),
      NULL },

	{ ngx_string("passenger_security_update_check_proxy"),
	  NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
	  ngx_conf_set_str_slot,
	  NGX_HTTP_MAIN_CONF_OFFSET,
	  offsetof(passenger_main_conf_t, security_update_check_proxy),
	  NULL },

	{ ngx_string("passenger_abort_on_startup_error"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, abort_on_startup_error),
      NULL },

    { ngx_string("passenger_max_pool_size"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, max_pool_size),
      NULL },

    { ngx_string("passenger_pool_idle_time"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, pool_idle_time),
      NULL },

    { ngx_string("passenger_response_buffer_high_watermark"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, response_buffer_high_watermark),
      NULL },

    { ngx_string("passenger_stat_throttle_rate"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, stat_throttle_rate),
      NULL },

    { ngx_string("passenger_show_version_in_header"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, show_version_in_header),
      NULL },

    { ngx_string("passenger_turbocaching"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, turbocaching),
      NULL },

    { ngx_string("passenger_user_switching"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, user_switching),
      NULL },

    { ngx_string("passenger_default_user"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, default_user),
      NULL },

    { ngx_string("passenger_default_group"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, default_group),
      NULL },

    { ngx_string("passenger_analytics_log_user"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, analytics_log_user),
      NULL },

    { ngx_string("passenger_analytics_log_group"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, analytics_log_group),
      NULL },

    { ngx_string("passenger_read_timeout"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream_config.read_timeout),
      NULL },

    { ngx_string("union_station_gateway_address"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, union_station_gateway_address),
      NULL },

    { ngx_string("union_station_gateway_port"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, union_station_gateway_port),
      NULL },

    { ngx_string("union_station_gateway_cert"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, union_station_gateway_cert),
      NULL },

    { ngx_string("union_station_proxy_address"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, union_station_proxy_address),
      NULL },

    /******** Per-location config ********/

    #include "ConfigurationCommands.c"

    /******** Backward compatibility ********/

    { ngx_string("passenger_debug_log_file"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, log_file),
      NULL },

    ngx_null_command
};

