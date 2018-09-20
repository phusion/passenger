/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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

#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <vector>
#include <cassert>

#include <LoggingKit/LoggingKit.h>
#include <Core/ConfigChange.h>
#include <Core/Config.h>

namespace Passenger {
namespace Core {

using namespace std;


struct ConfigChangeRequest {
	Json::Value updates;
	PrepareConfigChangeCallback prepareCallback;
	CommitConfigChangeCallback commitCallback;
	unsigned int counter;
	vector<ConfigKit::Error> errors;

	boost::scoped_ptr<ConfigKit::Store> config;
	LoggingKit::ConfigChangeRequest forLoggingKit;
	SecurityUpdateChecker::ConfigChangeRequest forSecurityUpdateChecker;
	TelemetryCollector::ConfigChangeRequest forTelemetryCollector;
	vector<ServerKit::ConfigChangeRequest *> forControllerServerKit;
	vector<Controller::ConfigChangeRequest *> forController;
	ServerKit::ConfigChangeRequest forApiServerKit;
	ApiServer::ConfigChangeRequest forApiServer;
	AdminPanelConnector::ConfigChangeRequest forAdminPanelConnector;

	ConfigChangeRequest()
		: counter(0)
		{ }

	~ConfigChangeRequest() {
		{
			vector<ServerKit::ConfigChangeRequest *>::iterator it;
			for (it = forControllerServerKit.begin(); it != forControllerServerKit.end(); it++) {
				delete *it;
			}
		}
		{
			vector<Controller::ConfigChangeRequest *>::iterator it;
			for (it = forController.begin(); it != forController.end(); it++) {
				delete *it;
			}
		}
	}
};


/**************** Functions: prepare config change ****************/


static void
asyncPrepareConfigChangeCompletedOne(ConfigChangeRequest *req) {
	assert(req->counter > 0);
	req->counter--;
	if (req->counter == 0) {
		req->errors = ConfigKit::deduplicateErrors(req->errors);
		if (req->errors.empty()) {
			P_INFO("Changing configuration: " << req->updates.toStyledString());
		} else {
			P_ERROR("Error changing configuration: " << ConfigKit::toString(req->errors)
				<< "\nThe proposed configuration was: " << req->updates.toStyledString());
		}
		oxt::thread(boost::bind(req->prepareCallback, req->errors, req),
			"Core config callback thread",
			128 * 1024);
	}
}

static void
asyncPrepareConfigChangeForController(unsigned int i, const Json::Value &updates, ConfigChangeRequest *req) {
	ThreadWorkingObjects *two = &workingObjects->threadWorkingObjects[i];
	vector<ConfigKit::Error> errors1, errors2;

	req->forControllerServerKit[i] = new ServerKit::ConfigChangeRequest();
	ConfigKit::prepareConfigChangeForSubComponent(
		*two->serverKitContext, coreSchema->controllerServerKit.translator,
		req->config->inspectEffectiveValues(),
		errors1, *req->forControllerServerKit[i]);

	req->forController[i] = new Controller::ConfigChangeRequest();
	ConfigKit::prepareConfigChangeForSubComponent(
		*two->controller, coreSchema->controller.translator,
		req->config->inspectEffectiveValues(),
		errors2, *req->forController[i]);

	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);
	P_DEBUG("asyncPrepareConfigChangeForController(" << i << "): counter "
		<< req->counter << " -> " << (req->counter - 1));
	req->errors.insert(req->errors.begin(), errors1.begin(), errors1.end());
	req->errors.insert(req->errors.begin(), errors2.begin(), errors2.end());
	asyncPrepareConfigChangeCompletedOne(req);
}

static void
asyncPrepareConfigChangeForApiServer(const Json::Value &updates, ConfigChangeRequest *req) {
	vector<ConfigKit::Error> errors1, errors2;

	ConfigKit::prepareConfigChangeForSubComponent(
		*workingObjects->apiWorkingObjects.serverKitContext,
		coreSchema->apiServerKit.translator,
		req->config->inspectEffectiveValues(),
		errors1, req->forApiServerKit);
	ConfigKit::prepareConfigChangeForSubComponent(
		*workingObjects->apiWorkingObjects.apiServer,
		coreSchema->apiServer.translator,
		req->config->inspectEffectiveValues(),
		errors2, req->forApiServer);

	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);
	P_DEBUG("asyncPrepareConfigChangeForApiServer: counter "
		<< req->counter << " -> " << (req->counter - 1));
	req->errors.insert(req->errors.begin(), errors1.begin(), errors1.end());
	req->errors.insert(req->errors.begin(), errors2.begin(), errors2.end());
	asyncPrepareConfigChangeCompletedOne(req);
}

