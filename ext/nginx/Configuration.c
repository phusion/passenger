/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (C) 2010 Phusion
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

#include "ngx_http_passenger_module.h"
#include "Configuration.h"
#include "ContentHandler.h"
#include "../common/Constants.h"


static ngx_str_t headers_to_hide[] = {
    /* NOTE: Do not hide the "Status" header; some broken HTTP clients
     * expect this header. See http://tinyurl.com/87rezm
     */
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffer"),
    ngx_null_string
};

passenger_main_conf_t passenger_main_conf;

static ngx_path_init_t  ngx_http_proxy_temp_path = {
    ngx_string(NGX_HTTP_PROXY_TEMP_PATH), { 1, 2, 0 }
};


void *
passenger_create_main_conf(ngx_conf_t *cf)
{
    passenger_main_conf_t *conf;
    
    conf = ngx_pcalloc(cf->pool, sizeof(passenger_main_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    
    conf->log_level     = (ngx_int_t) NGX_CONF_UNSET;
    conf->debug_log_file.data = NULL;
    conf->debug_log_file.len = 0;
    conf->abort_on_startup_error = NGX_CONF_UNSET;
    conf->max_pool_size = (ngx_uint_t) NGX_CONF_UNSET;
    conf->max_instances_per_app = (ngx_uint_t) NGX_CONF_UNSET;
    conf->pool_idle_time = (ngx_uint_t) NGX_CONF_UNSET;
    conf->user_switching = NGX_CONF_UNSET;
    conf->default_user.data = NULL;
    conf->default_user.len  = 0;
    conf->default_group.data = NULL;
    conf->default_group.len  = 0;
    conf->analytics_log_dir.data = NULL;
    conf->analytics_log_dir.len  = 0;
    conf->analytics_log_user.data = NULL;
    conf->analytics_log_user.len  = 0;
    conf->analytics_log_group.data = NULL;
    conf->analytics_log_group.len  = 0;
    conf->analytics_log_permissions.data = NULL;
    conf->analytics_log_permissions.len  = 0;
    conf->union_station_gateway_address.data = NULL;
    conf->union_station_gateway_address.len = 0;
    conf->union_station_gateway_port = (ngx_uint_t) NGX_CONF_UNSET;
    conf->union_station_gateway_cert.data = NULL;
    conf->union_station_gateway_cert.len = 0;
    
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
    u_char                 filename[NGX_MAX_PATH], *last;
    ngx_str_t              str;
    struct passwd         *user_entry;
    struct group          *group_entry;
    char buf[128];
    
    conf = &passenger_main_conf;
    *conf = *((passenger_main_conf_t *) conf_pointer);
    
    if (conf->ruby.len == 0) {
        conf->ruby.data = (u_char *) "ruby";
        conf->ruby.len  = sizeof("ruby") - 1;
    }
    
    if (conf->log_level == (ngx_int_t) NGX_CONF_UNSET) {
        conf->log_level = DEFAULT_LOG_LEVEL;
    }
    
    if (conf->debug_log_file.len == 0) {
        conf->debug_log_file.data = (u_char *) "";
    }
    
    if (conf->abort_on_startup_error == NGX_CONF_UNSET) {
        conf->abort_on_startup_error = 0;
    }
    
    if (conf->max_pool_size == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->max_pool_size = DEFAULT_MAX_POOL_SIZE;
    }
    
    if (conf->max_instances_per_app == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->max_instances_per_app = DEFAULT_MAX_INSTANCES_PER_APP;
    }
    
    if (conf->pool_idle_time == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->pool_idle_time = DEFAULT_POOL_IDLE_TIME;
    }
    
    if (conf->user_switching == NGX_CONF_UNSET) {
        conf->user_switching = 1;
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
    
    if (conf->default_group.len == 0) {
        group_entry = getgrgid(user_entry->pw_gid);
        if (group_entry != NULL) {
            conf->default_group.len  = strlen(group_entry->gr_name);
            conf->default_group.data = ngx_palloc(cf->pool, conf->default_group.len + 1);
            memcpy(conf->default_group.data, group_entry->gr_name, conf->default_group.len + 1);
        } else {
            return "The primary group of the user specified by the 'default_user' "
                   "option does not exist. Your system's user account database is "
                   "probably broken, please fix it.";
        }
    } else {
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
    
    if (conf->analytics_log_dir.len == 0) {
        if (geteuid() == 0) {
            conf->analytics_log_dir.data = (u_char *) "/var/log/passenger-analytics";
            conf->analytics_log_dir.len  = sizeof("/var/log/passenger-analytics") - 1;
        } else {
            user_entry = getpwuid(geteuid());
            if (user_entry == NULL) {
                last = ngx_snprintf(filename, sizeof(filename),
                                    "/tmp/passenger-analytics-logs.user-%L",
                                    (int64_t) geteuid());
            } else {
                last = ngx_snprintf(filename, sizeof(filename),
                                    "/tmp/passenger-analytics-logs.%s",
                                    user_entry->pw_name);
            }
            str.data = filename;
            str.len  = last - filename;
            conf->analytics_log_dir.data = ngx_pstrdup(cf->pool, &str);
            conf->analytics_log_dir.len  = str.len;
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
    
    if (conf->analytics_log_permissions.len == 0) {
        conf->analytics_log_permissions.len  = sizeof(DEFAULT_ANALYTICS_LOG_PERMISSIONS) - 1;
        conf->analytics_log_permissions.data = (u_char *) DEFAULT_ANALYTICS_LOG_PERMISSIONS;
    }
    
    if (conf->union_station_gateway_address.len == 0) {
        conf->union_station_gateway_address.data = (u_char *) "";
    }
    
    if (conf->union_station_gateway_port == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->union_station_gateway_port = DEFAULT_UNION_STATION_GATEWAY_PORT;
    }
    
    if (conf->union_station_gateway_cert.len == 0) {
        conf->union_station_gateway_cert.data = (u_char *) "";
    }
    
    return NGX_CONF_OK;
}

void *
passenger_create_loc_conf(ngx_conf_t *cf)
{
    passenger_loc_conf_t  *conf;
    ngx_keyval_t          *kv;

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
     *
     *     conf->index.len = 0;
     *     conf->index.data = NULL;
     */

    conf->enabled = NGX_CONF_UNSET;
    conf->use_global_queue = NGX_CONF_UNSET;
    conf->friendly_error_pages = NGX_CONF_UNSET;
    conf->analytics = NGX_CONF_UNSET;
    conf->debugger = NGX_CONF_UNSET;
    conf->show_version_in_header = NGX_CONF_UNSET;
    conf->environment.data = NULL;
    conf->environment.len = 0;
    conf->spawn_method.data = NULL;
    conf->spawn_method.len = 0;
    conf->union_station_key.data = NULL;
    conf->union_station_key.len = 0;
    conf->user.data = NULL;
    conf->user.len = 0;
    conf->group.data = NULL;
    conf->group.len = 0;
    conf->app_group_name.data = NULL;
    conf->app_group_name.len = 0;
    conf->app_rights.data = NULL;
    conf->app_rights.len = 0;
    conf->base_uris = NGX_CONF_UNSET_PTR;
    conf->min_instances = NGX_CONF_UNSET;
    conf->framework_spawner_idle_time = NGX_CONF_UNSET;
    conf->app_spawner_idle_time = NGX_CONF_UNSET;

    /******************************/
    /******************************/

    conf->upstream_config.pass_headers = ngx_array_create(cf->pool, 1, sizeof(ngx_keyval_t));

    conf->upstream_config.store = NGX_CONF_UNSET;
    conf->upstream_config.store_access = NGX_CONF_UNSET_UINT;
    conf->upstream_config.buffering = NGX_CONF_UNSET;
    conf->upstream_config.ignore_client_abort = NGX_CONF_UNSET;

    conf->upstream_config.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream_config.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream_config.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream_config.send_lowat = NGX_CONF_UNSET_SIZE;
    conf->upstream_config.buffer_size = NGX_CONF_UNSET_SIZE;

    conf->upstream_config.busy_buffers_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream_config.max_temp_file_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream_config.temp_file_write_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream_config.pass_request_headers = NGX_CONF_UNSET;
    conf->upstream_config.pass_request_body = NGX_CONF_UNSET;

    conf->upstream_config.intercept_errors = NGX_CONF_UNSET;

    conf->upstream_config.cyclic_temp_file = 0;
    
    #define DEFINE_VAR_TO_PASS(header_name, var_name) \
        kv = ngx_array_push(conf->vars_source);       \
        kv->key.data = (u_char *) header_name;        \
        kv->key.len  = strlen(header_name) + 1;       \
        kv->value.data = (u_char *) var_name;         \
        kv->value.len  = strlen(var_name) + 1
    
    conf->vars_source = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
    if (conf->vars_source == NULL) {
        return NGX_CONF_ERROR;
    }
    
    DEFINE_VAR_TO_PASS("SCGI",            "1");
    DEFINE_VAR_TO_PASS("QUERY_STRING",    "$query_string");
    DEFINE_VAR_TO_PASS("REQUEST_METHOD",  "$request_method");
    DEFINE_VAR_TO_PASS("REQUEST_URI",     "$uri$is_args$args");
    DEFINE_VAR_TO_PASS("SERVER_PROTOCOL", "$server_protocol");
    DEFINE_VAR_TO_PASS("SERVER_SOFTWARE", "nginx/$nginx_version");
    DEFINE_VAR_TO_PASS("REMOTE_ADDR",     "$remote_addr");
    DEFINE_VAR_TO_PASS("REMOTE_PORT",     "$remote_port");
    DEFINE_VAR_TO_PASS("SERVER_ADDR",     "$server_addr");
    DEFINE_VAR_TO_PASS("SERVER_PORT",     "$server_port");
    DEFINE_VAR_TO_PASS("SERVER_NAME",     "$server_name");

    return conf;
}

char *
passenger_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    passenger_loc_conf_t         *prev = parent;
    passenger_loc_conf_t         *conf = child;

    u_char                       *p;
    size_t                        size;
    uintptr_t                    *code;
    ngx_str_t                    *header;
    ngx_uint_t                    i, j;
    ngx_array_t                   hide_headers;
    ngx_str_t                    *prev_base_uris, *base_uri;
    ngx_keyval_t                 *src;
    ngx_hash_key_t               *hk;
    ngx_hash_init_t               hash;
    ngx_http_script_compile_t     sc;
    ngx_http_script_copy_code_t  *copy;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_value(conf->use_global_queue, prev->use_global_queue, 1);
    ngx_conf_merge_value(conf->friendly_error_pages, prev->friendly_error_pages, 1);
    ngx_conf_merge_value(conf->analytics, prev->analytics, 0);
    ngx_conf_merge_value(conf->debugger, prev->debugger, 0);
    ngx_conf_merge_value(conf->show_version_in_header, prev->show_version_in_header, 1);
    ngx_conf_merge_str_value(conf->environment, prev->environment, "production");
    ngx_conf_merge_str_value(conf->spawn_method, prev->spawn_method, "smart-lv2");
    ngx_conf_merge_str_value(conf->union_station_key, prev->union_station_key, NULL);
    ngx_conf_merge_str_value(conf->user, prev->user, "");
    ngx_conf_merge_str_value(conf->group, prev->group, "");
    ngx_conf_merge_str_value(conf->app_group_name, prev->app_group_name, NULL);
    ngx_conf_merge_str_value(conf->app_rights, prev->app_rights, NULL);
    ngx_conf_merge_value(conf->min_instances, prev->min_instances, (ngx_int_t) -1);
    ngx_conf_merge_value(conf->framework_spawner_idle_time, prev->framework_spawner_idle_time, (ngx_int_t) -1);
    ngx_conf_merge_value(conf->app_spawner_idle_time, prev->app_spawner_idle_time, (ngx_int_t) -1);
    
    if (prev->base_uris != NGX_CONF_UNSET_PTR) {
        if (conf->base_uris == NGX_CONF_UNSET_PTR) {
            conf->base_uris = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
            if (conf->base_uris == NULL) {
                return NGX_CONF_ERROR;
            }
        }
        
        prev_base_uris = (ngx_str_t *) prev->base_uris->elts;
        for (i = 0; i < prev->base_uris->nelts; i++) {
            base_uri = (ngx_str_t *) ngx_array_push(conf->base_uris);
            if (base_uri == NULL) {
                return NGX_CONF_ERROR;
            }
            *base_uri = prev_base_uris[i];
        }
    }

    /******************************/
    /******************************/

    if (conf->upstream_config.store != 0) {
        ngx_conf_merge_value(conf->upstream_config.store,
                                  prev->upstream_config.store, 0);

        if (conf->upstream_config.store_lengths == NULL) {
            conf->upstream_config.store_lengths = prev->upstream_config.store_lengths;
            conf->upstream_config.store_values = prev->upstream_config.store_values;
        }
    }

    ngx_conf_merge_uint_value(conf->upstream_config.store_access,
                              prev->upstream_config.store_access, 0600);

    ngx_conf_merge_value(conf->upstream_config.buffering,
                              prev->upstream_config.buffering, 1);

    ngx_conf_merge_value(conf->upstream_config.ignore_client_abort,
                              prev->upstream_config.ignore_client_abort, 0);

    ngx_conf_merge_msec_value(conf->upstream_config.connect_timeout,
                              prev->upstream_config.connect_timeout, 600000);

    ngx_conf_merge_msec_value(conf->upstream_config.send_timeout,
                              prev->upstream_config.send_timeout, 600000);

    ngx_conf_merge_msec_value(conf->upstream_config.read_timeout,
                              prev->upstream_config.read_timeout, 600000);

    ngx_conf_merge_size_value(conf->upstream_config.send_lowat,
                              prev->upstream_config.send_lowat, 0);

    ngx_conf_merge_size_value(conf->upstream_config.buffer_size,
                              prev->upstream_config.buffer_size,
                              (size_t) ngx_pagesize);


    ngx_conf_merge_bufs_value(conf->upstream_config.bufs, prev->upstream_config.bufs,
                              8, ngx_pagesize);

    if (conf->upstream_config.bufs.num < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "there must be at least 2 \"scgi_buffers\"");
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
             "\"scgi_busy_buffers_size\" must be equal or bigger than "
             "maximum of the value of \"scgi_buffer_size\" and "
             "one of the \"scgi_buffers\"");

        return NGX_CONF_ERROR;
    }

    if (conf->upstream_config.busy_buffers_size
        > (conf->upstream_config.bufs.num - 1) * conf->upstream_config.bufs.size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"scgi_busy_buffers_size\" must be less than "
             "the size of all \"scgi_buffers\" minus one buffer");

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
             "\"scgi_temp_file_write_size\" must be equal or bigger than "
             "maximum of the value of \"scgi_buffer_size\" and "
             "one of the \"scgi_buffers\"");

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
             "\"scgi_max_temp_file_size\" must be equal to zero to disable "
             "the temporary files usage or must be equal or bigger than "
             "maximum of the value of \"scgi_buffer_size\" and "
             "one of the \"scgi_buffers\"");

        return NGX_CONF_ERROR;
    }


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

    ngx_conf_merge_value(conf->upstream_config.pass_request_headers,
                         prev->upstream_config.pass_request_headers, 1);
    ngx_conf_merge_value(conf->upstream_config.pass_request_body,
                         prev->upstream_config.pass_request_body, 1);

    ngx_conf_merge_value(conf->upstream_config.intercept_errors,
                         prev->upstream_config.intercept_errors, 0);



    ngx_conf_merge_str_value(conf->index, prev->index, "");

    if (conf->upstream_config.hide_headers == NULL
        && conf->upstream_config.pass_headers == NULL)
    {
        conf->upstream_config.hide_headers = prev->upstream_config.hide_headers;
        conf->upstream_config.pass_headers = prev->upstream_config.pass_headers;
        conf->upstream_config.hide_headers_hash = prev->upstream_config.hide_headers_hash;

        if (conf->upstream_config.hide_headers_hash.buckets) {
            goto peers;
        }

    } else {
        if (conf->upstream_config.hide_headers == NULL) {
            conf->upstream_config.hide_headers = prev->upstream_config.hide_headers;
        }

        if (conf->upstream_config.pass_headers == NULL) {
            conf->upstream_config.pass_headers = prev->upstream_config.pass_headers;
        }
    }

    if (ngx_array_init(&hide_headers, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    for (header = headers_to_hide; header->len; header++) {
        hk = ngx_array_push(&hide_headers);
        if (hk == NULL) {
            return NGX_CONF_ERROR;
        }

        hk->key = *header;
        hk->key_hash = ngx_hash_key_lc(header->data, header->len);
        hk->value = (void *) 1;
    }

    if (conf->upstream_config.hide_headers) {

        header = conf->upstream_config.hide_headers->elts;

        for (i = 0; i < conf->upstream_config.hide_headers->nelts; i++) {

            hk = hide_headers.elts;

            for (j = 0; j < hide_headers.nelts; j++) {
                if (ngx_strcasecmp(header[i].data, hk[j].key.data) == 0) {
                    goto exist;
                }
            }

            hk = ngx_array_push(&hide_headers);
            if (hk == NULL) {
                return NGX_CONF_ERROR;
            }

            hk->key = header[i];
            hk->key_hash = ngx_hash_key_lc(header[i].data, header[i].len);
            hk->value = (void *) 1;

        exist:

            continue;
        }
    }

    if (conf->upstream_config.pass_headers) {

        hk = hide_headers.elts;
        header = conf->upstream_config.pass_headers->elts;

        for (i = 0; i < conf->upstream_config.pass_headers->nelts; i++) {

            for (j = 0; j < hide_headers.nelts; j++) {

                if (hk[j].key.data == NULL) {
                    continue;
                }

                if (ngx_strcasecmp(header[i].data, hk[j].key.data) == 0) {
                    hk[j].key.data = NULL;
                    break;
                }
            }
        }
    }

    hash.hash = &conf->upstream_config.hide_headers_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "passenger_hide_headers_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    if (ngx_hash_init(&hash, hide_headers.elts, hide_headers.nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

peers:

    if (conf->upstream_config.upstream == NULL) {
        conf->upstream_config.upstream = prev->upstream_config.upstream;
    }

    if (conf->vars_source == NULL) {
        conf->flushes = prev->flushes;
        conf->vars_len = prev->vars_len;
        conf->vars = prev->vars;
        conf->vars_source = prev->vars_source;

        if (conf->vars_source == NULL) {
            return NGX_CONF_OK;
        }
    }

    conf->vars_len = ngx_array_create(cf->pool, 64, 1);
    if (conf->vars_len == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->vars = ngx_array_create(cf->pool, 512, 1);
    if (conf->vars == NULL) {
        return NGX_CONF_ERROR;
    }

    src = conf->vars_source->elts;
    for (i = 0; i < conf->vars_source->nelts; i++) {

        if (ngx_http_script_variables_count(&src[i].value) == 0) {
            copy = ngx_array_push_n(conf->vars_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_CONF_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                  ngx_http_script_copy_len_code;
            copy->len = src[i].key.len;


            copy = ngx_array_push_n(conf->vars_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_CONF_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = src[i].value.len;


            size = (sizeof(ngx_http_script_copy_code_t)
                       + src[i].key.len + src[i].value.len
                       + sizeof(uintptr_t) - 1)
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->vars, size);
            if (copy == NULL) {
                return NGX_CONF_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = src[i].key.len + src[i].value.len;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);

            p = ngx_cpymem(p, src[i].key.data, src[i].key.len);
            ngx_memcpy(p, src[i].value.data, src[i].value.len);

        } else {
            copy = ngx_array_push_n(conf->vars_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_CONF_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = src[i].key.len;


            size = (sizeof(ngx_http_script_copy_code_t)
                    + src[i].key.len + sizeof(uintptr_t) - 1)
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->vars, size);
            if (copy == NULL) {
                return NGX_CONF_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = src[i].key.len;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
            ngx_memcpy(p, src[i].key.data, src[i].key.len);


            ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

            sc.cf = cf;
            sc.source = &src[i].value;
            sc.flushes = &conf->flushes;
            sc.lengths = &conf->vars_len;
            sc.values = &conf->vars;

            if (ngx_http_script_compile(&sc) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }

        code = ngx_array_push_n(conf->vars_len, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_CONF_ERROR;
        }

        *code = (uintptr_t) NULL;


        code = ngx_array_push_n(conf->vars, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_CONF_ERROR;
        }

        *code = (uintptr_t) NULL;
    }

    code = ngx_array_push_n(conf->vars_len, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_CONF_ERROR;
    }

    *code = (uintptr_t) NULL;

    return NGX_CONF_OK;
}

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
         * address (the helper agent socket filename) will be set while processing
         * requests, because we can't start the helper agent until config
         * loading is done.
         */
        ngx_memzero(&upstream_url, sizeof(ngx_url_t));
        upstream_url.url = passenger_placeholder_upstream_address;
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

#if 0
static char *
ngx_http_scgi_lowat_check(ngx_conf_t *cf, void *post, void *data)
{
#if (NGX_FREEBSD)
    ssize_t *np = data;

    if ((u_long) *np >= ngx_freebsd_net_inet_tcp_sendspace) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"scgi_send_lowat\" must be less than %d "
                           "(sysctl net.inet.tcp.sendspace)",
                           ngx_freebsd_net_inet_tcp_sendspace);

        return NGX_CONF_ERROR;
    }

#elif !(NGX_HAVE_SO_SNDLOWAT)
    ssize_t *np = data;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"scgi_send_lowat\" is not supported, ignored");

    *np = 0;

#endif

    return NGX_CONF_OK;
}

static char *
ngx_http_scgi_store(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    passenger_loc_conf_t       *slcf = conf;

    ngx_str_t                  *value;
    ngx_http_script_compile_t   sc;

    if (slcf->upstream.store != NGX_CONF_UNSET || slcf->upstream.store_lengths)
    {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "on") == 0) {
        slcf->upstream.store = 1;
        return NGX_CONF_OK;
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        slcf->upstream.store = 0;
        return NGX_CONF_OK;
    }

    /* include the terminating '\0' into script */
    value[1].len++;

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = &value[1];
    sc.lengths = &slcf->upstream.store_lengths;
    sc.values = &slcf->upstream.store_values;
    sc.variables = ngx_http_script_variables_count(&value[1]);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

char *
ngx_scgi_set_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    /*
     * Like ngx_conf_set_keyval_slot but keeps the \0 at the end of
     * the strings.
     */

    char  *p = conf;

    ngx_str_t         *value;
    ngx_array_t      **a;
    ngx_keyval_t      *kv;
    ngx_conf_post_t   *post;

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

    /* strings are null terminated */
    value[1].len += 1;
    value[2].len += 1;

    kv->key = value[1];
    kv->value = value[2];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, kv);
    }

    return NGX_CONF_OK;
}


