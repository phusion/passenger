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
 * ConfigurationFields.hpp is automatically generated from ConfigurationFields.hpp.erb,
 * using definitions from lib/phusion_passenger/apache2/config_options.rb.
 * Edits to ConfigurationFields.hpp will be lost.
 *
 * To update ConfigurationFields.hpp:
 *   rake apache2
 *
 * To force regeneration of ConfigurationFields.hpp:
 *   rm -f ext/apache2/ConfigurationFields.hpp
 *   rake ext/apache2/ConfigurationFields.hpp
 */



	/** Whether to buffer file uploads. */
	Threeway bufferUpload;
	/** Enable or disable Phusion Passenger. */
	Threeway enabled;
	/** Allow Apache to handle error response. */
	Threeway errorOverride;
	/** Enable or disable Passenger's high performance mode. */
	Threeway highPerformance;
	/** Whether to load environment variables from the shell before running the application. */
	Threeway loadShellEnvvars;
	/** Whether to enable sticky sessions. */
	Threeway stickySessions;
	/** The cookie name to use for sticky sessions. */
	Threeway stickySessionsCookieName;
	/** The maximum number of simultaneously alive application instances a single application may occupy. */
	int maxInstancesPerApp;
	/** The maximum number of queued requests. */
	int maxRequestQueueSize;
	/** The maximum number of requests that an application instance may process. */
	int maxRequests;
	/** The minimum number of application instances to keep when cleaning idle instances. */
	int minInstances;
	/** A timeout for application startup. */
	int startTimeout;
	/** The environment under which applications are run. */
	const char *appEnv;
	/** Force specific application type. */
	const char *appType;
	/** The group that Ruby applications must run as. */
	const char *group;
	/** The Node.js command to use. */
	const char *nodejs;
	/** The Python interpreter to use. */
	const char *python;
	/** The Ruby interpreter to use. */
	const char *ruby;
	/** Force specific startup file. */
	const char *startupFile;
	/** The user that Ruby applications must run as. */
	const char *user;

