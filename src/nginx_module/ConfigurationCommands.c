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
 * ConfigurationCommands.c is automatically generated from ConfigurationCommands.c.cxxcodebuilder,
 * using definitions from src/ruby_supportlib/phusion_passenger/nginx/config_options.rb.
 * Edits to ConfigurationCommands.c will be lost.
 *
 * To update ConfigurationCommands.c:
 *   rake nginx
 *
 * To force regeneration of ConfigurationCommands.c:
 *   rm -f src/nginx_module/ConfigurationCommands.c
 *   rake src/nginx_module/ConfigurationCommands.c
 */

{
    ngx_string("passenger_socket_backlog"),
    NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(passenger_main_conf_t, socket_backlog),
    NULL
},
{
    ngx_string("passenger_core_file_descriptor_ulimit"),
    NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(passenger_main_conf_t, core_file_descriptor_ulimit),
    NULL
},
{
    ngx_string("disable_security_update_check"),
    NGX_HTTP_MAIN_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(passenger_main_conf_t, disable_security_update_check),
    NULL
},
{
    ngx_string("security_update_check_proxy"),
    NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(passenger_main_conf_t, security_update_check_proxy),
    NULL
},
{
    ngx_string("passenger_app_file_descriptor_ulimit"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, app_file_descriptor_ulimit),
    NULL
},
{
    ngx_string("passenger_enabled"),
    NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    passenger_enabled,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, enabled),
    NULL
},
{
    ngx_string("passenger_ruby"),
    NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, ruby),
    NULL
},
{
    ngx_string("passenger_python"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, python),
    NULL
},
{
    ngx_string("passenger_nodejs"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, nodejs),
    NULL
},
{
    ngx_string("passenger_meteor_app_settings"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, meteor_app_settings),
    NULL
},
{
    ngx_string("passenger_app_env"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, environment),
    NULL
},
{
    ngx_string("passenger_friendly_error_pages"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, friendly_error_pages),
    NULL
},
{
    ngx_string("passenger_min_instances"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, min_instances),
    NULL
},
{
    ngx_string("passenger_max_instances_per_app"),
    NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, max_instances_per_app),
    NULL
},
{
    ngx_string("passenger_max_requests"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, max_requests),
    NULL
},
{
    ngx_string("passenger_start_timeout"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, start_timeout),
    NULL
},
{
    ngx_string("passenger_base_uri"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_array_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, base_uris),
    NULL
},
{
    ngx_string("passenger_document_root"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, document_root),
    NULL
},
{
    ngx_string("passenger_user"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, user),
    NULL
},
{
    ngx_string("passenger_group"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, group),
    NULL
},
{
    ngx_string("passenger_app_group_name"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, app_group_name),
    NULL
},
{
    ngx_string("passenger_app_root"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, app_root),
    NULL
},
{
    ngx_string("passenger_app_rights"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, app_rights),
    NULL
},
{
    ngx_string("union_station_support"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, union_station_support),
    NULL
},
{
    ngx_string("union_station_filter"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    union_station_filter,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, union_station_filters),
    NULL
},
{
    ngx_string("passenger_debugger"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, debugger),
    NULL
},
{
    ngx_string("passenger_max_preloader_idle_time"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, max_preloader_idle_time),
    NULL
},
{
    ngx_string("passenger_ignore_headers"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_1MORE,
    ngx_conf_set_bitmask_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.ignore_headers),
    &ngx_http_upstream_ignore_headers_masks
},
{
    ngx_string("passenger_env_var"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE2,
    ngx_conf_set_keyval_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, env_vars),
    NULL
},
{
    ngx_string("passenger_set_header"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE2,
    ngx_conf_set_keyval_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, headers_source),
    NULL
},
{
    ngx_string("passenger_pass_header"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_array_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.pass_headers),
    NULL
},
{
    ngx_string("passenger_headers_hash_max_size"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, headers_hash_max_size),
    NULL
},
{
    ngx_string("passenger_headers_hash_bucket_size"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, headers_hash_bucket_size),
    NULL
},
{
    ngx_string("passenger_ignore_client_abort"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.ignore_client_abort),
    NULL
},
{
    ngx_string("passenger_buffer_response"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.buffering),
    NULL
},
{
    ngx_string("passenger_buffer_size"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_size_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.buffer_size),
    NULL
},
{
    ngx_string("passenger_buffers"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE2,
    ngx_conf_set_bufs_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.bufs),
    NULL
},
{
    ngx_string("passenger_busy_buffers_size"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_size_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.busy_buffers_size_conf),
    NULL
},
{
    ngx_string("passenger_intercept_errors"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, upstream_config.intercept_errors),
    NULL
},
{
    ngx_string("passenger_spawn_method"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, spawn_method),
    NULL
},
{
    ngx_string("passenger_load_shell_envvars"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, load_shell_envvars),
    NULL
},
{
    ngx_string("union_station_key"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, union_station_key),
    NULL
},
{
    ngx_string("passenger_max_request_queue_size"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, max_request_queue_size),
    NULL
},
{
    ngx_string("passenger_request_queue_overflow_status_code"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, request_queue_overflow_status_code),
    NULL
},
{
    ngx_string("passenger_restart_dir"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, restart_dir),
    NULL
},
{
    ngx_string("passenger_app_type"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, app_type),
    NULL
},
{
    ngx_string("passenger_startup_file"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, startup_file),
    NULL
},
{
    ngx_string("passenger_sticky_sessions"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, sticky_sessions),
    NULL
},
{
    ngx_string("passenger_sticky_sessions_cookie_name"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, sticky_sessions_cookie_name),
    NULL
},
{
    ngx_string("passenger_vary_turbocache_by_cookie"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, vary_turbocache_by_cookie),
    NULL
},
{
    ngx_string("passenger_abort_websockets_on_process_shutdown"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, abort_websockets_on_process_shutdown),
    NULL
},
{
    ngx_string("passenger_force_max_concurrent_requests_per_process"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, force_max_concurrent_requests_per_process),
    NULL
},
{
    ngx_string("passenger_fly_with"),
    NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
    passenger_enterprise_only,
    NGX_HTTP_MAIN_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_max_instances"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    passenger_enterprise_only,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_max_request_time"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    passenger_enterprise_only,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_memory_limit"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    passenger_enterprise_only,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_concurrency_model"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    passenger_enterprise_only,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_thread_count"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    passenger_enterprise_only,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_rolling_restarts"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    passenger_enterprise_only,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_resist_deployment_errors"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    passenger_enterprise_only,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("rails_spawn_method"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, spawn_method),
    NULL
},
{
    ngx_string("rails_env"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, environment),
    NULL
},
{
    ngx_string("rack_env"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, environment),
    NULL
},
{
    ngx_string("rails_app_spawner_idle_time"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(passenger_loc_conf_t, max_preloader_idle_time),
    NULL
},
{
    ngx_string("rails_framework_spawner_idle_time"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
    rails_framework_spawner_idle_time,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
{
    ngx_string("passenger_use_global_queue"),
    NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
    passenger_use_global_queue,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
},
