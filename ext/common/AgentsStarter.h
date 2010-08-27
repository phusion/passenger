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

/* C wrappers for Passenger::AgentsStarter. */
#ifndef _PASSENGER_AGENTS_STARTER_H_
#define _PASSENGER_AGENTS_STARTER_H_

#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
	extern "C" {
#endif


typedef void AgentsStarter;

typedef enum {
	AS_APACHE,
	AS_NGINX
} AgentsStarterType;

typedef void (*AfterForkCallback)(void *);

AgentsStarter *agents_starter_new(AgentsStarterType type, char **error_message);
int  agents_starter_start(AgentsStarter *as,
                          int logLevel, const char *debugLogFile,
                          pid_t webServerPid,
                          const char *tempDir, int userSwitching,
                          const char *defaultUser, const char *defaultGroup,
                          uid_t webServerWorkerUid, gid_t webServerWorkerGid,
                          const char *passengerRoot,
                          const char *rubyCommand, unsigned int maxPoolSize,
                          unsigned int maxInstancesPerApp,
                          unsigned int poolIdleTime,
                          const char *analyticsServer,
                          const char *analyticsLogDir, const char *analyticsLogUser,
                          const char *analyticsLogGroup, const char *analyticsLogPermissions,
                          const char *unionStationGatewayAddress,
                          unsigned short unionStationGatewayPort,
                          const char *unionStationGatewayCert,
                          const char **prestartURLs, unsigned int prestartURLsCount,
                          const AfterForkCallback afterFork,
                          void *callbackArgument,
                          char **errorMessage);
const char *agents_starter_get_request_socket_filename(AgentsStarter *as, unsigned int *size);
const char *agents_starter_get_request_socket_password(AgentsStarter *as, unsigned int *size);
const char *agents_starter_get_server_instance_dir(AgentsStarter *as);
const char *agents_starter_get_generation_dir(AgentsStarter *as);
pid_t       agents_starter_get_pid(AgentsStarter *as);
void        agents_starter_detach(AgentsStarter *as);
void        agents_starter_free(AgentsStarter *as);


#ifdef __cplusplus
	}
#endif

#endif /* _PASSENGER_AGENTS_STARTER_H_ */
