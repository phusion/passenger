/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
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
 * ConfigurationSetters.c is automatically generated from ConfigurationSetters.c.cxxcodebuilder,
 * using definitions from src/ruby_supportlib/phusion_passenger/nginx/config_options.rb.
 * Edits to ConfigurationSetters.c will be lost.
 *
 * To update ConfigurationSetters.c:
 *   rake nginx
 *
 * To force regeneration of ConfigurationSetters.c:
 *   rm -f src/nginx_module/ConfigurationSetters.c
 *   rake src/nginx_module/ConfigurationSetters.c
 */

static void
record_loc_conf_source_location(ngx_conf_t *cf, passenger_loc_conf_t *pl_conf, ngx_str_t *file, ngx_uint_t *line) {
    pl_conf->cscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_core_module);
    pl_conf->clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    if (cf->conf_file == NULL) {
        file->data = (u_char *) NULL;
        file->len = 0;
        *line = 0;
    } else if (cf->conf_file->file.fd == NGX_INVALID_FILE) {
        file->data = (u_char *) "(command line)";
        file->len = sizeof("(command line)") - 1;
        *line = 0;
    } else {
        *file = cf->conf_file->file.name;
        *line = cf->conf_file->line;
    }
}


char *
passenger_conf_set_socket_backlog(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_core_file_descriptor_ulimit(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_disable_security_update_check(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_security_update_check_proxy(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_app_file_descriptor_ulimit(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->app_file_descriptor_ulimit_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->app_file_descriptor_ulimit_source_file,
        &passenger_conf->app_file_descriptor_ulimit_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_ruby(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->ruby_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->ruby_source_file,
        &passenger_conf->ruby_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_python(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->python_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->python_source_file,
        &passenger_conf->python_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_nodejs(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->nodejs_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->nodejs_source_file,
        &passenger_conf->nodejs_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_meteor_app_settings(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->meteor_app_settings_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->meteor_app_settings_source_file,
        &passenger_conf->meteor_app_settings_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_app_env(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->environment_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->environment_source_file,
        &passenger_conf->environment_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_friendly_error_pages(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->friendly_error_pages_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->friendly_error_pages_source_file,
        &passenger_conf->friendly_error_pages_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_min_instances(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->min_instances_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->min_instances_source_file,
        &passenger_conf->min_instances_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_max_instances_per_app(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->max_instances_per_app_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->max_instances_per_app_source_file,
        &passenger_conf->max_instances_per_app_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_max_requests(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->max_requests_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->max_requests_source_file,
        &passenger_conf->max_requests_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_start_timeout(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->start_timeout_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->start_timeout_source_file,
        &passenger_conf->start_timeout_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_base_uri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->base_uris_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->base_uris_source_file,
        &passenger_conf->base_uris_source_line);

    return ngx_conf_set_str_array_slot(cf, cmd, conf);
}

char *
passenger_conf_set_document_root(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->document_root_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->document_root_source_file,
        &passenger_conf->document_root_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->user_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->user_source_file,
        &passenger_conf->user_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_group(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->group_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->group_source_file,
        &passenger_conf->group_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_app_group_name(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->app_group_name_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->app_group_name_source_file,
        &passenger_conf->app_group_name_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_app_root(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->app_root_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->app_root_source_file,
        &passenger_conf->app_root_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_app_rights(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->app_rights_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->app_rights_source_file,
        &passenger_conf->app_rights_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_union_station_support(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->union_station_support_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->union_station_support_source_file,
        &passenger_conf->union_station_support_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_debugger(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->debugger_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->debugger_source_file,
        &passenger_conf->debugger_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_max_preloader_idle_time(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->max_preloader_idle_time_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->max_preloader_idle_time_source_file,
        &passenger_conf->max_preloader_idle_time_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_env_var(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->env_vars_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->env_vars_source_file,
        &passenger_conf->env_vars_source_line);

    return ngx_conf_set_keyval_slot(cf, cmd, conf);
}

char *
passenger_conf_set_set_header(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->headers_source_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->headers_source_source_file,
        &passenger_conf->headers_source_source_line);

    return ngx_conf_set_keyval_slot(cf, cmd, conf);
}

char *
passenger_conf_set_pass_header(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->upstream_config_pass_headers_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->upstream_config_pass_headers_source_file,
        &passenger_conf->upstream_config_pass_headers_source_line);

    return ngx_conf_set_str_array_slot(cf, cmd, conf);
}

char *
passenger_conf_set_headers_hash_max_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->headers_hash_max_size_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->headers_hash_max_size_source_file,
        &passenger_conf->headers_hash_max_size_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_headers_hash_bucket_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->headers_hash_bucket_size_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->headers_hash_bucket_size_source_file,
        &passenger_conf->headers_hash_bucket_size_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_ignore_client_abort(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->upstream_config_ignore_client_abort_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->upstream_config_ignore_client_abort_source_file,
        &passenger_conf->upstream_config_ignore_client_abort_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_buffer_response(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->upstream_config_buffering_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->upstream_config_buffering_source_file,
        &passenger_conf->upstream_config_buffering_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_intercept_errors(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->upstream_config_intercept_errors_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->upstream_config_intercept_errors_source_file,
        &passenger_conf->upstream_config_intercept_errors_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_spawn_method(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->spawn_method_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->spawn_method_source_file,
        &passenger_conf->spawn_method_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_load_shell_envvars(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->load_shell_envvars_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->load_shell_envvars_source_file,
        &passenger_conf->load_shell_envvars_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_union_station_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->union_station_key_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->union_station_key_source_file,
        &passenger_conf->union_station_key_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_max_request_queue_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->max_request_queue_size_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->max_request_queue_size_source_file,
        &passenger_conf->max_request_queue_size_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_request_queue_overflow_status_code(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->request_queue_overflow_status_code_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->request_queue_overflow_status_code_source_file,
        &passenger_conf->request_queue_overflow_status_code_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

char *
passenger_conf_set_restart_dir(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->restart_dir_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->restart_dir_source_file,
        &passenger_conf->restart_dir_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_app_type(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->app_type_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->app_type_source_file,
        &passenger_conf->app_type_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_startup_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->startup_file_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->startup_file_source_file,
        &passenger_conf->startup_file_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_sticky_sessions(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->sticky_sessions_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->sticky_sessions_source_file,
        &passenger_conf->sticky_sessions_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_sticky_sessions_cookie_name(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->sticky_sessions_cookie_name_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->sticky_sessions_cookie_name_source_file,
        &passenger_conf->sticky_sessions_cookie_name_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_vary_turbocache_by_cookie(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->vary_turbocache_by_cookie_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->vary_turbocache_by_cookie_source_file,
        &passenger_conf->vary_turbocache_by_cookie_source_line);

    return ngx_conf_set_str_slot(cf, cmd, conf);
}

char *
passenger_conf_set_abort_websockets_on_process_shutdown(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->abort_websockets_on_process_shutdown_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->abort_websockets_on_process_shutdown_source_file,
        &passenger_conf->abort_websockets_on_process_shutdown_source_line);

    return ngx_conf_set_flag_slot(cf, cmd, conf);
}

char *
passenger_conf_set_force_max_concurrent_requests_per_process(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    passenger_loc_conf_t *passenger_conf = conf;

    passenger_conf->force_max_concurrent_requests_per_process_explicitly_set = 1;
    record_loc_conf_source_location(cf, passenger_conf,
        &passenger_conf->force_max_concurrent_requests_per_process_source_file,
        &passenger_conf->force_max_concurrent_requests_per_process_source_line);

    return ngx_conf_set_num_slot(cf, cmd, conf);
}

