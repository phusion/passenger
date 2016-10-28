/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2016 Phusion Holding B.V.
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
#include <Core/Controller.h>

/*************************************************************************
 *
 * Implements Core::Controller methods pertaining the initialization
 * of a request.
 *
 *************************************************************************/

namespace Passenger {
namespace Core {

using namespace std;
using namespace boost;


/****************************
 *
 * Private methods
 *
 ****************************/


struct Controller::RequestAnalysis {
	const LString *flags;
	ServerKit::HeaderTable::Cell *appGroupNameCell;
	bool unionStationSupport;
};


void
Controller::initializeFlags(Client *client, Request *req, RequestAnalysis &analysis) {
	if (analysis.flags != NULL) {
		const LString::Part *part = analysis.flags->start;
		while (part != NULL) {
			const char *data = part->data;
			const char *end  = part->data + part->size;
			while (data < end) {
				switch (*data) {
				case 'D':
					req->dechunkResponse = true;
					break;
				case 'B':
					req->requestBodyBuffering = true;
					break;
				case 'S':
					req->https = true;
					break;
				case 'C':
					req->strip100ContinueHeader = true;
					break;
				default:
					break;
				}
				data++;
			}
			part = part->next;
		}

		if (OXT_UNLIKELY(getLogLevel() >= LVL_DEBUG2)) {
			if (req->dechunkResponse) {
				SKC_TRACE(client, 2, "Dechunk flag detected");
			}
			if (req->requestBodyBuffering) {
				SKC_TRACE(client, 2, "Request body buffering enabled");
			}
			if (req->https) {
				SKC_TRACE(client, 2, "HTTPS flag detected");
			}
			if (req->strip100ContinueHeader) {
				SKC_TRACE(client, 2, "Stripping 100 Continue header");
			}
		}
	}
}

bool
Controller::respondFromTurboCache(Client *client, Request *req) {
	if (!turboCaching.isEnabled() || !turboCaching.responseCache.prepareRequest(this, req)) {
		return false;
	}

	SKC_TRACE(client, 2, "Turbocaching: trying to reply from cache (key \"" <<
		cEscapeString(req->cacheKey) << "\")");
	SKC_TRACE(client, 2, "Turbocache entries:\n" << turboCaching.responseCache.inspect());

	if (turboCaching.responseCache.requestAllowsFetching(req)) {
		ResponseCache<Request>::Entry entry(turboCaching.responseCache.fetch(req,
			ev_now(getLoop())));
		if (entry.valid()) {
			SKC_TRACE(client, 2, "Turbocaching: cache hit (key \"" <<
				cEscapeString(req->cacheKey) << "\")");
			turboCaching.writeResponse(this, client, req, entry);
			if (!req->ended()) {
				endRequest(&client, &req);
			}
			return true;
		} else {
			SKC_TRACE(client, 2, "Turbocaching: cache miss: " <<
				entry.getCacheMissReasonString() <<
				" (key \"" << cEscapeString(req->cacheKey) << "\")");
			return false;
		}
	} else {
		SKC_TRACE(client, 2, "Turbocaching: request not eligible for caching");
		return false;
	}
}

void
Controller::initializePoolOptions(Client *client, Request *req, RequestAnalysis &analysis) {
	boost::shared_ptr<Options> *options;

	if (singleAppMode) {
		P_ASSERT_EQ(poolOptionsCache.size(), 1);
		poolOptionsCache.lookupRandom(NULL, &options);
		req->options = **options;
	} else {
		ServerKit::HeaderTable::Cell *appGroupNameCell = analysis.appGroupNameCell;
		if (appGroupNameCell != NULL && appGroupNameCell->header->val.size > 0) {
			const LString *appGroupName = psg_lstr_make_contiguous(
				&appGroupNameCell->header->val,
				req->pool);
			HashedStaticString hAppGroupName(appGroupName->start->data,
				appGroupName->size);

			poolOptionsCache.lookup(hAppGroupName, &options);

			if (options != NULL) {
				req->options = **options;
			} else {
				createNewPoolOptions(client, req, hAppGroupName);
			}
		} else {
			disconnectWithError(&client, "the !~PASSENGER_APP_GROUP_NAME header must be set");
		}
	}

	if (!req->ended()) {
		// See comment for req->envvars to learn how it is different
		// from req->options.environmentVariables.
		req->envvars = req->secureHeaders.lookup(PASSENGER_ENV_VARS);
		if (req->envvars != NULL && req->envvars->size > 0) {
			req->envvars = psg_lstr_make_contiguous(req->envvars, req->pool);
			req->options.environmentVariables = StaticString(
				req->envvars->start->data,
				req->envvars->size);
		}

		fillPoolOption(req, req->options.maxRequests, PASSENGER_MAX_REQUESTS);
	}
}

void
Controller::fillPoolOptionsFromAgentsOptions(Options &options) {
	options.ruby = defaultRuby;
	if (agentsOptions->has("default_nodejs")) {
		options.nodejs = agentsOptions->get("default_nodejs");
	}
	if (agentsOptions->has("default_python")) {
		options.python = agentsOptions->get("default_python");
	}
	if (agentsOptions->has("meteor_app_settings")) {
		options.meteorAppSettings = agentsOptions->get("meteor_app_settings");
	}
	if (agentsOptions->has("app_file_descriptor_ulimit")) {
		options.fileDescriptorUlimit = agentsOptions->getUint("app_file_descriptor_ulimit");
	}

	options.logLevel = getLogLevel();
	options.integrationMode = agentsOptions->get("integration_mode",
		false, DEFAULT_INTEGRATION_MODE);
	options.ustRouterAddress = ustRouterAddress;
	options.ustRouterUsername = P_STATIC_STRING("logging");
	options.ustRouterPassword = ustRouterPassword;
	options.userSwitching = agentsOptions->getBool("user_switching");
	if (agentsOptions->has("default_user")) {
		options.defaultUser = agentsOptions->get("default_user");
	}
	if (agentsOptions->has("default_group")) {
		options.defaultGroup = agentsOptions->get("default_group");
	}
	options.minProcesses = agentsOptions->getInt("min_instances");
	options.maxPreloaderIdleTime = agentsOptions->getInt("max_preloader_idle_time");
	options.maxRequestQueueSize = agentsOptions->getInt("max_request_queue_size");
	options.abortWebsocketsOnProcessShutdown = agentsOptions->getBool("abort_websockets_on_process_shutdown");
	options.forceMaxConcurrentRequestsPerProcess = agentsOptions->getInt("force_max_concurrent_requests_per_process");
	options.spawnMethod = agentsOptions->get("spawn_method");
	options.loadShellEnvvars = agentsOptions->getBool("load_shell_envvars");
	options.statThrottleRate = statThrottleRate;

	/******************************/
}

void
Controller::fillPoolOption(Request *req, StaticString &field,
	const HashedStaticString &name)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = StaticString(value->start->data, value->size);
	}
}

