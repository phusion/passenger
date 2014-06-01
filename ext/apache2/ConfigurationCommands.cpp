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
 * ConfigurationCommands.cpp is automatically generated from ConfigurationCommands.cpp.erb,
 * using definitions from lib/phusion_passenger/apache2/config_options.rb.
 * Edits to ConfigurationCommands.cpp will be lost.
 *
 * To update ConfigurationCommands.cpp:
 *   rake apache2
 *
 * To force regeneration of ConfigurationCommands.c:
 *   rm -f ext/apache2/ConfigurationCommands.cpp
 *   rake ext/apache2/ConfigurationCommands.cpp
 */




	
	AP_INIT_TAKE1("PassengerRuby",
		(Take1Func) cmd_passenger_ruby,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"The Ruby interpreter to use."),

	
	AP_INIT_TAKE1("PassengerPython",
		(Take1Func) cmd_passenger_python,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"The Python interpreter to use."),

	
	AP_INIT_TAKE1("PassengerNodejs",
		(Take1Func) cmd_passenger_nodejs,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"The Node.js command to use."),

	
	AP_INIT_TAKE1("PassengerAppEnv",
		(Take1Func) cmd_passenger_app_env,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"The environment under which applications are run."),

	
	AP_INIT_TAKE1("RailsEnv",
		(Take1Func) cmd_passenger_app_env,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"The environment under which applications are run."),

	
	AP_INIT_TAKE1("RackEnv",
		(Take1Func) cmd_passenger_app_env,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"The environment under which applications are run."),

	
	AP_INIT_TAKE1("PassengerMinInstances",
		(Take1Func) cmd_passenger_min_instances,
		NULL,
		OR_LIMIT | ACCESS_CONF | RSRC_CONF,
		"The minimum number of application instances to keep when cleaning idle instances."),

	
	AP_INIT_TAKE1("PassengerMaxInstancesPerApp",
		(Take1Func) cmd_passenger_max_instances_per_app,
		NULL,
		RSRC_CONF,
		"The maximum number of simultaneously alive application instances a single application may occupy."),

	
	AP_INIT_TAKE1("PassengerUser",
		(Take1Func) cmd_passenger_user,
		NULL,
		ACCESS_CONF | RSRC_CONF,
		"The user that Ruby applications must run as."),

	
	AP_INIT_TAKE1("PassengerGroup",
		(Take1Func) cmd_passenger_group,
		NULL,
		ACCESS_CONF | RSRC_CONF,
		"The group that Ruby applications must run as."),

	
	AP_INIT_FLAG("PassengerErrorOverride",
		(FlagFunc) cmd_passenger_error_override,
		NULL,
		OR_ALL,
		"Allow Apache to handle error response."),

	
	AP_INIT_TAKE1("PassengerMaxRequests",
		(Take1Func) cmd_passenger_max_requests,
		NULL,
		OR_LIMIT | ACCESS_CONF | RSRC_CONF,
		"The maximum number of requests that an application instance may process."),

	
	AP_INIT_TAKE1("PassengerStartTimeout",
		(Take1Func) cmd_passenger_start_timeout,
		NULL,
		OR_LIMIT | ACCESS_CONF | RSRC_CONF,
		"A timeout for application startup."),

	
	AP_INIT_FLAG("PassengerHighPerformance",
		(FlagFunc) cmd_passenger_high_performance,
		NULL,
		OR_ALL,
		"Enable or disable Passenger's high performance mode."),

	
	AP_INIT_FLAG("PassengerEnabled",
		(FlagFunc) cmd_passenger_enabled,
		NULL,
		OR_ALL,
		"Enable or disable Phusion Passenger."),

	
	AP_INIT_TAKE1("PassengerMaxRequestQueueSize",
		(Take1Func) cmd_passenger_max_request_queue_size,
		NULL,
		OR_ALL,
		"The maximum number of queued requests."),

	
	AP_INIT_FLAG("PassengerLoadShellEnvvars",
		(FlagFunc) cmd_passenger_load_shell_envvars,
		NULL,
		OR_OPTIONS | ACCESS_CONF | RSRC_CONF,
		"Whether to load environment variables from the shell before running the application."),

	
	AP_INIT_FLAG("PassengerBufferUpload",
		(FlagFunc) cmd_passenger_buffer_upload,
		NULL,
		OR_ALL,
		"Whether to buffer file uploads."),

	
	AP_INIT_TAKE1("PassengerAppType",
		(Take1Func) cmd_passenger_app_type,
		NULL,
		OR_ALL,
		"Force specific application type."),

	
	AP_INIT_TAKE1("PassengerStartupFile",
		(Take1Func) cmd_passenger_startup_file,
		NULL,
		OR_ALL,
		"Force specific startup file."),

	
	AP_INIT_FLAG("PassengerStickySessions",
		(FlagFunc) cmd_passenger_sticky_sessions,
		NULL,
		OR_ALL,
		"Whether to enable sticky sessions."),

	
	AP_INIT_FLAG("PassengerStickySessionsCookieName",
		(FlagFunc) cmd_passenger_sticky_sessions_cookie_name,
		NULL,
		OR_ALL,
		"The cookie name to use for sticky sessions."),

