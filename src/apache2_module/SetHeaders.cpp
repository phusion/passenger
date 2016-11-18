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
 * SetHeaders.cpp is automatically generated from SetHeaders.cpp.cxxcodebuilder,
 * using definitions from src/ruby_supportlib/phusion_passenger/apache2/config_options.rb.
 * Edits to SetHeaders.cpp will be lost.
 *
 * To update SetHeaders.cpp:
 *   rake apache2
 *
 * To force regeneration of SetHeaders.cpp:
 *   rm -f src/apache2_module/SetHeaders.cpp
 *   rake src/apache2_module/SetHeaders.cpp
 */

addHeader(result, StaticString("!~PASSENGER_RUBY",
		sizeof("!~PASSENGER_RUBY") - 1),
	config->ruby ? config->ruby : serverConfig.defaultRuby);
addHeader(result, StaticString("!~PASSENGER_PYTHON",
		sizeof("!~PASSENGER_PYTHON") - 1),
	config->python);
addHeader(result, StaticString("!~PASSENGER_NODEJS",
		sizeof("!~PASSENGER_NODEJS") - 1),
	config->nodejs);
addHeader(result, StaticString("!~PASSENGER_METEOR_APP_SETTINGS",
		sizeof("!~PASSENGER_METEOR_APP_SETTINGS") - 1),
	config->meteorAppSettings);
addHeader(result, StaticString("!~PASSENGER_APP_ENV",
		sizeof("!~PASSENGER_APP_ENV") - 1),
	config->appEnv);
addHeader(r, result, StaticString("!~PASSENGER_MIN_PROCESSES",
		sizeof("!~PASSENGER_MIN_PROCESSES") - 1),
	config->minInstances);
addHeader(r, result, StaticString("!~PASSENGER_MAX_PROCESSES",
		sizeof("!~PASSENGER_MAX_PROCESSES") - 1),
	config->maxInstancesPerApp);
addHeader(result, StaticString("!~PASSENGER_USER",
		sizeof("!~PASSENGER_USER") - 1),
	config->user);
addHeader(result, StaticString("!~PASSENGER_GROUP",
		sizeof("!~PASSENGER_GROUP") - 1),
	config->group);
addHeader(r, result, StaticString("!~PASSENGER_MAX_REQUESTS",
		sizeof("!~PASSENGER_MAX_REQUESTS") - 1),
	config->maxRequests);
addHeader(r, result, StaticString("!~PASSENGER_START_TIMEOUT",
		sizeof("!~PASSENGER_START_TIMEOUT") - 1),
	config->startTimeout);
addHeader(r, result, StaticString("!~PASSENGER_MAX_REQUEST_QUEUE_SIZE",
		sizeof("!~PASSENGER_MAX_REQUEST_QUEUE_SIZE") - 1),
	config->maxRequestQueueSize);
addHeader(r, result, StaticString("!~PASSENGER_MAX_PRELOADER_IDLE_TIME",
		sizeof("!~PASSENGER_MAX_PRELOADER_IDLE_TIME") - 1),
	config->maxPreloaderIdleTime);
addHeader(result, StaticString("!~PASSENGER_LOAD_SHELL_ENVVARS",
		sizeof("!~PASSENGER_LOAD_SHELL_ENVVARS") - 1),
	config->loadShellEnvvars);
addHeader(result, StaticString("!~PASSENGER_STARTUP_FILE",
		sizeof("!~PASSENGER_STARTUP_FILE") - 1),
	config->startupFile);
addHeader(result, StaticString("!~PASSENGER_STICKY_SESSIONS",
		sizeof("!~PASSENGER_STICKY_SESSIONS") - 1),
	config->stickySessions);
addHeader(result, StaticString("!~PASSENGER_STICKY_SESSIONS_COOKIE_NAME",
		sizeof("!~PASSENGER_STICKY_SESSIONS_COOKIE_NAME") - 1),
	config->stickySessionsCookieName);
addHeader(result, StaticString("!~PASSENGER_SPAWN_METHOD",
		sizeof("!~PASSENGER_SPAWN_METHOD") - 1),
	config->spawnMethod);
addHeader(result, StaticString("!~PASSENGER_SHOW_VERSION_IN_HEADER",
		sizeof("!~PASSENGER_SHOW_VERSION_IN_HEADER") - 1),
	config->showVersionInHeader);
addHeader(result, StaticString("!~PASSENGER_FRIENDLY_ERROR_PAGES",
		sizeof("!~PASSENGER_FRIENDLY_ERROR_PAGES") - 1),
	config->friendlyErrorPages);
addHeader(result, StaticString("!~PASSENGER_RESTART_DIR",
		sizeof("!~PASSENGER_RESTART_DIR") - 1),
	config->restartDir);
addHeader(result, StaticString("!~PASSENGER_APP_GROUP_NAME",
		sizeof("!~PASSENGER_APP_GROUP_NAME") - 1),
	config->appGroupName);
addHeader(r, result, StaticString("!~PASSENGER_FORCE_MAX_CONCURRENT_REQUESTS_PER_PROCESS",
		sizeof("!~PASSENGER_FORCE_MAX_CONCURRENT_REQUESTS_PER_PROCESS") - 1),
	config->forceMaxConcurrentRequestsPerProcess);
addHeader(r, result, StaticString("!~PASSENGER_LVE_MIN_UID",
		sizeof("!~PASSENGER_LVE_MIN_UID") - 1),
	config->lveMinUid);