static void
asyncPrepareConfigChangeForAdminPanelConnectorDone(const vector<ConfigKit::Error> &errors,
	AdminPanelConnector::ConfigChangeRequest &_, ConfigChangeRequest *req)
{
	vector<ConfigKit::Error> translatedErrors = coreSchema->adminPanelConnector.translator.reverseTranslate(errors);
	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);
	P_DEBUG("asyncPrepareConfigChangeForAdminPanelConnectorDone: counter "
		<< req->counter << " -> " << (req->counter - 1));
	req->errors.insert(req->errors.begin(), translatedErrors.begin(), translatedErrors.end());
	asyncPrepareConfigChangeCompletedOne(req);
}

//
//

void
asyncPrepareConfigChange(const Json::Value &updates, ConfigChangeRequest *req,
	const PrepareConfigChangeCallback &callback)
{
	P_DEBUG("Preparing configuration change: " << updates.toStyledString());
	WorkingObjects *wo = workingObjects;
	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);

	req->updates = updates;
	req->prepareCallback = callback;
	req->counter++;

	req->config.reset(new ConfigKit::Store(*coreConfig, updates, req->errors));
	if (!req->errors.empty()) {
		asyncPrepareConfigChangeCompletedOne(req);
		return;
	}

	ConfigKit::prepareConfigChangeForSubComponent(
		*LoggingKit::context, coreSchema->loggingKit.translator,
		manipulateLoggingKitConfig(*req->config,
			req->config->inspectEffectiveValues()),
		req->errors, req->forLoggingKit);
	ConfigKit::prepareConfigChangeForSubComponent(
		*workingObjects->securityUpdateChecker,
		coreSchema->securityUpdateChecker.translator,
		req->config->inspectEffectiveValues(),
		req->errors, req->forSecurityUpdateChecker);
	if (workingObjects->telemetryCollector != NULL) {
		ConfigKit::prepareConfigChangeForSubComponent(
			*workingObjects->telemetryCollector,
			coreSchema->telemetryCollector.translator,
			req->config->inspectEffectiveValues(),
			req->errors, req->forTelemetryCollector);
	}

	req->forControllerServerKit.resize(wo->threadWorkingObjects.size(), NULL);
	req->forController.resize(wo->threadWorkingObjects.size(), NULL);
	for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		req->counter++;
		two->bgloop->safe->runLater(boost::bind(asyncPrepareConfigChangeForController,
			i, updates, req));
	}

	if (wo->apiWorkingObjects.apiServer != NULL) {
		req->counter++;
		wo->apiWorkingObjects.bgloop->safe->runLater(boost::bind(
			asyncPrepareConfigChangeForApiServer, updates, req));
	}

	if (wo->adminPanelConnector != NULL) {
		req->counter++;
		wo->adminPanelConnector->asyncPrepareConfigChange(
			coreSchema->adminPanelConnector.translator.translate(updates),
			req->forAdminPanelConnector,
			boost::bind(asyncPrepareConfigChangeForAdminPanelConnectorDone,
				boost::placeholders::_1, boost::placeholders::_2, req));
	}

	/***************/
	/***************/

	asyncPrepareConfigChangeCompletedOne(req);
}


/**************** Functions: commit config change ****************/


static void
asyncCommitConfigChangeCompletedOne(ConfigChangeRequest *req) {
	assert(req->counter > 0);
	req->counter--;
	if (req->counter == 0) {
		oxt::thread(boost::bind(req->commitCallback, req),
			"Core config callback thread",
			128 * 1024);
	}
}

