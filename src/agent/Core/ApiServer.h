/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_CORE_API_SERVER_H_
#define _PASSENGER_CORE_API_SERVER_H_

#include <boost/regex.hpp>
#include <oxt/thread.hpp>
#include <sstream>
#include <string>
#include <cstring>
#include <sys/types.h>

#include <jsoncpp/json.h>
#include <modp_b64.h>

#include <Core/Controller.h>
#include <Core/ApplicationPool/Pool.h>
#include <Shared/ApiServerUtils.h>
#include <ServerKit/HttpServer.h>
#include <DataStructures/LString.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <Logging.h>
#include <Constants.h>
#include <Utils/StrIntUtils.h>
#include <Utils/BufferedIO.h>
#include <Utils/MessageIO.h>

namespace Passenger {
namespace Core {
namespace ApiServer {

using namespace std;


class Request: public ServerKit::BaseHttpRequest {
public:
	string body;
	Json::Value jsonBody;
	Authorization authorization;
	unsigned int controllerStatesGathered;
	vector<Json::Value> controllerStates;

	DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(Passenger::Core::ApiServer::Request);
};

class ApiServer: public ServerKit::HttpServer<ApiServer, ServerKit::HttpClient<Request> > {
private:
	typedef ServerKit::HttpServer<ApiServer, ServerKit::HttpClient<Request> > ParentClass;
	typedef ServerKit::HttpClient<Request> Client;
	typedef ServerKit::HeaderTable HeaderTable;

	boost::regex serverConnectionPath;

	bool regex_match(const StaticString &str, const boost::regex &e) const {
		return boost::regex_match(str.data(), str.data() + str.size(), e);
	}

	int extractThreadNumberFromClientName(const string &clientName) const {
		boost::smatch results;
		boost::regex re("^([0-9]+)-.*");

		if (!boost::regex_match(clientName, results, re)) {
			return -1;
		}
		if (results.size() != 2) {
			return -1;
		}
		return stringToUint(results.str(1));
	}

	static void disconnectClient(Controller *controller, string clientName) {
		controller->disconnect(clientName);
	}

	void route(Client *client, Request *req, const StaticString &path) {
		if (path == P_STATIC_STRING("/server.json")) {
			processServerStatus(client, req);
		} else if (regex_match(path, serverConnectionPath)) {
			processServerConnectionOperation(client, req);
		} else if (path == P_STATIC_STRING("/pool.xml")) {
			processPoolStatusXml(client, req);
		} else if (path == P_STATIC_STRING("/pool.txt")) {
			processPoolStatusTxt(client, req);
		} else if (path == P_STATIC_STRING("/pool/restart_app_group.json")) {
			processPoolRestartAppGroup(client, req);
		} else if (path == P_STATIC_STRING("/pool/detach_process.json")) {
			processPoolDetachProcess(client, req);
		} else if (path == P_STATIC_STRING("/backtraces.txt")) {
			apiServerProcessBacktraces(this, client, req);
		} else if (path == P_STATIC_STRING("/ping.json")) {
			apiServerProcessPing(this, client, req);
		} else if (path == P_STATIC_STRING("/info.json")
			// The "/version.json" path is deprecated
			|| path == P_STATIC_STRING("/version.json"))
		{
			apiServerProcessInfo(this, client, req);
		} else if (path == P_STATIC_STRING("/shutdown.json")) {
			apiServerProcessShutdown(this, client, req);
		} else if (path == P_STATIC_STRING("/gc.json")) {
			processGc(client, req);
		} else if (path == P_STATIC_STRING("/config.json")) {
			processConfig(client, req);
		} else if (path == P_STATIC_STRING("/reinherit_logs.json")) {
			apiServerProcessReinheritLogs(this, client, req,
				instanceDir, fdPassingPassword);
		} else if (path == P_STATIC_STRING("/reopen_logs.json")) {
			apiServerProcessReopenLogs(this, client, req);
		} else {
			apiServerRespondWith404(this, client, req);
		}
	}

