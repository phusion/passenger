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
#ifndef _PASSENGER_CONSTANTS_H_
#define _PASSENGER_CONSTANTS_H_

/* Constants.h is automatically generated from Constants.h.erb by the build system.
 * Most constants are derived from lib/phusion_passenger/constants.rb.
 *
 * To force regenerating this file:
 *   rm -f ext/common/Constants.h
 *   rake ext/common/Constants.h
 */

#define DEFAULT_BACKEND_ACCOUNT_RIGHTS Account::DETACH


	#define APACHE2_DOC_URL "https://www.phusionpassenger.com/documentation/Users%20guide%20Apache.html"

	#define DEB_APACHE_MODULE_PACKAGE "libapache2-mod-passenger"

	#define DEB_DEV_PACKAGE "passenger-dev"

	#define DEB_MAIN_PACKAGE "passenger"

	#define DEB_NGINX_PACKAGE "nginx-extras"

	#define DEFAULT_ANALYTICS_LOG_GROUP ""

	#define DEFAULT_ANALYTICS_LOG_PERMISSIONS "u=rwx,g=rx,o=rx"

	#define DEFAULT_ANALYTICS_LOG_USER "nobody"

	#define DEFAULT_CONCURRENCY_MODEL "process"

	#define DEFAULT_LOG_LEVEL 0

	#define DEFAULT_MAX_POOL_SIZE 6

	#define DEFAULT_NODEJS "node"

	#define DEFAULT_POOL_IDLE_TIME 300

	#define DEFAULT_PYTHON "python"

	#define DEFAULT_RUBY "ruby"

	#define DEFAULT_START_TIMEOUT 90000

	#define DEFAULT_STICKY_SESSIONS_COOKIE_NAME "_passenger_route"

	#define DEFAULT_THREAD_COUNT 1

	#define DEFAULT_UNION_STATION_GATEWAY_ADDRESS "gateway.unionstationapp.com"

	#define DEFAULT_UNION_STATION_GATEWAY_PORT 443

	#define DEFAULT_WEB_APP_USER "nobody"

	#define ENTERPRISE_URL "https://www.phusionpassenger.com/enterprise"

	#define FEEDBACK_FD 3

	#define INDEX_DOC_URL "https://www.phusionpassenger.com/documentation/Users%20guide.html"

	#define MESSAGE_SERVER_MAX_PASSWORD_SIZE 100

	#define MESSAGE_SERVER_MAX_USERNAME_SIZE 100

	#define NGINX_DOC_URL "https://www.phusionpassenger.com/documentation/Users%20guide%20Nginx.html"

	#define PASSENGER_VERSION "4.0.50"

	#define POOL_HELPER_THREAD_STACK_SIZE 262144

	#define PROCESS_SHUTDOWN_TIMEOUT 60

	#define PROCESS_SHUTDOWN_TIMEOUT_DISPLAY "1 minute"

	#define PROGRAM_NAME "Phusion Passenger"

	#define RPM_APACHE_MODULE_PACKAGE "mod_passenger"

	#define RPM_DEV_PACKAGE "passenger-devel"

	#define RPM_MAIN_PACKAGE "passenger"

	#define RPM_NGINX_PACKAGE "nginx"

	#define SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION 3

	#define SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MINOR_VERSION 0

	#define SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION 1

	#define SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION 0

	#define STANDALONE_DOC_URL "https://www.phusionpassenger.com/documentation/Users%20guide%20Standalone.html"

	#define STANDALONE_NGINX_CONFIGURE_OPTIONS "--with-cc-opt='-Wno-error' --without-http_fastcgi_module --without-http_scgi_module --without-http_uwsgi_module --with-http_gzip_static_module --with-http_stub_status_module --with-http_ssl_module"

	#define SUPPORT_URL "https://www.phusionpassenger.com/documentation_and_support"


#endif /* _PASSENGER_CONSTANTS_H */