static void
asyncCommitConfigChangeForController(unsigned int i, ConfigChangeRequest *req) {
	ThreadWorkingObjects *two = &workingObjects->threadWorkingObjects[i];

	two->serverKitContext->commitConfigChange(*req->forControllerServerKit[i]);
	two->controller->commitConfigChange(*req->forController[i]);

	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);
	P_DEBUG("asyncCommitConfigChangeForController(" << i << "): counter "
		<< req->counter << " -> " << (req->counter - 1));
	asyncCommitConfigChangeCompletedOne(req);
}

static void
asyncCommitConfigChangeForApiServer(ConfigChangeRequest *req) {
	ApiWorkingObjects *awo = &workingObjects->apiWorkingObjects;

	awo->serverKitContext->commitConfigChange(req->forApiServerKit);
	awo->apiServer->commitConfigChange(req->forApiServer);

	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);
	P_DEBUG("asyncCommitConfigChangeForApiServer: counter "
		<< req->counter << " -> " << (req->counter - 1));
	asyncCommitConfigChangeCompletedOne(req);
}

static void
asyncCommitConfigChangeForAdminPanelConnectorDone(AdminPanelConnector::ConfigChangeRequest &_,
	ConfigChangeRequest *req)
{
	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);
	P_DEBUG("asyncCommitConfigChangeForAdminPanelConnectorDone: counter "
		<< req->counter << " -> " << (req->counter - 1));
	asyncCommitConfigChangeCompletedOne(req);
}

//
//

void
asyncCommitConfigChange(ConfigChangeRequest *req, const CommitConfigChangeCallback &callback)
	BOOST_NOEXCEPT_OR_NOTHROW
{
	WorkingObjects *wo = workingObjects;
	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);

	req->commitCallback = callback;
	req->counter++;

	coreConfig->swap(*req->config);
	LoggingKit::context->commitConfigChange(req->forLoggingKit);
	workingObjects->securityUpdateChecker->commitConfigChange(
		req->forSecurityUpdateChecker);
	if (workingObjects->telemetryCollector != NULL) {
		workingObjects->telemetryCollector->commitConfigChange(
			req->forTelemetryCollector);
	}

	wo->appPool->setMax(coreConfig->get("max_pool_size").asInt());
	wo->appPool->setMaxIdleTime(coreConfig->get("pool_idle_time").asInt() * 1000000ULL);
	wo->appPool->enableSelfChecking(coreConfig->get("pool_selfchecks").asBool());
	{
		LockGuard l(wo->appPoolContext->agentConfigSyncher);
		wo->appPoolContext->agentConfig = coreConfig->inspectEffectiveValues();
	}

	for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		req->counter++;
		two->bgloop->safe->runLater(boost::bind(asyncCommitConfigChangeForController,
			i, req));
	}

	if (wo->apiWorkingObjects.apiServer != NULL) {
		req->counter++;
		wo->apiWorkingObjects.bgloop->safe->runLater(boost::bind(
			asyncCommitConfigChangeForApiServer, req));
	}

	if (wo->adminPanelConnector != NULL) {
		req->counter++;
		wo->adminPanelConnector->asyncCommitConfigChange(
			req->forAdminPanelConnector,
			boost::bind(asyncCommitConfigChangeForAdminPanelConnectorDone,
				boost::placeholders::_1, req));
	}

	/***************/
	/***************/

	asyncCommitConfigChangeCompletedOne(req);
}


/**************** Functions: miscellaneous ****************/


inline ConfigChangeRequest *
createConfigChangeRequest() {
	return new ConfigChangeRequest();
}

inline void
freeConfigChangeRequest(ConfigChangeRequest *req) {
	delete req;
}

Json::Value
inspectConfig() {
	boost::lock_guard<boost::mutex> l(workingObjects->configSyncher);
	return coreConfig->inspect();
}


Json::Value
manipulateLoggingKitConfig(const ConfigKit::Store &coreConfig,
	const Json::Value &loggingKitConfig)
{
	Json::Value result = loggingKitConfig;
	result["buffer_logs"] = !coreConfig["admin_panel_url"].isNull();
	return result;
}


} // namespace Core
} // namespace Passenger