	void processServerConnectionOperation(Client *client, Request *req) {
		if (!authorizeAdminOperation(this, client, req)) {
			apiServerRespondWith401(this, client, req);
		} else if (req->method == HTTP_DELETE) {
			StaticString path = req->getPathWithoutQueryString();
			boost::smatch results;

			boost::regex_match(path.toString(), results, serverConnectionPath);
			if (results.size() != 2) {
				endAsBadRequest(&client, &req, "Invalid URI");
				return;
			}

			int threadNumber = extractThreadNumberFromClientName(results.str(1));
			if (threadNumber < 1 || (unsigned int) threadNumber > controllers.size()) {
				HeaderTable headers;
				headers.insert(req->pool, "Content-Type", "application/json");
				writeSimpleResponse(client, 400, &headers,
					"{ \"status\": \"error\", \"reason\": \"Invalid thread number\" }");
				if (!req->ended()) {
					endRequest(&client, &req);
				}
				return;
			}

			controllers[threadNumber - 1]->getContext()->libev->runLater(boost::bind(
				disconnectClient, controllers[threadNumber - 1], results.str(1)));

			HeaderTable headers;
			headers.insert(req->pool, "Content-Type", "application/json");
			writeSimpleResponse(client, 200, &headers,
				"{ \"status\": \"ok\" }");
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			apiServerRespondWith405(this, client, req);
		}
	}

	void gatherControllerState(Client *client, Request *req,
		Controller *controller, unsigned int i)
	{
		Json::Value state = controller->inspectStateAsJson();
		getContext()->libev->runLater(boost::bind(&ApiServer::controllerStateGathered,
			this, client, req, i, state));
	}

	void controllerStateGathered(Client *client, Request *req,
		unsigned int i, Json::Value state)
	{
		if (req->ended()) {
			unrefRequest(req, __FILE__, __LINE__);
			return;
		}

		req->controllerStatesGathered++;
		req->controllerStates[i] = state;

		if (req->controllerStatesGathered == controllers.size()) {
			HeaderTable headers;
			headers.insert(req->pool, "Content-Type", "application/json");

			Json::Value response;
			response["threads"] = (Json::UInt) controllers.size();

			for (unsigned int i = 0; i < controllers.size(); i++) {
				string key = "thread" + toString(i + 1);
				response[key] = req->controllerStates[i];
			}

			writeSimpleResponse(client, 200, &headers,
				psg_pstrdup(req->pool, response.toStyledString()));
			if (!req->ended()) {
				Request *req2 = req;
				endRequest(&client, &req2);
			}
		}

		unrefRequest(req, __FILE__, __LINE__);
	}

	void processServerStatus(Client *client, Request *req) {
		if (authorizeStateInspectionOperation(this, client, req)) {
			req->controllerStates.resize(controllers.size());
			for (unsigned int i = 0; i < controllers.size(); i++) {
				refRequest(req, __FILE__, __LINE__);
				controllers[i]->getContext()->libev->runLater(boost::bind(
					&ApiServer::gatherControllerState, this,
					client, req, controllers[i], i));
			}
		} else {
			apiServerRespondWith401(this, client, req);
		}
	}

	void processPoolStatusXml(Client *client, Request *req) {
		Authorization auth(authorize(this, client, req));
		if (auth.canReadPool) {
			ApplicationPool2::Pool::ToXmlOptions options(
				parseQueryString(req->getQueryString()));
			options.uid = auth.uid;
			options.apiKey = auth.apiKey;

			HeaderTable headers;
			headers.insert(req->pool, "Content-Type", "text/xml");
			writeSimpleResponse(client, 200, &headers,
				psg_pstrdup(req->pool, appPool->toXml(options)));
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			HeaderTable headers;
			headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
			headers.insert(req->pool, "WWW-Authenticate", "Basic realm=\"api\"");
			if (clientOnUnixDomainSocket(client) && appPool->getGroupCount() == 0) {
				// Allow admin tools that connected through the Unix domain socket
				// to know that this authorization error is caused by the fact
				// that the pool is empty.
				headers.insert(req->pool, "Pool-Empty", "true");
			}
			writeSimpleResponse(client, 401, &headers, "Unauthorized");
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		}
	}