void
Controller::fillPoolOption(Request *req, bool &field,
	const HashedStaticString &name)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		field = psg_lstr_first_byte(value) == 't';
	}
}

void
Controller::fillPoolOption(Request *req, int &field,
	const HashedStaticString &name)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToInt(StaticString(value->start->data, value->size));
	}
}

void
Controller::fillPoolOption(Request *req, unsigned int &field,
	const HashedStaticString &name)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToUint(StaticString(value->start->data, value->size));
	}
}

void
Controller::fillPoolOption(Request *req, unsigned long &field,
	const HashedStaticString &name)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToUint(StaticString(value->start->data, value->size));
	}
}

void
Controller::fillPoolOption(Request *req, long &field,
	const HashedStaticString &name)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToInt(StaticString(value->start->data, value->size));
	}
}

void
Controller::fillPoolOptionSecToMsec(Request *req, unsigned int &field,
	const HashedStaticString &name)
{
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToInt(StaticString(value->start->data, value->size)) * 1000;
	}
}

void
Controller::createNewPoolOptions(Client *client, Request *req,
	const HashedStaticString &appGroupName)
{
	ServerKit::HeaderTable &secureHeaders = req->secureHeaders;
	Options &options = req->options;

	SKC_TRACE(client, 2, "Creating new pool options: app group name=" << appGroupName);

	options = Options();

	const LString *scriptName = secureHeaders.lookup("!~SCRIPT_NAME");
	const LString *appRoot = secureHeaders.lookup("!~PASSENGER_APP_ROOT");
	if (scriptName == NULL || scriptName->size == 0) {
		if (appRoot == NULL || appRoot->size == 0) {
			const LString *documentRoot = secureHeaders.lookup("!~DOCUMENT_ROOT");
			if (OXT_UNLIKELY(documentRoot == NULL || documentRoot->size == 0)) {
				disconnectWithError(&client, "client did not send a !~PASSENGER_APP_ROOT or a !~DOCUMENT_ROOT header");
				return;
			}

			documentRoot = psg_lstr_make_contiguous(documentRoot, req->pool);
			appRoot = psg_lstr_create(req->pool,
				extractDirNameStatic(StaticString(documentRoot->start->data,
					documentRoot->size)));
		} else {
			appRoot = psg_lstr_make_contiguous(appRoot, req->pool);
		}
		options.appRoot = HashedStaticString(appRoot->start->data, appRoot->size);
	} else {
		if (appRoot == NULL || appRoot->size == 0) {
			const LString *documentRoot = secureHeaders.lookup("!~DOCUMENT_ROOT");
			if (OXT_UNLIKELY(documentRoot == NULL || documentRoot->size == 0)) {
				disconnectWithError(&client, "client did not send a !~DOCUMENT_ROOT header");
				return;
			}

			documentRoot = psg_lstr_null_terminate(documentRoot, req->pool);
			documentRoot = resolveSymlink(StaticString(documentRoot->start->data,
				documentRoot->size), req->pool);
			appRoot = psg_lstr_create(req->pool,
				extractDirNameStatic(StaticString(documentRoot->start->data,
					documentRoot->size)));
		} else {
			appRoot = psg_lstr_make_contiguous(appRoot, req->pool);
		}
		options.appRoot = HashedStaticString(appRoot->start->data, appRoot->size);
		scriptName = psg_lstr_make_contiguous(scriptName, req->pool);
		options.baseURI = StaticString(scriptName->start->data, scriptName->size);
	}

	fillPoolOptionsFromAgentsOptions(options);

	const LString *appType = secureHeaders.lookup("!~PASSENGER_APP_TYPE");
	if (appType == NULL || appType->size == 0) {
		AppTypeDetector detector;
		PassengerAppType type = detector.checkAppRoot(options.appRoot);
		if (type == PAT_NONE || type == PAT_ERROR) {
			disconnectWithError(&client, "client did not send a recognized !~PASSENGER_APP_TYPE header");
			return;
		}
		options.appType = getAppTypeName(type);
	} else {
		fillPoolOption(req, options.appType, "!~PASSENGER_APP_TYPE");
	}

	options.appGroupName = appGroupName;

	fillPoolOption(req, options.appType, "!~PASSENGER_APP_TYPE");
	fillPoolOption(req, options.environment, "!~PASSENGER_APP_ENV");
	fillPoolOption(req, options.ruby, "!~PASSENGER_RUBY");
	fillPoolOption(req, options.python, "!~PASSENGER_PYTHON");
	fillPoolOption(req, options.nodejs, "!~PASSENGER_NODEJS");
	fillPoolOption(req, options.meteorAppSettings, "!~PASSENGER_METEOR_APP_SETTINGS");
	fillPoolOption(req, options.user, "!~PASSENGER_USER");
	fillPoolOption(req, options.group, "!~PASSENGER_GROUP");
	fillPoolOption(req, options.minProcesses, "!~PASSENGER_MIN_PROCESSES");
	fillPoolOption(req, options.maxProcesses, "!~PASSENGER_MAX_PROCESSES");
	fillPoolOption(req, options.spawnMethod, "!~PASSENGER_SPAWN_METHOD");
	fillPoolOption(req, options.startCommand, "!~PASSENGER_START_COMMAND");
	fillPoolOptionSecToMsec(req, options.startTimeout, "!~PASSENGER_START_TIMEOUT");
	fillPoolOption(req, options.maxPreloaderIdleTime, "!~PASSENGER_MAX_PRELOADER_IDLE_TIME");
	fillPoolOption(req, options.maxRequestQueueSize, "!~PASSENGER_MAX_REQUEST_QUEUE_SIZE");
	fillPoolOption(req, options.abortWebsocketsOnProcessShutdown, "!~PASSENGER_ABORT_WEBSOCKETS_ON_PROCESS_SHUTDOWN");
	fillPoolOption(req, options.forceMaxConcurrentRequestsPerProcess, "!~PASSENGER_FORCE_MAX_CONCURRENT_REQUESTS_PER_PROCESS");
	fillPoolOption(req, options.restartDir, "!~PASSENGER_RESTART_DIR");
	fillPoolOption(req, options.startupFile, "!~PASSENGER_STARTUP_FILE");
	fillPoolOption(req, options.loadShellEnvvars, "!~PASSENGER_LOAD_SHELL_ENVVARS");
	fillPoolOption(req, options.fileDescriptorUlimit, "!~PASSENGER_APP_FILE_DESCRIPTOR_ULIMIT");
	fillPoolOption(req, options.raiseInternalError, "!~PASSENGER_RAISE_INTERNAL_ERROR");
	fillPoolOption(req, options.lveMinUid, "!~PASSENGER_LVE_MIN_UID");
	/******************/

	boost::shared_ptr<Options> optionsCopy = boost::make_shared<Options>(options);
	optionsCopy->persist(options);
	optionsCopy->clearPerRequestFields();
	optionsCopy->detachFromUnionStationTransaction();
	poolOptionsCache.insert(options.getAppGroupName(), optionsCopy);
}

