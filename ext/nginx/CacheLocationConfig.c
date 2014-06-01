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
 * CacheLocationConfig.c is automatically generated from CacheLocationConfig.c.erb,
 * using definitions from lib/phusion_passenger/nginx/config_options.rb.
 * Edits to CacheLocationConfig.c will be lost.
 *
 * To update CacheLocationConfig.c:
 *   rake nginx
 *
 * To force regeneration of CacheLocationConfig.c:
 *   rm -f ext/nginx/CacheLocationConfig.c
 *   rake ext/nginx/CacheLocationConfig.c
 */



size_t len = 0;
u_char int_buf[32], *end, *buf, *pos;

/* Calculate lengths */

	
		if (conf->ruby.data != NULL) {
			len += 15;
			len += conf->ruby.len + 1;
		}
	

	
		if (conf->python.data != NULL) {
			len += 17;
			len += conf->python.len + 1;
		}
	

	
		if (conf->nodejs.data != NULL) {
			len += 17;
			len += conf->nodejs.len + 1;
		}
	

	
		if (conf->environment.data != NULL) {
			len += 18;
			len += conf->environment.len + 1;
		}
	

	
		if (conf->friendly_error_pages != NGX_CONF_UNSET) {
			len += 31;
			len += conf->friendly_error_pages ? sizeof("true") : sizeof("false");
		}
	

	
		if (conf->min_instances != NGX_CONF_UNSET) {
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->min_instances);
			len += 24;
			len += end - int_buf + 1;
		}
	

	
		if (conf->max_instances_per_app != NGX_CONF_UNSET) {
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_instances_per_app);
			len += 24;
			len += end - int_buf + 1;
		}
	

	
		if (conf->max_requests != NGX_CONF_UNSET) {
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_requests);
			len += 23;
			len += end - int_buf + 1;
		}
	

	
		if (conf->start_timeout != NGX_CONF_UNSET) {
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->start_timeout);
			len += 24;
			len += end - int_buf + 1;
		}
	

	
		if (conf->user.data != NULL) {
			len += 15;
			len += conf->user.len + 1;
		}
	

	
		if (conf->group.data != NULL) {
			len += 16;
			len += conf->group.len + 1;
		}
	

	
		if (conf->app_group_name.data != NULL) {
			len += 25;
			len += conf->app_group_name.len + 1;
		}
	

	
		if (conf->app_root.data != NULL) {
			len += 19;
			len += conf->app_root.len + 1;
		}
	

	
		if (conf->app_rights.data != NULL) {
			len += 21;
			len += conf->app_rights.len + 1;
		}
	

	
		if (conf->union_station_support != NGX_CONF_UNSET) {
			len += 22;
			len += conf->union_station_support ? sizeof("true") : sizeof("false");
		}
	

	
		if (conf->debugger != NGX_CONF_UNSET) {
			len += 19;
			len += conf->debugger ? sizeof("true") : sizeof("false");
		}
	

	
		if (conf->show_version_in_header != NGX_CONF_UNSET) {
			len += 33;
			len += conf->show_version_in_header ? sizeof("true") : sizeof("false");
		}
	

	
		if (conf->max_preloader_idle_time != NGX_CONF_UNSET) {
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_preloader_idle_time);
			len += 34;
			len += end - int_buf + 1;
		}
	

	
		if (conf->spawn_method.data != NULL) {
			len += 23;
			len += conf->spawn_method.len + 1;
		}
	

	
		if (conf->load_shell_envvars != NGX_CONF_UNSET) {
			len += 29;
			len += conf->load_shell_envvars ? sizeof("true") : sizeof("false");
		}
	

	
		if (conf->union_station_key.data != NULL) {
			len += 18;
			len += conf->union_station_key.len + 1;
		}
	

	
		if (conf->max_request_queue_size != NGX_CONF_UNSET) {
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_request_queue_size);
			len += 33;
			len += end - int_buf + 1;
		}
	

	
		if (conf->request_queue_overflow_status_code != NGX_CONF_UNSET) {
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->request_queue_overflow_status_code);
			len += 45;
			len += end - int_buf + 1;
		}
	

	
		if (conf->restart_dir.data != NULL) {
			len += 22;
			len += conf->restart_dir.len + 1;
		}
	

	
		if (conf->startup_file.data != NULL) {
			len += 23;
			len += conf->startup_file.len + 1;
		}
	

	
		if (conf->sticky_sessions != NGX_CONF_UNSET) {
			len += 26;
			len += conf->sticky_sessions ? sizeof("true") : sizeof("false");
		}
	

	
		if (conf->sticky_sessions_cookie_name.data != NULL) {
			len += 38;
			len += conf->sticky_sessions_cookie_name.len + 1;
		}
	


