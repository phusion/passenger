/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

/*
 * CacheLocationConfig.c is automatically generated from CacheLocationConfig.c.cxxcodebuilder,
 * using definitions from src/ruby_supportlib/phusion_passenger/nginx/config_options.rb.
 * Edits to CacheLocationConfig.c will be lost.
 *
 * To update CacheLocationConfig.c:
 *   rake nginx
 *
 * To force regeneration of CacheLocationConfig.c:
 *   rm -f src/nginx_module/CacheLocationConfig.c
 *   rake src/nginx_module/CacheLocationConfig.c
 */
/*
 * 0: NGX_ERROR, 1: OK
 */
int
generated_cache_location_part(ngx_conf_t *cf, passenger_loc_conf_t *conf) {
    size_t len = 0;
    u_char int_buf[32], *end, *buf, *pos;

    /*
     * Calculate lengths
     */

    if (conf->socket_backlog != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->socket_backlog);
        len += sizeof("!~PASSENGER_SOCKET_BACKLOG: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->core_file_descriptor_ulimit != NGX_CONF_UNSET_UINT) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%ui",
            conf->core_file_descriptor_ulimit);
        len += sizeof("!~PASSENGER_CORE_FILE_DESCRIPTOR_ULIMIT: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->disable_security_update_check != NGX_CONF_UNSET) {
        len += sizeof("!~DISABLE_SECURITY_UPDATE_CHECK: ") - 1;
        len += conf->disable_security_update_check
            ? sizeof("t\r\n") - 1
            : sizeof("f\r\n") - 1;
    }

    if (conf->security_update_check_proxy.data != NULL) {
        len += sizeof("!~SECURITY_UPDATE_CHECK_PROXY: ") - 1;
        len += conf->security_update_check_proxy.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->app_file_descriptor_ulimit != NGX_CONF_UNSET_UINT) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%ui",
            conf->app_file_descriptor_ulimit);
        len += sizeof("!~PASSENGER_APP_FILE_DESCRIPTOR_ULIMIT: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->ruby.data != NULL) {
        len += sizeof("!~PASSENGER_RUBY: ") - 1;
        len += conf->ruby.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->python.data != NULL) {
        len += sizeof("!~PASSENGER_PYTHON: ") - 1;
        len += conf->python.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->nodejs.data != NULL) {
        len += sizeof("!~PASSENGER_NODEJS: ") - 1;
        len += conf->nodejs.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->meteor_app_settings.data != NULL) {
        len += sizeof("!~PASSENGER_METEOR_APP_SETTINGS: ") - 1;
        len += conf->meteor_app_settings.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->environment.data != NULL) {
        len += sizeof("!~PASSENGER_APP_ENV: ") - 1;
        len += conf->environment.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->friendly_error_pages != NGX_CONF_UNSET) {
        len += sizeof("!~PASSENGER_FRIENDLY_ERROR_PAGES: ") - 1;
        len += conf->friendly_error_pages
            ? sizeof("t\r\n") - 1
            : sizeof("f\r\n") - 1;
    }

    if (conf->min_instances != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->min_instances);
        len += sizeof("!~PASSENGER_MIN_PROCESSES: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->max_instances_per_app != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_instances_per_app);
        len += sizeof("!~PASSENGER_MAX_PROCESSES: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->max_requests != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_requests);
        len += sizeof("!~PASSENGER_MAX_REQUESTS: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->start_timeout != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->start_timeout);
        len += sizeof("!~PASSENGER_START_TIMEOUT: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->user.data != NULL) {
        len += sizeof("!~PASSENGER_USER: ") - 1;
        len += conf->user.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->group.data != NULL) {
        len += sizeof("!~PASSENGER_GROUP: ") - 1;
        len += conf->group.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->app_group_name.data != NULL) {
        len += sizeof("!~PASSENGER_APP_GROUP_NAME: ") - 1;
        len += conf->app_group_name.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->app_root.data != NULL) {
        len += sizeof("!~PASSENGER_APP_ROOT: ") - 1;
        len += conf->app_root.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->app_rights.data != NULL) {
        len += sizeof("!~PASSENGER_APP_RIGHTS: ") - 1;
        len += conf->app_rights.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->union_station_support != NGX_CONF_UNSET) {
        len += sizeof("!~UNION_STATION_SUPPORT: ") - 1;
        len += conf->union_station_support
            ? sizeof("t\r\n") - 1
            : sizeof("f\r\n") - 1;
    }

    if (conf->debugger != NGX_CONF_UNSET) {
        len += sizeof("!~PASSENGER_DEBUGGER: ") - 1;
        len += conf->debugger
            ? sizeof("t\r\n") - 1
            : sizeof("f\r\n") - 1;
    }

    if (conf->max_preloader_idle_time != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_preloader_idle_time);
        len += sizeof("!~PASSENGER_MAX_PRELOADER_IDLE_TIME: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->spawn_method.data != NULL) {
        len += sizeof("!~PASSENGER_SPAWN_METHOD: ") - 1;
        len += conf->spawn_method.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->load_shell_envvars != NGX_CONF_UNSET) {
        len += sizeof("!~PASSENGER_LOAD_SHELL_ENVVARS: ") - 1;
        len += conf->load_shell_envvars
            ? sizeof("t\r\n") - 1
            : sizeof("f\r\n") - 1;
    }

    if (conf->union_station_key.data != NULL) {
        len += sizeof("!~UNION_STATION_KEY: ") - 1;
        len += conf->union_station_key.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->max_request_queue_size != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_request_queue_size);
        len += sizeof("!~PASSENGER_MAX_REQUEST_QUEUE_SIZE: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->request_queue_overflow_status_code != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->request_queue_overflow_status_code);
        len += sizeof("!~PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }

    if (conf->restart_dir.data != NULL) {
        len += sizeof("!~PASSENGER_RESTART_DIR: ") - 1;
        len += conf->restart_dir.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->startup_file.data != NULL) {
        len += sizeof("!~PASSENGER_STARTUP_FILE: ") - 1;
        len += conf->startup_file.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->sticky_sessions != NGX_CONF_UNSET) {
        len += sizeof("!~PASSENGER_STICKY_SESSIONS: ") - 1;
        len += conf->sticky_sessions
            ? sizeof("t\r\n") - 1
            : sizeof("f\r\n") - 1;
    }

    if (conf->sticky_sessions_cookie_name.data != NULL) {
        len += sizeof("!~PASSENGER_STICKY_SESSIONS_COOKIE_NAME: ") - 1;
        len += conf->sticky_sessions_cookie_name.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->vary_turbocache_by_cookie.data != NULL) {
        len += sizeof("!~PASSENGER_VARY_TURBOCACHE_BY_COOKIE: ") - 1;
        len += conf->vary_turbocache_by_cookie.len;
        len += sizeof("\r\n") - 1;
    }

    if (conf->abort_websockets_on_process_shutdown != NGX_CONF_UNSET) {
        len += sizeof("!~PASSENGER_ABORT_WEBSOCKETS_ON_PROCESS_SHUTDOWN: ") - 1;
        len += conf->abort_websockets_on_process_shutdown
            ? sizeof("t\r\n") - 1
            : sizeof("f\r\n") - 1;
    }

    if (conf->force_max_concurrent_requests_per_process != NGX_CONF_UNSET) {
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->force_max_concurrent_requests_per_process);
        len += sizeof("!~PASSENGER_FORCE_MAX_CONCURRENT_REQUESTS_PER_PROCESS: ") - 1;
        len += end - int_buf;
        len += sizeof("\r\n") - 1;
    }


    /* Create string */
    buf = pos = ngx_pnalloc(cf->pool, len);
    if (buf == NULL) {
        return 0;
    }

    if (conf->socket_backlog != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_SOCKET_BACKLOG: ",
            sizeof("!~PASSENGER_SOCKET_BACKLOG: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->socket_backlog);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->core_file_descriptor_ulimit != NGX_CONF_UNSET_UINT) {
        pos = ngx_copy(pos,
            "!~PASSENGER_CORE_FILE_DESCRIPTOR_ULIMIT: ",
            sizeof("!~PASSENGER_CORE_FILE_DESCRIPTOR_ULIMIT: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%ui",
            conf->core_file_descriptor_ulimit);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->disable_security_update_check != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~DISABLE_SECURITY_UPDATE_CHECK: ",
            sizeof("!~DISABLE_SECURITY_UPDATE_CHECK: ") - 1);
        if (conf->disable_security_update_check) {
            pos = ngx_copy(pos, "t\r\n", sizeof("t\r\n") - 1);
        } else {
            pos = ngx_copy(pos, "f\r\n", sizeof("f\r\n") - 1);
        }
    }

    if (conf->security_update_check_proxy.data != NULL) {
        pos = ngx_copy(pos,
            "!~SECURITY_UPDATE_CHECK_PROXY: ",
            sizeof("!~SECURITY_UPDATE_CHECK_PROXY: ") - 1);
        pos = ngx_copy(pos,
            conf->security_update_check_proxy.data,
            conf->security_update_check_proxy.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->app_file_descriptor_ulimit != NGX_CONF_UNSET_UINT) {
        pos = ngx_copy(pos,
            "!~PASSENGER_APP_FILE_DESCRIPTOR_ULIMIT: ",
            sizeof("!~PASSENGER_APP_FILE_DESCRIPTOR_ULIMIT: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%ui",
            conf->app_file_descriptor_ulimit);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->ruby.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_RUBY: ",
            sizeof("!~PASSENGER_RUBY: ") - 1);
        pos = ngx_copy(pos,
            conf->ruby.data,
            conf->ruby.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->python.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_PYTHON: ",
            sizeof("!~PASSENGER_PYTHON: ") - 1);
        pos = ngx_copy(pos,
            conf->python.data,
            conf->python.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->nodejs.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_NODEJS: ",
            sizeof("!~PASSENGER_NODEJS: ") - 1);
        pos = ngx_copy(pos,
            conf->nodejs.data,
            conf->nodejs.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->meteor_app_settings.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_METEOR_APP_SETTINGS: ",
            sizeof("!~PASSENGER_METEOR_APP_SETTINGS: ") - 1);
        pos = ngx_copy(pos,
            conf->meteor_app_settings.data,
            conf->meteor_app_settings.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->environment.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_APP_ENV: ",
            sizeof("!~PASSENGER_APP_ENV: ") - 1);
        pos = ngx_copy(pos,
            conf->environment.data,
            conf->environment.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->friendly_error_pages != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_FRIENDLY_ERROR_PAGES: ",
            sizeof("!~PASSENGER_FRIENDLY_ERROR_PAGES: ") - 1);
        if (conf->friendly_error_pages) {
            pos = ngx_copy(pos, "t\r\n", sizeof("t\r\n") - 1);
        } else {
            pos = ngx_copy(pos, "f\r\n", sizeof("f\r\n") - 1);
        }
    }

    if (conf->min_instances != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_MIN_PROCESSES: ",
            sizeof("!~PASSENGER_MIN_PROCESSES: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->min_instances);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->max_instances_per_app != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_MAX_PROCESSES: ",
            sizeof("!~PASSENGER_MAX_PROCESSES: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_instances_per_app);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->max_requests != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_MAX_REQUESTS: ",
            sizeof("!~PASSENGER_MAX_REQUESTS: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_requests);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->start_timeout != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_START_TIMEOUT: ",
            sizeof("!~PASSENGER_START_TIMEOUT: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->start_timeout);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->user.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_USER: ",
            sizeof("!~PASSENGER_USER: ") - 1);
        pos = ngx_copy(pos,
            conf->user.data,
            conf->user.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->group.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_GROUP: ",
            sizeof("!~PASSENGER_GROUP: ") - 1);
        pos = ngx_copy(pos,
            conf->group.data,
            conf->group.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->app_group_name.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_APP_GROUP_NAME: ",
            sizeof("!~PASSENGER_APP_GROUP_NAME: ") - 1);
        pos = ngx_copy(pos,
            conf->app_group_name.data,
            conf->app_group_name.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->app_root.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_APP_ROOT: ",
            sizeof("!~PASSENGER_APP_ROOT: ") - 1);
        pos = ngx_copy(pos,
            conf->app_root.data,
            conf->app_root.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->app_rights.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_APP_RIGHTS: ",
            sizeof("!~PASSENGER_APP_RIGHTS: ") - 1);
        pos = ngx_copy(pos,
            conf->app_rights.data,
            conf->app_rights.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->union_station_support != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~UNION_STATION_SUPPORT: ",
            sizeof("!~UNION_STATION_SUPPORT: ") - 1);
        if (conf->union_station_support) {
            pos = ngx_copy(pos, "t\r\n", sizeof("t\r\n") - 1);
        } else {
            pos = ngx_copy(pos, "f\r\n", sizeof("f\r\n") - 1);
        }
    }

    if (conf->debugger != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_DEBUGGER: ",
            sizeof("!~PASSENGER_DEBUGGER: ") - 1);
        if (conf->debugger) {
            pos = ngx_copy(pos, "t\r\n", sizeof("t\r\n") - 1);
        } else {
            pos = ngx_copy(pos, "f\r\n", sizeof("f\r\n") - 1);
        }
    }

    if (conf->max_preloader_idle_time != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_MAX_PRELOADER_IDLE_TIME: ",
            sizeof("!~PASSENGER_MAX_PRELOADER_IDLE_TIME: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_preloader_idle_time);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->spawn_method.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_SPAWN_METHOD: ",
            sizeof("!~PASSENGER_SPAWN_METHOD: ") - 1);
        pos = ngx_copy(pos,
            conf->spawn_method.data,
            conf->spawn_method.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->load_shell_envvars != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_LOAD_SHELL_ENVVARS: ",
            sizeof("!~PASSENGER_LOAD_SHELL_ENVVARS: ") - 1);
        if (conf->load_shell_envvars) {
            pos = ngx_copy(pos, "t\r\n", sizeof("t\r\n") - 1);
        } else {
            pos = ngx_copy(pos, "f\r\n", sizeof("f\r\n") - 1);
        }
    }

    if (conf->union_station_key.data != NULL) {
        pos = ngx_copy(pos,
            "!~UNION_STATION_KEY: ",
            sizeof("!~UNION_STATION_KEY: ") - 1);
        pos = ngx_copy(pos,
            conf->union_station_key.data,
            conf->union_station_key.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->max_request_queue_size != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_MAX_REQUEST_QUEUE_SIZE: ",
            sizeof("!~PASSENGER_MAX_REQUEST_QUEUE_SIZE: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->max_request_queue_size);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->request_queue_overflow_status_code != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE: ",
            sizeof("!~PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->request_queue_overflow_status_code);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->restart_dir.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_RESTART_DIR: ",
            sizeof("!~PASSENGER_RESTART_DIR: ") - 1);
        pos = ngx_copy(pos,
            conf->restart_dir.data,
            conf->restart_dir.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->startup_file.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_STARTUP_FILE: ",
            sizeof("!~PASSENGER_STARTUP_FILE: ") - 1);
        pos = ngx_copy(pos,
            conf->startup_file.data,
            conf->startup_file.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->sticky_sessions != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_STICKY_SESSIONS: ",
            sizeof("!~PASSENGER_STICKY_SESSIONS: ") - 1);
        if (conf->sticky_sessions) {
            pos = ngx_copy(pos, "t\r\n", sizeof("t\r\n") - 1);
        } else {
            pos = ngx_copy(pos, "f\r\n", sizeof("f\r\n") - 1);
        }
    }

    if (conf->sticky_sessions_cookie_name.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_STICKY_SESSIONS_COOKIE_NAME: ",
            sizeof("!~PASSENGER_STICKY_SESSIONS_COOKIE_NAME: ") - 1);
        pos = ngx_copy(pos,
            conf->sticky_sessions_cookie_name.data,
            conf->sticky_sessions_cookie_name.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->vary_turbocache_by_cookie.data != NULL) {
        pos = ngx_copy(pos,
            "!~PASSENGER_VARY_TURBOCACHE_BY_COOKIE: ",
            sizeof("!~PASSENGER_VARY_TURBOCACHE_BY_COOKIE: ") - 1);
        pos = ngx_copy(pos,
            conf->vary_turbocache_by_cookie.data,
            conf->vary_turbocache_by_cookie.len);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }
    if (conf->abort_websockets_on_process_shutdown != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_ABORT_WEBSOCKETS_ON_PROCESS_SHUTDOWN: ",
            sizeof("!~PASSENGER_ABORT_WEBSOCKETS_ON_PROCESS_SHUTDOWN: ") - 1);
        if (conf->abort_websockets_on_process_shutdown) {
            pos = ngx_copy(pos, "t\r\n", sizeof("t\r\n") - 1);
        } else {
            pos = ngx_copy(pos, "f\r\n", sizeof("f\r\n") - 1);
        }
    }

    if (conf->force_max_concurrent_requests_per_process != NGX_CONF_UNSET) {
        pos = ngx_copy(pos,
            "!~PASSENGER_FORCE_MAX_CONCURRENT_REQUESTS_PER_PROCESS: ",
            sizeof("!~PASSENGER_FORCE_MAX_CONCURRENT_REQUESTS_PER_PROCESS: ") - 1);
        end = ngx_snprintf(int_buf,
            sizeof(int_buf) - 1,
            "%d",
            conf->force_max_concurrent_requests_per_process);
        pos = ngx_copy(pos, int_buf, end - int_buf);
        pos = ngx_copy(pos, (const u_char *) "\r\n", sizeof("\r\n") - 1);
    }

    conf->options_cache.data = buf;
    conf->options_cache.len = pos - buf;

    return 1;
}

