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
#include <oxt/thread.hpp>
#include "AgentsStarter.h"
#include "AgentsStarter.hpp"
#include <cerrno>
#include <cstring>

using namespace std;
using namespace boost;
using namespace oxt;


AgentsStarter *
agents_starter_new(AgentsStarterType type, char **error_message) {
	Passenger::AgentsStarter::Type theType;
	if (type == AS_APACHE) {
		theType = Passenger::AgentsStarter::APACHE;
	} else {
		theType = Passenger::AgentsStarter::NGINX;
	}
	return (AgentsStarter *) new Passenger::AgentsStarter(theType);
}

int
agents_starter_start(AgentsStarter *as,
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
                     char **errorMessage)
{
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	this_thread::disable_syscall_interruption dsi;
	try {
		function<void ()> afterForkFunctionObject;
		set<string> setOfprestartURLs;
		unsigned int i;
		
		if (afterFork != NULL) {
			afterForkFunctionObject = boost::bind(afterFork, callbackArgument);
		}
		for (i = 0; i < prestartURLsCount; i++) {
			setOfprestartURLs.insert(prestartURLs[i]);
		}
		agentsStarter->start(logLevel, debugLogFile,
			webServerPid, tempDir, userSwitching,
			defaultUser, defaultGroup,
			webServerWorkerUid, webServerWorkerGid,
			passengerRoot, rubyCommand,
			maxPoolSize, maxInstancesPerApp, poolIdleTime,
			analyticsServer,
			analyticsLogDir, analyticsLogUser,
			analyticsLogGroup, analyticsLogPermissions,
			unionStationGatewayAddress,
			unionStationGatewayPort,
			unionStationGatewayCert,
			setOfprestartURLs,
			afterForkFunctionObject);
		return 1;
	} catch (const Passenger::SystemException &e) {
		errno = e.code();
		*errorMessage = strdup(e.what());
		return 0;
	} catch (const std::exception &e) {
		errno = -1;
		*errorMessage = strdup(e.what());
		return 0;
	}
}

const char *
agents_starter_get_request_socket_filename(AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketFilename().size();
	}
	return agentsStarter->getRequestSocketFilename().c_str();
}

const char *
agents_starter_get_request_socket_password(AgentsStarter *as, unsigned int *size) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	if (size != NULL) {
		*size = agentsStarter->getRequestSocketPassword().size();
	}
	return agentsStarter->getRequestSocketPassword().c_str();
}

const char *
agents_starter_get_server_instance_dir(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getServerInstanceDir()->getPath().c_str();
}

const char *
agents_starter_get_generation_dir(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getGeneration()->getPath().c_str();
}

pid_t
agents_starter_get_pid(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	return agentsStarter->getPid();
}

void
agents_starter_detach(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	agentsStarter->detach();
}

void
agents_starter_free(AgentsStarter *as) {
	Passenger::AgentsStarter *agentsStarter = (Passenger::AgentsStarter *) as;
	delete agentsStarter;
}