void
Controller::initializeUnionStation(Client *client, Request *req, RequestAnalysis &analysis) {
	if (analysis.unionStationSupport) {
		Options &options = req->options;
		ServerKit::HeaderTable &headers = req->secureHeaders;

		const LString *key = headers.lookup("!~UNION_STATION_KEY");
		if (key == NULL || key->size == 0) {
			disconnectWithError(&client, "header !~UNION_STATION_KEY must be set.");
			return;
		}
		key = psg_lstr_make_contiguous(key, req->pool);

		const LString *filters = headers.lookup("!~UNION_STATION_FILTERS");
		if (filters != NULL) {
			filters = psg_lstr_make_contiguous(filters, req->pool);
		}

		options.transaction = unionStationContext->newTransaction(
			options.getAppGroupName(), "requests",
			string(key->start->data, key->size),
			(filters != NULL)
				? string(filters->start->data, filters->size)
				: string());
		if (!options.transaction->isNull()) {
			options.analytics = true;
			options.unionStationKey = StaticString(key->start->data, key->size);
		}

		req->beginStopwatchLog(&req->stopwatchLogs.requestProcessing, "request processing");
		req->logMessage(string("Request method: ") + http_method_str(req->method));
		req->logMessage("URI: " + StaticString(req->path.start->data, req->path.size));
	}
}

