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
 * MergeLocationConfig.c is automatically generated from MergeLocationConfig.c.erb,
 * using definitions from lib/phusion_passenger/nginx/config_options.rb.
 * Edits to MergeLocationConfig.c will be lost.
 *
 * To update MergeLocationConfig.c:
 *   rake nginx
 *
 * To force regeneration of MergeLocationConfig.c:
 *   rm -f ext/nginx/MergeLocationConfig.c
 *   rake ext/nginx/MergeLocationConfig.c
 */




	
		ngx_conf_merge_value(conf->enabled,
			prev->enabled,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_str_value(conf->ruby,
			prev->ruby,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->python,
			prev->python,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->nodejs,
			prev->nodejs,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->environment,
			prev->environment,
			NULL);
	

	
		ngx_conf_merge_value(conf->friendly_error_pages,
			prev->friendly_error_pages,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->min_instances,
			prev->min_instances,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->max_instances_per_app,
			prev->max_instances_per_app,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->max_requests,
			prev->max_requests,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->start_timeout,
			prev->start_timeout,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_str_value(conf->document_root,
			prev->document_root,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->user,
			prev->user,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->group,
			prev->group,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->app_group_name,
			prev->app_group_name,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->app_root,
			prev->app_root,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->app_rights,
			prev->app_rights,
			NULL);
	

	
		ngx_conf_merge_value(conf->union_station_support,
			prev->union_station_support,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->debugger,
			prev->debugger,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->show_version_in_header,
			prev->show_version_in_header,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->max_preloader_idle_time,
			prev->max_preloader_idle_time,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_str_value(conf->spawn_method,
			prev->spawn_method,
			NULL);
	

	
		ngx_conf_merge_value(conf->load_shell_envvars,
			prev->load_shell_envvars,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_str_value(conf->union_station_key,
			prev->union_station_key,
			NULL);
	

	
		ngx_conf_merge_value(conf->max_request_queue_size,
			prev->max_request_queue_size,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_value(conf->request_queue_overflow_status_code,
			prev->request_queue_overflow_status_code,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_str_value(conf->restart_dir,
			prev->restart_dir,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->app_type,
			prev->app_type,
			NULL);
	

	
		ngx_conf_merge_str_value(conf->startup_file,
			prev->startup_file,
			NULL);
	

	
		ngx_conf_merge_value(conf->sticky_sessions,
			prev->sticky_sessions,
			NGX_CONF_UNSET);
	

	
		ngx_conf_merge_str_value(conf->sticky_sessions_cookie_name,
			prev->sticky_sessions_cookie_name,
			NULL);
	