/* Create string */
buf = pos = ngx_pnalloc(cf->pool, len);


	
		if (conf->ruby.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_RUBY",
				15);
			pos = ngx_copy(pos,
				conf->ruby.data,
				conf->ruby.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->python.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_PYTHON",
				17);
			pos = ngx_copy(pos,
				conf->python.data,
				conf->python.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->nodejs.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_NODEJS",
				17);
			pos = ngx_copy(pos,
				conf->nodejs.data,
				conf->nodejs.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->environment.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_APP_ENV",
				18);
			pos = ngx_copy(pos,
				conf->environment.data,
				conf->environment.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->friendly_error_pages != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_FRIENDLY_ERROR_PAGES",
				31);
			if (conf->friendly_error_pages) {
				pos = ngx_copy(pos, "true", sizeof("true"));
			} else {
				pos = ngx_copy(pos, "false", sizeof("false"));
			}
		}
	

	
		if (conf->min_instances != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_MIN_PROCESSES",
				24);
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->min_instances);
			pos = ngx_copy(pos,
				int_buf,
				end - int_buf);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->max_instances_per_app != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_MAX_PROCESSES",
				24);
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_instances_per_app);
			pos = ngx_copy(pos,
				int_buf,
				end - int_buf);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->max_requests != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_MAX_REQUESTS",
				23);
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_requests);
			pos = ngx_copy(pos,
				int_buf,
				end - int_buf);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->start_timeout != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_START_TIMEOUT",
				24);
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->start_timeout);
			pos = ngx_copy(pos,
				int_buf,
				end - int_buf);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->user.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_USER",
				15);
			pos = ngx_copy(pos,
				conf->user.data,
				conf->user.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->group.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_GROUP",
				16);
			pos = ngx_copy(pos,
				conf->group.data,
				conf->group.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->app_group_name.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_APP_GROUP_NAME",
				25);
			pos = ngx_copy(pos,
				conf->app_group_name.data,
				conf->app_group_name.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->app_root.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_APP_ROOT",
				19);
			pos = ngx_copy(pos,
				conf->app_root.data,
				conf->app_root.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->app_rights.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_APP_RIGHTS",
				21);
			pos = ngx_copy(pos,
				conf->app_rights.data,
				conf->app_rights.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->union_station_support != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"UNION_STATION_SUPPORT",
				22);
			if (conf->union_station_support) {
				pos = ngx_copy(pos, "true", sizeof("true"));
			} else {
				pos = ngx_copy(pos, "false", sizeof("false"));
			}
		}
	

	
		if (conf->debugger != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_DEBUGGER",
				19);
			if (conf->debugger) {
				pos = ngx_copy(pos, "true", sizeof("true"));
			} else {
				pos = ngx_copy(pos, "false", sizeof("false"));
			}
		}
	

	
		if (conf->show_version_in_header != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_SHOW_VERSION_IN_HEADER",
				33);
			if (conf->show_version_in_header) {
				pos = ngx_copy(pos, "true", sizeof("true"));
			} else {
				pos = ngx_copy(pos, "false", sizeof("false"));
			}
		}
	

	
		if (conf->max_preloader_idle_time != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_MAX_PRELOADER_IDLE_TIME",
				34);
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_preloader_idle_time);
			pos = ngx_copy(pos,
				int_buf,
				end - int_buf);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->spawn_method.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_SPAWN_METHOD",
				23);
			pos = ngx_copy(pos,
				conf->spawn_method.data,
				conf->spawn_method.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->load_shell_envvars != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_LOAD_SHELL_ENVVARS",
				29);
			if (conf->load_shell_envvars) {
				pos = ngx_copy(pos, "true", sizeof("true"));
			} else {
				pos = ngx_copy(pos, "false", sizeof("false"));
			}
		}
	

	
		if (conf->union_station_key.data != NULL) {
			pos = ngx_copy(pos,
				"UNION_STATION_KEY",
				18);
			pos = ngx_copy(pos,
				conf->union_station_key.data,
				conf->union_station_key.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->max_request_queue_size != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_MAX_REQUEST_QUEUE_SIZE",
				33);
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->max_request_queue_size);
			pos = ngx_copy(pos,
				int_buf,
				end - int_buf);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->request_queue_overflow_status_code != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_REQUEST_QUEUE_OVERFLOW_STATUS_CODE",
				45);
			end = ngx_snprintf(int_buf,
				sizeof(int_buf) - 1,
				"%d",
				conf->request_queue_overflow_status_code);
			pos = ngx_copy(pos,
				int_buf,
				end - int_buf);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->restart_dir.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_RESTART_DIR",
				22);
			pos = ngx_copy(pos,
				conf->restart_dir.data,
				conf->restart_dir.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->startup_file.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_STARTUP_FILE",
				23);
			pos = ngx_copy(pos,
				conf->startup_file.data,
				conf->startup_file.len);
			*pos = '\0';
			pos++;
		}
	

	
		if (conf->sticky_sessions != NGX_CONF_UNSET) {
			pos = ngx_copy(pos,
				"PASSENGER_STICKY_SESSIONS",
				26);
			if (conf->sticky_sessions) {
				pos = ngx_copy(pos, "true", sizeof("true"));
			} else {
				pos = ngx_copy(pos, "false", sizeof("false"));
			}
		}
	

	
		if (conf->sticky_sessions_cookie_name.data != NULL) {
			pos = ngx_copy(pos,
				"PASSENGER_STICKY_SESSIONS_COOKIE_NAME",
				38);
			pos = ngx_copy(pos,
				conf->sticky_sessions_cookie_name.data,
				conf->sticky_sessions_cookie_name.len);
			*pos = '\0';
			pos++;
		}
	


conf->options_cache.data = buf;
conf->options_cache.len = pos - buf;
