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
 * SetHeaders.cpp is automatically generated from SetHeaders.cpp.erb,
 * using definitions from lib/phusion_passenger/apache2/config_options.rb.
 * Edits to SetHeaders.cpp will be lost.
 *
 * To update SetHeaders.cpp:
 *   rake apache2
 *
 * To force regeneration of SetHeaders.cpp:
 *   rm -f ext/apache2/SetHeaders.cpp
 *   rake ext/apache2/SetHeaders.cpp
 */




	
		addHeader(output, "PASSENGER_RUBY", config->ruby ? config->ruby : serverConfig.defaultRuby);
	

	
		addHeader(output, "PASSENGER_PYTHON", config->python);
	

	
		addHeader(output, "PASSENGER_NODEJS", config->nodejs);
	

	
		addHeader(output, "PASSENGER_APP_ENV", config->appEnv);
	

	
		addHeader(r, output, "PASSENGER_MIN_PROCESSES", config->minInstances);
	

	
		addHeader(r, output, "PASSENGER_MAX_PROCESSES", config->maxInstancesPerApp);
	

	
		addHeader(output, "PASSENGER_USER", config->user);
	

	
		addHeader(output, "PASSENGER_GROUP", config->group);
	

	
		addHeader(r, output, "PASSENGER_MAX_REQUESTS", config->maxRequests);
	

	
		addHeader(r, output, "PASSENGER_START_TIMEOUT", config->startTimeout);
	

	
		addHeader(r, output, "PASSENGER_MAX_REQUEST_QUEUE_SIZE", config->maxRequestQueueSize);
	

	
		addHeader(r, output, "PASSENGER_LOAD_SHELL_ENVVARS", config->loadShellEnvvars);
	

	
		addHeader(output, "PASSENGER_STARTUP_FILE", config->startupFile);
	

	
		addHeader(r, output, "PASSENGER_STICKY_SESSIONS", config->stickySessions);
	

	
		addHeader(r, output, "PASSENGER_STICKY_SESSIONS_COOKIE_NAME", config->stickySessionsCookieName);
	