	void processPoolStatusTxt(Client *client, Request *req) {
		Authorization auth(authorize(this, client, req));
		if (auth.canReadPool) {
			ApplicationPool2::Pool::InspectOptions options(
				parseQueryString(req->getQueryString()));
			options.uid = auth.uid;
			options.apiKey = auth.apiKey;

			HeaderTable headers;
			headers.insert(req->pool, "Content-Type", "text/plain");
			writeSimpleResponse(client, 200, &headers,
				psg_pstrdup(req->pool, appPool->inspect(options)));
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			HeaderTable headers;
			headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
			headers.insert(req->pool, "WWW-Authenticate", "Basic realm=\"api\"");
			if (clientOnUnixDomainSocket(client) && appPool->getGroupCount() == 0) {
				// Allow admin tools that connected through the Unix domain socket
				// to know that this authorization error is caused by the fact
				// that the pool is empty.
				headers.insert(req->pool, "Pool-Empty", "true");
			}
			writeSimpleResponse(client, 401, &headers, "Unauthorized");
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		}
	}

	void processPoolRestartAppGroup(Client *client, Request *req) {
		Authorization auth(authorize(this, client, req));
		if (!auth.canModifyPool) {
			apiServerRespondWith401(this, client, req);
		} else if (req->method != HTTP_POST) {
			apiServerRespondWith405(this, client, req);
		} else if (!req->hasBody()) {
			endAsBadRequest(&client, &req, "Body required");
		} else if (requestBodyExceedsLimit(client, req)) {
			apiServerRespondWith413(this, client, req);
		} else {
			req->authorization = auth;
			// Continues in processPoolRestartAppGroupBody().
		}
	}

	void processPoolRestartAppGroupBody(Client *client, Request *req) {
		if (!req->jsonBody.isMember("name")) {
			endAsBadRequest(&client, &req, "Name required");
			return;
		}

		ApplicationPool2::Pool::RestartOptions options;
		options.uid = req->authorization.uid;
		options.apiKey = req->authorization.apiKey;
		if (req->jsonBody.isMember("restart_method")) {
			string restartMethodString = req->jsonBody["restart_method"].asString();
			if (restartMethodString == "blocking") {
				options.method = RM_BLOCKING;
			} else if (restartMethodString == "rolling") {
				options.method = RM_ROLLING;
			} else {
				endAsBadRequest(&client, &req, "Unsupported restart method");
				return;
			}
		}

		bool result;
		const char *response;
		try {
			result = appPool->restartGroupByName(req->jsonBody["name"].asString(),
				options);
		} catch (const SecurityException &) {
			apiServerRespondWith401(this, client, req);
			return;
		}
		if (result) {
			response = "{ \"restarted\": true }";
		} else {
			response = "{ \"restarted\": false }";
		}

		HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "application/json");
		headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 200, &headers, response);

		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	void processPoolDetachProcess(Client *client, Request *req) {
		Authorization auth(authorize(this, client, req));
		if (!auth.canModifyPool) {
			apiServerRespondWith401(this, client, req);
		} else if (req->method != HTTP_POST) {
			apiServerRespondWith405(this, client, req);
		} else if (!req->hasBody()) {
			endAsBadRequest(&client, &req, "Body required");
		} else if (requestBodyExceedsLimit(client, req)) {
			apiServerRespondWith413(this, client, req);
		} else {
			req->authorization = auth;
			// Continues in processPoolDetachProcessBody().
		}
	}

	void processPoolDetachProcessBody(Client *client, Request *req) {
		if (req->jsonBody.isMember("pid")) {
			pid_t pid = (pid_t) req->jsonBody["pid"].asUInt();
			ApplicationPool2::Pool::AuthenticationOptions options;
			options.uid = req->authorization.uid;
			options.apiKey = req->authorization.apiKey;

			bool result;
			try {
				result = appPool->detachProcess(pid, options);
			} catch (const SecurityException &) {
				apiServerRespondWith401(this, client, req);
				return;
			}

			const char *response;
			if (result) {
				response = "{ \"detached\": true }";
			} else {
				response = "{ \"detached\": false }";
			}

			HeaderTable headers;
			headers.insert(req->pool, "Content-Type", "application/json");
			headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
			writeSimpleResponse(client, 200, &headers, response);
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			endAsBadRequest(&client, &req, "PID required");
		}
	}

	static void garbageCollect(Controller *controller) {
		ServerKit::Context *ctx = controller->getContext();
		unsigned int count;

		count = mbuf_pool_compact(&ctx->mbuf_pool);
		SKS_NOTICE_FROM_STATIC(controller, "Freed " << count << " mbufs");

		controller->compact(LVL_NOTICE);
	}

