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
 * CreateLocationConfig.c is automatically generated from CreateLocationConfig.c.cxxcodebuilder,
 * using definitions from src/ruby_supportlib/phusion_passenger/nginx/config_options.rb.
 * Edits to CreateLocationConfig.c will be lost.
 *
 * To update CreateLocationConfig.c:
 *   rake nginx
 *
 * To force regeneration of CreateLocationConfig.c:
 *   rm -f src/nginx_module/CreateLocationConfig.c
 *   rake src/nginx_module/CreateLocationConfig.c
 */

void
generated_set_conf_part(passenger_loc_conf_t  *conf) {
    conf->socket_backlog = NGX_CONF_UNSET;
    conf->core_file_descriptor_ulimit = NGX_CONF_UNSET_UINT;
    conf->disable_security_update_check = NGX_CONF_UNSET;
    conf->security_update_check_proxy.data = NULL;
    conf->security_update_check_proxy.len  = 0;
    conf->app_file_descriptor_ulimit = NGX_CONF_UNSET_UINT;
    conf->enabled = NGX_CONF_UNSET;
    conf->ruby.data = NULL;
    conf->ruby.len  = 0;
    conf->python.data = NULL;
    conf->python.len  = 0;
    conf->nodejs.data = NULL;
    conf->nodejs.len  = 0;
    conf->meteor_app_settings.data = NULL;
    conf->meteor_app_settings.len  = 0;
    conf->environment.data = NULL;
    conf->environment.len  = 0;
    conf->friendly_error_pages = NGX_CONF_UNSET;
    conf->min_instances = NGX_CONF_UNSET;
    conf->max_instances_per_app = NGX_CONF_UNSET;
    conf->max_requests = NGX_CONF_UNSET;
    conf->start_timeout = NGX_CONF_UNSET;
    conf->base_uris = NGX_CONF_UNSET_PTR;
    conf->document_root.data = NULL;
    conf->document_root.len  = 0;
    conf->user.data = NULL;
    conf->user.len  = 0;
    conf->group.data = NULL;
    conf->group.len  = 0;
    conf->app_group_name.data = NULL;
    conf->app_group_name.len  = 0;
    conf->app_root.data = NULL;
    conf->app_root.len  = 0;
    conf->app_rights.data = NULL;
    conf->app_rights.len  = 0;
    conf->union_station_support = NGX_CONF_UNSET;
    conf->union_station_filters = NGX_CONF_UNSET_PTR;
    conf->debugger = NGX_CONF_UNSET;
    conf->max_preloader_idle_time = NGX_CONF_UNSET;
    conf->env_vars = NULL;
    conf->headers_hash_max_size = NGX_CONF_UNSET_UINT;
    conf->headers_hash_bucket_size = NGX_CONF_UNSET_UINT;
    conf->spawn_method.data = NULL;
    conf->spawn_method.len  = 0;
    conf->load_shell_envvars = NGX_CONF_UNSET;
    conf->union_station_key.data = NULL;
    conf->union_station_key.len  = 0;
    conf->max_request_queue_size = NGX_CONF_UNSET;
    conf->request_queue_overflow_status_code = NGX_CONF_UNSET;
    conf->restart_dir.data = NULL;
    conf->restart_dir.len  = 0;
    conf->app_type.data = NULL;
    conf->app_type.len  = 0;
    conf->startup_file.data = NULL;
    conf->startup_file.len  = 0;
    conf->sticky_sessions = NGX_CONF_UNSET;
    conf->sticky_sessions_cookie_name.data = NULL;
    conf->sticky_sessions_cookie_name.len  = 0;
    conf->vary_turbocache_by_cookie.data = NULL;
    conf->vary_turbocache_by_cookie.len  = 0;
    conf->abort_websockets_on_process_shutdown = NGX_CONF_UNSET;
    conf->force_max_concurrent_requests_per_process = NGX_CONF_UNSET;
}

