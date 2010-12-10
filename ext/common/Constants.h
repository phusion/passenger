/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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

/* Don't forget to update lib/phusion_passenger.rb too. */
#define PASSENGER_VERSION "3.0.1"

#define FEEDBACK_FD 3

#define DEFAULT_LOG_LEVEL 0
#define DEFAULT_MAX_POOL_SIZE 6
#define DEFAULT_POOL_IDLE_TIME 300
#define DEFAULT_MAX_INSTANCES_PER_APP 0
#define DEFAULT_WEB_APP_USER "nobody"
#define DEFAULT_ANALYTICS_LOG_USER DEFAULT_WEB_APP_USER
#define DEFAULT_ANALYTICS_LOG_GROUP ""
#define DEFAULT_ANALYTICS_LOG_PERMISSIONS "u=rwx,g=rx,o=rx"
#define DEFAULT_UNION_STATION_GATEWAY_ADDRESS "gateway.unionstationapp.com"
#define DEFAULT_UNION_STATION_GATEWAY_PORT 443

#define MESSAGE_SERVER_MAX_USERNAME_SIZE 100
#define MESSAGE_SERVER_MAX_PASSWORD_SIZE 100
#define DEFAULT_BACKEND_ACCOUNT_RIGHTS Account::DETACH

#endif /* _PASSENGER_CONSTANTS_H */
