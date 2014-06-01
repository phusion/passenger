/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
 * ConfigurationFields.h is automatically generated from ConfigurationFields.h.erb,
 * using definitions from lib/phusion_passenger/nginx/config_options.rb.
 * Edits to ConfigurationFields.h will be lost.
 *
 * To update ConfigurationFields.h:
 *   rake nginx
 *
 * To force regeneration of ConfigurationFields.h:
 *   rm -f ext/nginx/ConfigurationFields.h
 *   rake ext/nginx/ConfigurationFields.h
 */




	ngx_array_t *base_uris;

	ngx_int_t debugger;

	ngx_int_t enabled;

	ngx_int_t friendly_error_pages;

	ngx_int_t load_shell_envvars;

	ngx_int_t max_instances_per_app;

	ngx_int_t max_preloader_idle_time;

	ngx_int_t max_request_queue_size;

	ngx_int_t max_requests;

	ngx_int_t min_instances;

	ngx_int_t request_queue_overflow_status_code;

	ngx_int_t show_version_in_header;

	ngx_int_t start_timeout;

	ngx_int_t sticky_sessions;

	ngx_array_t *union_station_filters;

	ngx_int_t union_station_support;

	ngx_array_t *vars_source;

	ngx_str_t app_group_name;

	ngx_str_t app_rights;

	ngx_str_t app_root;

	ngx_str_t app_type;

	ngx_str_t document_root;

	ngx_str_t environment;

	ngx_str_t group;

	ngx_str_t nodejs;

	ngx_str_t python;

	ngx_str_t restart_dir;

	ngx_str_t ruby;

	ngx_str_t spawn_method;

	ngx_str_t startup_file;

	ngx_str_t sticky_sessions_cookie_name;

	ngx_str_t union_station_key;

	ngx_str_t user;