	void processGc(Client *client, Request *req) {
		if (req->method != HTTP_PUT) {
			apiServerRespondWith405(this, client, req);
		} else if (authorizeAdminOperation(this, client, req)) {
			HeaderTable headers;
			headers.insert(req->pool, "Content-Type", "application/json");
			for (unsigned int i = 0; i < controllers.size(); i++) {
				controllers[i]->getContext()->libev->runLater(boost::bind(
					garbageCollect, controllers[i]));
			}
			writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			apiServerRespondWith401(this, client, req);
		}
	}

	void processConfig(Client *client, Request *req) {
		if (req->method == HTTP_GET) {
			if (!authorizeStateInspectionOperation(this, client, req)) {
				apiServerRespondWith401(this, client, req);
			}

			refRequest(req, __FILE__, __LINE__);
			controllers[0]->getContext()->libev->runLater(boost::bind(
				&ApiServer::processConfig_getControllerConfig, this,
				client, req, controllers[0]));
		} else if (req->method == HTTP_PUT) {
			if (!authorizeAdminOperation(this, client, req)) {
				apiServerRespondWith401(this, client, req);
			} else if (!req->hasBody()) {
				endAsBadRequest(&client, &req, "Body required");
			}
			// Continue in processConfigBody()
		} else {
			apiServerRespondWith405(this, client, req);
		}
	}

	void processConfig_getControllerConfig(Client *client, Request *req,
		Controller *controller)
	{
		Json::Value config = controller->getConfigAsJson();
		getContext()->libev->runLater(boost::bind(
			&ApiServer::processConfig_controllerConfigGathered, this,
			client, req, controller, config));
	}

	void processConfig_controllerConfigGathered(Client *client, Request *req,
		Controller *controller, Json::Value config)
	{
		if (req->ended()) {
			unrefRequest(req, __FILE__, __LINE__);
			return;
		}

		HeaderTable headers;
		string logFile = getLogFile();
		string fileDescriptorLogFile = getFileDescriptorLogFile();

		headers.insert(req->pool, "Content-Type", "application/json");
		config["log_level"] = getLogLevel();
		if (!logFile.empty()) {
			config["log_file"] = logFile;
		}
		if (!fileDescriptorLogFile.empty()) {
			config["file_descriptor_log_file"] = fileDescriptorLogFile;
		}

		writeSimpleResponse(client, 200, &headers,
			psg_pstrdup(req->pool, config.toStyledString()));
		if (!req->ended()) {
			Request *req2 = req;
			endRequest(&client, &req2);
		}

		unrefRequest(req, __FILE__, __LINE__);
	}

	static void configureController(Controller *controller, Json::Value json) {
		controller->configure(json);
	}

