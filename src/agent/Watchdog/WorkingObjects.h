/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2026 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#ifndef _PASSENGER_WATCHDOG_WORKING_OBJECTS_H_
#define _PASSENGER_WATCHDOG_WORKING_OBJECTS_H_

#include <vector>
#include <string>
#include <jsoncpp/json.h>

#include <RandomGenerator.h>
#include <InstanceDirectory.h>
#include <BackgroundEventLoop.h>
#include <WrapperRegistry/Registry.h>
#include <ServerKit/Context.h>

#include <Watchdog/Config.h>
#include <Watchdog/ApiServer.h>

namespace Passenger {
namespace Watchdog {

using namespace std;


struct WorkingObjects {
	RandomGenerator randomGenerator;
	EventFd errorEvent;
	EventFd exitEvent;
	uid_t defaultUid;
	gid_t defaultGid;
	InstanceDirectoryPtr instanceDir;
	int startupReportFile;
	int lockFile;
	vector<string> cleanupPidfiles;
	bool pidsCleanedUp;
	bool pidFileCleanedUp;
	string corePidFile;
	string fdPassingPassword;
	Json::Value extraConfigToPassToSubAgents;
	Json::Value controllerAddresses;
	Json::Value coreApiServerAddresses;
	Json::Value coreApiServerAuthorizations;
	Json::Value watchdogApiServerAddresses;
	Json::Value watchdogApiServerAuthorizations;

	int apiServerFds[SERVER_KIT_MAX_SERVER_ENDPOINTS];
	BackgroundEventLoop *bgloop;
	ServerKit::Context *serverKitContext;
	ServerKit::Schema serverKitSchema;
	ApiServer::ApiServer *apiServer;

	WorkingObjects()
		: errorEvent(__FILE__, __LINE__, "WorkingObjects: errorEvent"),
			exitEvent(__FILE__, __LINE__, "WorkingObjects: exitEvent"),
			startupReportFile(-1),
			pidsCleanedUp(false),
			pidFileCleanedUp(false),
			extraConfigToPassToSubAgents(Json::objectValue),
			controllerAddresses(Json::arrayValue),
			coreApiServerAddresses(Json::arrayValue),
			coreApiServerAuthorizations(Json::arrayValue),
			watchdogApiServerAddresses(Json::arrayValue),
			watchdogApiServerAuthorizations(Json::arrayValue),
			bgloop(NULL),
			serverKitContext(NULL),
			apiServer(NULL)
	{
		for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
			apiServerFds[i] = -1;
		}
	}
};

typedef boost::shared_ptr<WorkingObjects> WorkingObjectsPtr;

static WrapperRegistry::Registry *watchdogWrapperRegistry;
static Schema *watchdogSchema;
static ConfigKit::Store *watchdogConfig;
static WorkingObjects *workingObjects;


} // namespace Watchdog
} // namespace Passenger

#endif /* _PASSENGER_WATCHDOG_WORKING_OBJECTS_H_ */
