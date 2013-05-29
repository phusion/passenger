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


	#define DEFAULT_LOG_LEVEL 0

	#define DEFAULT_RUBY "ruby"

	#define DEFAULT_PYTHON "python"

	#define DEFAULT_MAX_POOL_SIZE 6

	#define DEFAULT_POOL_IDLE_TIME 300

	#define DEFAULT_MAX_INSTANCES_PER_APP 0

	#define DEFAULT_WEB_APP_USER "nobody"

	#define DEFAULT_ANALYTICS_LOG_USER "nobody"

	#define DEFAULT_ANALYTICS_LOG_GROUP ""

	#define DEFAULT_ANALYTICS_LOG_PERMISSIONS "u=rwx,g=rx,o=rx"

	#define DEFAULT_UNION_STATION_GATEWAY_ADDRESS "gateway.unionstationapp.com"

	#define DEFAULT_UNION_STATION_GATEWAY_PORT 443

	#define MESSAGE_SERVER_MAX_USERNAME_SIZE 100

	#define MESSAGE_SERVER_MAX_PASSWORD_SIZE 100

	#define POOL_HELPER_THREAD_STACK_SIZE 262144

	#define PROCESS_SHUTDOWN_TIMEOUT 60

	#define PROCESS_SHUTDOWN_TIMEOUT_DISPLAY "1 minute"

	#define PASSENGER_VERSION "4.0.5"

	#define SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION 1

	#define SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION 0

	#define SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION 2

	#define SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MINOR_VERSION 0

	#define FEEDBACK_FD 3


#endif /* _PASSENGER_CONSTANTS_H */