static ngx_conf_post_t  ngx_http_scgi_lowat_post =
    { ngx_http_scgi_lowat_check };

#endif /* 0 */

const ngx_command_t passenger_commands[] = {

    { ngx_string("passenger_enabled"),
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      passenger_enabled,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("passenger_root"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, root_dir),
      NULL },

    { ngx_string("passenger_ruby"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, ruby),
      NULL },

    { ngx_string("passenger_log_level"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, log_level),
      NULL },

    { ngx_string("passenger_debug_log_file"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, debug_log_file),
      NULL },

    { ngx_string("passenger_abort_on_startup_error"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, abort_on_startup_error),
      NULL },

    { ngx_string("passenger_use_global_queue"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, use_global_queue),
      NULL },

    { ngx_string("passenger_friendly_error_pages"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, friendly_error_pages),
      NULL },

    { ngx_string("passenger_max_pool_size"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, max_pool_size),
      NULL },

    { ngx_string("passenger_min_instances"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, min_instances),
      NULL },

    { ngx_string("passenger_max_instances_per_app"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, max_instances_per_app),
      NULL },

    { ngx_string("passenger_pool_idle_time"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, pool_idle_time),
      NULL },

    { ngx_string("passenger_base_uri"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, base_uris),
      NULL },

    { ngx_string("passenger_user_switching"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, user_switching),
      NULL },

    { ngx_string("passenger_user"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, user),
      NULL },

    { ngx_string("passenger_group"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, group),
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

    { ngx_string("passenger_app_group_name"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, app_group_name),
      NULL },

    { ngx_string("passenger_app_rights"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, app_rights),
      NULL },

    { ngx_string("passenger_analytics"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, analytics),
      NULL },

    { ngx_string("passenger_analytics_log_dir"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, analytics_log_dir),
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

    { ngx_string("passenger_analytics_log_permissions"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, analytics_log_permissions),
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

    { ngx_string("passenger_debugger"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, debugger),
      NULL },

    { ngx_string("passenger_show_version_in_header"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, show_version_in_header),
      NULL },

    { ngx_string("passenger_pre_start"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, prestart_uris),
      NULL },

    { ngx_string("passenger_pass_header"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream_config.pass_headers),
      NULL },

    { ngx_string("passenger_set_cgi_param"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE2,
      set_null_terminated_keyval_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, vars_source),
      NULL },

    { ngx_string("passenger_ignore_client_abort"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream_config.ignore_client_abort),
      NULL },

    { ngx_string("passenger_buffer_response"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream_config.buffering),
      NULL },

    { ngx_string("passenger_spawn_method"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, spawn_method),
      NULL },

    { ngx_string("union_station_key"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, union_station_key),
      NULL },

    { ngx_string("rails_env"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, environment),
      NULL },

    { ngx_string("rails_framework_spawner_idle_time"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, framework_spawner_idle_time),
      NULL },

    { ngx_string("rails_app_spawner_idle_time"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, app_spawner_idle_time),
      NULL },

    { ngx_string("rack_env"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, environment),
      NULL },

    /************************************/
    /************************************/

    /******** Backward compatibility options ********/

    { ngx_string("rails_spawn_method"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, spawn_method),
      NULL },

/*

    { ngx_string("scgi_index"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, index),
      NULL },

    { ngx_string("scgi_store"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_scgi_store,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("scgi_store_access"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
      ngx_conf_set_access_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.store_access),
      NULL },

    { ngx_string("scgi_ignore_client_abort"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.ignore_client_abort),
      NULL },

    { ngx_string("scgi_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("scgi_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("scgi_send_lowat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.send_lowat),
      &ngx_http_scgi_lowat_post },

    { ngx_string("scgi_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("scgi_pass_request_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.pass_request_headers),
      NULL },

    { ngx_string("scgi_pass_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.pass_request_body),
      NULL },

    { ngx_string("scgi_intercept_errors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.intercept_errors),
      NULL },

    { ngx_string("scgi_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("scgi_buffers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_bufs_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.bufs),
      NULL },

    { ngx_string("scgi_busy_buffers_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.busy_buffers_size_conf),
      NULL },

    { ngx_string("scgi_temp_path"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1234,
      ngx_conf_set_path_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.temp_path),
      (void *) ngx_garbage_collector_temp_handler },

    { ngx_string("scgi_max_temp_file_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.max_temp_file_size_conf),
      NULL },

    { ngx_string("scgi_temp_file_write_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.temp_file_write_size_conf),
      NULL },

    { ngx_string("scgi_var"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_scgi_set_keyval_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, vars_source),
      NULL },

    { ngx_string("scgi_hide_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.hide_headers),
      NULL },

*/

      ngx_null_command
};