	void processConfigBody(Client *client, Request *req) {
		HeaderTable headers;
		Json::Value &json = req->jsonBody;

		headers.insert(req->pool, "Content-Type", "application/json");
		headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");

		if (json.isMember("log_level")) {
			setLogLevel(json["log_level"].asInt());
		}
		if (json.isMember("log_file")) {
			string logFile = json["log_file"].asString();
			try {
				logFile = absolutizePath(logFile);
			} catch (const SystemException &e) {
				unsigned int bufsize = 1024;
				char *message = (char *) psg_pnalloc(req->pool, bufsize);
				snprintf(message, bufsize, "{ \"status\": \"error\", "
					"\"message\": \"Cannot absolutize log file filename: %s\" }",
					e.what());
				writeSimpleResponse(client, 500, &headers, message);
				if (!req->ended()) {
					endRequest(&client, &req);
				}
				return;
			}

			int e;
			if (!setLogFile(logFile, &e)) {
				unsigned int bufsize = 1024;
				char *message = (char *) psg_pnalloc(req->pool, bufsize);
				snprintf(message, bufsize, "{ \"status\": \"error\", "
					"\"message\": \"Cannot open log file: %s (errno=%d)\" }",
					strerror(e), e);
				writeSimpleResponse(client, 500, &headers, message);
				if (!req->ended()) {
					endRequest(&client, &req);
				}
				return;
			}
			P_NOTICE("Log file opened.");
		}
		for (unsigned int i = 0; i < controllers.size(); i++) {
			controllers[i]->getContext()->libev->runLater(boost::bind(
				configureController, controllers[i], json));
		}

		writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	bool requestBodyExceedsLimit(Client *client, Request *req, unsigned int limit = 1024 * 128) {
		return (req->bodyType == Request::RBT_CONTENT_LENGTH
				&& req->aux.bodyInfo.contentLength > limit)
			|| (req->bodyType == Request::RBT_CHUNKED
				&& req->body.size() > limit);
	}

protected:
	virtual void onRequestBegin(Client *client, Request *req) {
		TRACE_POINT();
		StaticString path = req->getPathWithoutQueryString();

		P_INFO("API request: " << http_method_str(req->method) <<
			" " << StaticString(req->path.start->data, req->path.size));

		try {
			route(client, req, path);
		} catch (const oxt::tracable_exception &e) {
			SKC_ERROR(client, "Exception: " << e.what() << "\n" << e.backtrace());
			if (!req->ended()) {
				req->wantKeepAlive = false;
				endRequest(&client, &req);
			}
		}
	}

	virtual ServerKit::Channel::Result onRequestBody(Client *client, Request *req,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		TRACE_POINT();
		if (buffer.size() > 0) {
			// Data
			req->body.append(buffer.start, buffer.size());
			if (requestBodyExceedsLimit(client, req)) {
				apiServerRespondWith413(this, client, req);
			}
		} else if (errcode == 0) {
			// EOF
			Json::Reader reader;
			if (reader.parse(req->body, req->jsonBody)) {
				StaticString path = req->getPathWithoutQueryString();
				try {
					if (path == P_STATIC_STRING("/pool/restart_app_group.json")) {
						processPoolRestartAppGroupBody(client, req);
					} else if (path == P_STATIC_STRING("/pool/detach_process.json")) {
						processPoolDetachProcessBody(client, req);
					} else if (path == P_STATIC_STRING("/config.json")) {
						processConfigBody(client, req);
					} else {
						P_BUG("Unknown path for body processing: " << path);
					}
				} catch (const oxt::tracable_exception &e) {
					SKC_ERROR(client, "Exception: " << e.what() << "\n" << e.backtrace());
					if (!req->ended()) {
						req->wantKeepAlive = false;
						endRequest(&client, &req);
					}
				}
			} else {
				apiServerRespondWith422(this, client, req, reader.getFormattedErrorMessages());
			}
		} else {
			// Error
			disconnect(&client);
		}
		return ServerKit::Channel::Result(buffer.size(), false);
	}

	virtual void reinitializeRequest(Client *client, Request *req) {
		ParentClass::reinitializeRequest(client, req);
		req->controllerStatesGathered = 0;
	}

	virtual void deinitializeRequest(Client *client, Request *req) {
		req->body.clear();
		if (!req->jsonBody.isNull()) {
			req->jsonBody = Json::Value();
		}
		req->authorization = Authorization();
		req->controllerStates.clear();
		ParentClass::deinitializeRequest(client, req);
	}

public:
	vector<Controller *> controllers;
	ApiAccountDatabase *apiAccountDatabase;
	ApplicationPool2::PoolPtr appPool;
	string instanceDir;
	string fdPassingPassword;
	EventFd *exitEvent;
	vector<Authorization> authorizations;

	ApiServer(ServerKit::Context *context)
		: ParentClass(context),
		  serverConnectionPath("^/server/(.+)\\.json$"),
		  apiAccountDatabase(NULL),
		  exitEvent(NULL)
		{ }

	virtual StaticString getServerName() const {
		return P_STATIC_STRING("ApiServer");
	}

	virtual unsigned int getClientName(const Client *client, char *buf, size_t size) const {
		char *pos = buf;
		const char *end = buf + size - 1;
		pos = appendData(pos, end, "Adm.", 1);
		pos += uintToString(client->number, pos, end - pos);
		*pos = '\0';
		return pos - buf;
	}

	bool authorizeByUid(uid_t uid) const {
		return appPool->authorizeByUid(uid);
	}

	bool authorizeByApiKey(const ApplicationPool2::ApiKey &apiKey) const {
		return appPool->authorizeByApiKey(apiKey);
	}
};


} // namespace ApiServer
} // namespace Core
} // namespace Passenger

#endif /* _PASSENGER_CORE_API_SERVER_H_ */
