/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) 2007 Manlio Perillo (manlio.perillo@gmail.com)
 * Copyright (C) 2008, 2009 Phusion
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

#include "ngx_http_passenger_module.h"
#include "Configuration.h"
#include "ContentHandler.h"


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

#if NGINX_VERSION_NUM >= 7000
    static ngx_path_init_t  ngx_http_proxy_temp_path = {
        ngx_string(NGX_HTTP_PROXY_TEMP_PATH), { 1, 2, 0 }
    };
#endif


void *
passenger_create_main_conf(ngx_conf_t *cf)
{
    passenger_main_conf_t *conf;
    
    conf = ngx_pcalloc(cf->pool, sizeof(passenger_main_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    
    conf->log_level     = (ngx_uint_t) NGX_CONF_UNSET;
    conf->max_pool_size = (ngx_uint_t) NGX_CONF_UNSET;
    conf->max_instances_per_app = (ngx_uint_t) NGX_CONF_UNSET;
    conf->pool_idle_time = (ngx_uint_t) NGX_CONF_UNSET;
    conf->user_switching = NGX_CONF_UNSET;
    conf->default_user.data = NULL;
    conf->default_user.len  = 0;
    
    return conf;
}

char *
passenger_init_main_conf(ngx_conf_t *cf, void *conf_pointer)
{
    passenger_main_conf_t *conf;
    
    conf = &passenger_main_conf;
    *conf = *((passenger_main_conf_t *) conf_pointer);
    
    if (conf->ruby.len == 0) {
        conf->ruby.data = (u_char *) "ruby";
        conf->ruby.len  = sizeof("ruby") - 1;
    }
    
    if (conf->log_level == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->log_level = 0;
    }
    
    if (conf->max_pool_size == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->max_pool_size = 6;
    }
    
    if (conf->max_instances_per_app == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->max_instances_per_app = 0;
    }
    
    if (conf->pool_idle_time == (ngx_uint_t) NGX_CONF_UNSET) {
        conf->pool_idle_time = 300;
    }
    
    if (conf->user_switching == NGX_CONF_UNSET) {
        conf->user_switching = 1;
    }
    
    if (conf->default_user.len == 0) {
        conf->default_user.len  = sizeof("nobody") - 1;
        conf->default_user.data = (u_char *) "nobody";
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
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.hide_headers_hash = { NULL, 0 };
     *     conf->upstream.hide_headers = NULL;
     *     conf->upstream.pass_headers = NULL;
     *     conf->upstream.schema = { 0, NULL };
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     *     conf->upstream.store_lengths = NULL;
     *     conf->upstream.store_values = NULL;
     *
     *     conf->index.len = 0;
     *     conf->index.data = NULL;
     */

    conf->enabled = NGX_CONF_UNSET;
    conf->use_global_queue = NGX_CONF_UNSET;
    conf->environment.data = NULL;
    conf->environment.len = 0;
    conf->spawn_method.data = NULL;
    conf->spawn_method.len = 0;
    conf->base_uris = NGX_CONF_UNSET_PTR;
    conf->framework_spawner_idle_time = NGX_CONF_UNSET;
    conf->app_spawner_idle_time = NGX_CONF_UNSET;

    conf->upstream.store = NGX_CONF_UNSET;
    conf->upstream.store_access = NGX_CONF_UNSET_UINT;
    conf->upstream.buffering = NGX_CONF_UNSET;
    conf->upstream.ignore_client_abort = NGX_CONF_UNSET;

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.send_lowat = NGX_CONF_UNSET_SIZE;
    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    conf->upstream.busy_buffers_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.max_temp_file_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.temp_file_write_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream.pass_request_headers = NGX_CONF_UNSET;
    conf->upstream.pass_request_body = NGX_CONF_UNSET;

    conf->upstream.intercept_errors = NGX_CONF_UNSET;

    conf->upstream.cyclic_temp_file = 0;
    
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
    #if NGINX_VERSION_NUM < 7000
        u_char                       *temp_path;
    #endif

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_value(conf->use_global_queue, prev->use_global_queue, 0);
    ngx_conf_merge_str_value(conf->environment, prev->environment, "production");
    ngx_conf_merge_str_value(conf->spawn_method, prev->spawn_method, "smart-lv2");
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


    if (conf->upstream.store != 0) {
        ngx_conf_merge_value(conf->upstream.store,
                                  prev->upstream.store, 0);

        if (conf->upstream.store_lengths == NULL) {
            conf->upstream.store_lengths = prev->upstream.store_lengths;
            conf->upstream.store_values = prev->upstream.store_values;
        }
    }

    ngx_conf_merge_uint_value(conf->upstream.store_access,
                              prev->upstream.store_access, 0600);

    ngx_conf_merge_value(conf->upstream.buffering,
                              prev->upstream.buffering, 1);

    ngx_conf_merge_value(conf->upstream.ignore_client_abort,
                              prev->upstream.ignore_client_abort, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 600000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 600000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 600000);

    ngx_conf_merge_size_value(conf->upstream.send_lowat,
                              prev->upstream.send_lowat, 0);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);


    ngx_conf_merge_bufs_value(conf->upstream.bufs, prev->upstream.bufs,
                              8, ngx_pagesize);

    if (conf->upstream.bufs.num < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "there must be at least 2 \"scgi_buffers\"");
        return NGX_CONF_ERROR;
    }


    size = conf->upstream.buffer_size;
    if (size < conf->upstream.bufs.size) {
        size = conf->upstream.bufs.size;
    }


    ngx_conf_merge_size_value(conf->upstream.busy_buffers_size_conf,
                              prev->upstream.busy_buffers_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.busy_buffers_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.busy_buffers_size = 2 * size;
    } else {
        conf->upstream.busy_buffers_size =
                                         conf->upstream.busy_buffers_size_conf;
    }

    if (conf->upstream.busy_buffers_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"scgi_busy_buffers_size\" must be equal or bigger than "
             "maximum of the value of \"scgi_buffer_size\" and "
             "one of the \"scgi_buffers\"");

        return NGX_CONF_ERROR;
    }

    if (conf->upstream.busy_buffers_size
        > (conf->upstream.bufs.num - 1) * conf->upstream.bufs.size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"scgi_busy_buffers_size\" must be less than "
             "the size of all \"scgi_buffers\" minus one buffer");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream.temp_file_write_size_conf,
                              prev->upstream.temp_file_write_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.temp_file_write_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.temp_file_write_size = 2 * size;
    } else {
        conf->upstream.temp_file_write_size =
                                      conf->upstream.temp_file_write_size_conf;
    }

    if (conf->upstream.temp_file_write_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"scgi_temp_file_write_size\" must be equal or bigger than "
             "maximum of the value of \"scgi_buffer_size\" and "
             "one of the \"scgi_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream.max_temp_file_size_conf,
                              prev->upstream.max_temp_file_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.max_temp_file_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.max_temp_file_size = 1024 * 1024 * 1024;
    } else {
        conf->upstream.max_temp_file_size =
                                        conf->upstream.max_temp_file_size_conf;
    }

    if (conf->upstream.max_temp_file_size != 0
        && conf->upstream.max_temp_file_size < size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"scgi_max_temp_file_size\" must be equal to zero to disable "
             "the temporary files usage or must be equal or bigger than "
             "maximum of the value of \"scgi_buffer_size\" and "
             "one of the \"scgi_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    #if NGINX_VERSION_NUM < 7000
        temp_path = ngx_palloc(cf->pool, NGX_MAX_PATH);
        ngx_memzero(temp_path, NGX_MAX_PATH);
        ngx_snprintf(temp_path, NGX_MAX_PATH, "%s/webserver_private", passenger_temp_dir);
        ngx_conf_merge_path_value(conf->upstream.temp_path,
                                  prev->upstream.temp_path,
                                  temp_path, 1, 2, 0,
                                  ngx_garbage_collector_temp_handler, cf);
        conf->upstream.temp_path->name.len = ngx_strlen(conf->upstream.temp_path->name.data);
    #else
        ngx_conf_merge_path_value(cf,
                                  &conf->upstream.temp_path,
                                  prev->upstream.temp_path,
                                  &ngx_http_proxy_temp_path);
    #endif

    ngx_conf_merge_value(conf->upstream.pass_request_headers,
                              prev->upstream.pass_request_headers, 1);
    ngx_conf_merge_value(conf->upstream.pass_request_body,
                              prev->upstream.pass_request_body, 1);

    ngx_conf_merge_value(conf->upstream.intercept_errors,
                              prev->upstream.intercept_errors, 0);



    ngx_conf_merge_str_value(conf->index, prev->index, "");

    if (conf->upstream.hide_headers == NULL
        && conf->upstream.pass_headers == NULL)
    {
        conf->upstream.hide_headers = prev->upstream.hide_headers;
        conf->upstream.pass_headers = prev->upstream.pass_headers;
        conf->upstream.hide_headers_hash = prev->upstream.hide_headers_hash;

        if (conf->upstream.hide_headers_hash.buckets) {
            goto peers;
        }

    } else {
        if (conf->upstream.hide_headers == NULL) {
            conf->upstream.hide_headers = prev->upstream.hide_headers;
        }

        if (conf->upstream.pass_headers == NULL) {
            conf->upstream.pass_headers = prev->upstream.pass_headers;
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

    if (conf->upstream.hide_headers) {

        header = conf->upstream.hide_headers->elts;

        for (i = 0; i < conf->upstream.hide_headers->nelts; i++) {

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

    if (conf->upstream.pass_headers) {

        hk = hide_headers.elts;
        header = conf->upstream.pass_headers->elts;

        for (i = 0; i < conf->upstream.pass_headers->nelts; i++) {

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

    hash.hash = &conf->upstream.hide_headers_hash;
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

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
        #if NGINX_VERSION_NUM < 7000
            conf->upstream.schema = prev->upstream.schema;
        #endif
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
    passenger_loc_conf_t        *lcf = conf;

    ngx_url_t                    u;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_str_t                   *value;

    value = cf->args->elts;
    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        #if NGINX_VERSION_NUM < 7000
            if (lcf->upstream.schema.len) {
                return "is duplicate";
            }
        #endif

        lcf->enabled = 1;

        ngx_memzero(&u, sizeof(ngx_url_t));
        u.url.data = (u_char *) passenger_helper_server_socket;
        u.url.len  = strlen(passenger_helper_server_socket);
        u.no_resolve = 1;

        lcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
        if (lcf->upstream.upstream == NULL) {
            return NGX_CONF_ERROR;
        }

        #if NGINX_VERSION_NUM < 7000
            lcf->upstream.schema = passenger_schema_string;
        #endif

        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        clcf->handler = passenger_content_handler;

        if (clcf->name.data != NULL
         && clcf->name.data[clcf->name.len - 1] == '/') {
            clcf->auto_redirect = 1;
        }
    } else {
        lcf->enabled = 0;
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

    { ngx_string("passenger_use_global_queue"),
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, use_global_queue),
      NULL },

    { ngx_string("passenger_max_pool_size"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(passenger_main_conf_t, max_pool_size),
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
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, base_uris),
      NULL },

    { ngx_string("passenger_user_switching"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
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

    { ngx_string("rails_env"),
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, environment),
      NULL },

    { ngx_string("rails_spawn_method"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, spawn_method),
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
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, environment),
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

    { ngx_string("scgi_pass_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(passenger_loc_conf_t, upstream.pass_headers),
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