void
Controller::setStickySessionId(Client *client, Request *req) {
	if (req->stickySession) {
		// TODO: This is not entirely correct. Clients MAY send multiple Cookie
		// headers, although this is in practice extremely rare.
		// http://stackoverflow.com/questions/16305814/are-multiple-cookie-headers-allowed-in-an-http-request
		const LString *cookieHeader = req->headers.lookup(HTTP_COOKIE);
		if (cookieHeader != NULL && cookieHeader->size > 0) {
			const LString *cookieName = getStickySessionCookieName(req);
			vector< pair<StaticString, StaticString> > cookies;
			pair<StaticString, StaticString> cookie;

			parseCookieHeader(req->pool, cookieHeader, cookies);
			foreach (cookie, cookies) {
				if (psg_lstr_cmp(cookieName, cookie.first)) {
					// This cookie matches the one we're looking for.
					req->options.stickySessionId = stringToUint(cookie.second);
					return;
				}
			}
		}
	}
}

const LString *
Controller::getStickySessionCookieName(Request *req) {
	const LString *value = req->headers.lookup(PASSENGER_STICKY_SESSIONS_COOKIE_NAME);
	if (value == NULL || value->size == 0) {
		return psg_lstr_create(req->pool,
			defaultStickySessionsCookieName);
	} else {
		return value;
	}
}


/****************************
 *
 * Protected methods
 *
 ****************************/


void
Controller::onRequestBegin(Client *client, Request *req) {
	ParentClass::onRequestBegin(client, req);

	CC_BENCHMARK_POINT(client, req, BM_AFTER_ACCEPT);

	{
		// Perform hash table operations as close to header parsing as possible,
		// and localize them as much as possible, for better CPU caching.
		RequestAnalysis analysis;
		analysis.flags = req->secureHeaders.lookup(FLAGS);
		analysis.appGroupNameCell = singleAppMode
			? NULL
			: req->secureHeaders.lookupCell(PASSENGER_APP_GROUP_NAME);
		analysis.unionStationSupport = unionStationContext != NULL
			&& getBoolOption(req, UNION_STATION_SUPPORT, false);
		req->stickySession = getBoolOption(req, PASSENGER_STICKY_SESSIONS,
			this->stickySessions);
		req->showVersionInHeader = getBoolOption(req, PASSENGER_SHOW_VERSION_IN_HEADER,
			this->showVersionInHeader);
		req->host = req->headers.lookup(HTTP_HOST);

		/***************/
		/***************/

		SKC_TRACE(client, 2, "Initiating request");
		req->startedAt = ev_now(getLoop());
		req->bodyChannel.stop();

		initializeFlags(client, req, analysis);
		if (respondFromTurboCache(client, req)) {
			return;
		}
		initializePoolOptions(client, req, analysis);
		if (req->ended()) {
			return;
		}
		initializeUnionStation(client, req, analysis);
		if (req->ended()) {
			return;
		}
		setStickySessionId(client, req);
	}

	if (!req->hasBody() || !req->requestBodyBuffering) {
		req->requestBodyBuffering = false;
		checkoutSession(client, req);
	} else {
		beginBufferingBody(client, req);
	}
}


} // namespace Core
} // namespace Passenger
