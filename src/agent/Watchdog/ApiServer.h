/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_WATCHDOG_API_SERVER_H_
#define _PASSENGER_WATCHDOG_API_SERVER_H_

#include <string>
#include <exception>
#include <cstring>
#include <jsoncpp/json.h>
#include <modp_b64.h>

#include <Shared/ApiServerUtils.h>
#include <Shared/ApiAccountUtils.h>
#include <ServerKit/HttpServer.h>
#include <DataStructures/LString.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <LoggingKit/LoggingKit.h>
#include <Constants.h>
#include <StrIntTools/StrIntUtils.h>
#include <IOTools/MessageIO.h>

namespace Passenger {
namespace Watchdog {
namespace ApiServer {

using namespace std;


/*
 * BEGIN ConfigKit schema: Passenger::Watchdog::ApiServer::Schema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   accept_burst_count           unsigned integer   -          default(32)
 *   authorizations               array              -          default("[FILTERED]"),secret
 *   client_freelist_limit        unsigned integer   -          default(0)
 *   fd_passing_password          string             required   secret
 *   min_spare_clients            unsigned integer   -          default(0)
 *   request_freelist_limit       unsigned integer   -          default(1024)
 *   start_reading_after_accept   boolean            -          default(true)
 *
 * END
 */
class Schema: public ServerKit::HttpServerSchema {
private:
	static Json::Value normalizeAuthorizations(const Json::Value &effectiveValues) {
		Json::Value updates;
		updates["authorizations"] = ApiAccountUtils::normalizeApiAccountsJson(
			effectiveValues["authorizations"]);
		return updates;
	}

public:
	Schema()
		: ServerKit::HttpServerSchema(false)
	{
		using namespace ConfigKit;

		add("fd_passing_password", STRING_TYPE, REQUIRED | SECRET);
		add("authorizations", ARRAY_TYPE, OPTIONAL | SECRET, Json::arrayValue);

		addValidator(boost::bind(ApiAccountUtils::validateAuthorizationsField,
			"authorizations", boost::placeholders::_1, boost::placeholders::_2));

		addNormalizer(normalizeAuthorizations);

		finalize();
	}
};

struct ConfigChangeRequest {
	ServerKit::HttpServerConfigChangeRequest forParent;
	boost::scoped_ptr<ApiAccountUtils::ApiAccountDatabase> apiAccountDatabase;
};

class Request: public ServerKit::BaseHttpRequest {
public:
	string body;
	Json::Value jsonBody;

	DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(Request);
};

class ApiServer: public ServerKit::HttpServer<ApiServer, ServerKit::HttpClient<Request> > {
public:
	typedef ServerKit::HttpServer<ApiServer, ServerKit::HttpClient<Request> > ParentClass;
	typedef ServerKit::HttpClient<Request> Client;
	typedef ServerKit::HeaderTable HeaderTable;

	typedef Passenger::Watchdog::ApiServer::ConfigChangeRequest ConfigChangeRequest;

private:
	ApiAccountUtils::ApiAccountDatabase apiAccountDatabase;

	void route(Client *client, Request *req, const StaticString &path) {
		if (path == P_STATIC_STRING("/status.txt")) {
			processStatusTxt(client, req);
		} else if (path == P_STATIC_STRING("/ping.json")) {
			apiServerProcessPing(this, client, req);
		} else if (path == P_STATIC_STRING("/info.json")
			// The "/version.json" path is deprecated
			|| path == P_STATIC_STRING("/version.json"))
		{
			apiServerProcessInfo(this, client, req);
		} else if (path == P_STATIC_STRING("/shutdown.json")) {
			apiServerProcessShutdown(this, client, req);
		} else if (path == P_STATIC_STRING("/backtraces.txt")) {
			apiServerProcessBacktraces(this, client, req);
		} else if (path == P_STATIC_STRING("/config.json")) {
			processConfig(client, req);
		} else if (path == P_STATIC_STRING("/config/log_file.fd")) {
			processConfigLogFileFd(client, req);
		} else if (path == P_STATIC_STRING("/reopen_logs.json")) {
			apiServerProcessReopenLogs(this, client, req);
		} else {
			apiServerRespondWith404(this, client, req);
		}
	}

	void processStatusTxt(Client *client, Request *req) {
		if (authorizeStateInspectionOperation(this, client, req)) {
			HeaderTable headers;
			//stringstream stream;
			headers.insert(req->pool, "Content-Type", "text/plain");
			//loggingServer->dump(stream);
			//writeSimpleResponse(client, 200, &headers, stream.str());
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

			HeaderTable headers;
			Json::Value doc = LoggingKit::context->getConfig().inspect();
			headers.insert(req->pool, "Content-Type", "application/json");
			writeSimpleResponse(client, 200, &headers, doc.toStyledString());
			if (!req->ended()) {
				endRequest(&client, &req);
			}
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

	void processConfigBody(Client *client, Request *req) {
		HeaderTable headers;
		LoggingKit::ConfigChangeRequest configReq;
		const Json::Value &json = req->jsonBody;
		vector<ConfigKit::Error> errors;
		bool ok;

		headers.insert(req->pool, "Content-Type", "application/json");
		headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");

		try {
			ok = LoggingKit::context->prepareConfigChange(json,
				errors, configReq);
		} catch (const std::exception &e) {
			unsigned int bufsize = 2048;
			char *message = (char *) psg_pnalloc(req->pool, bufsize);
			snprintf(message, bufsize, "{ \"status\": \"error\", "
				"\"message\": \"Error reconfiguring logging system: %s\" }",
				e.what());
			writeSimpleResponse(client, 500, &headers, message);
			if (!req->ended()) {
				endRequest(&client, &req);
			}
			return;
		}
		if (!ok) {
			unsigned int bufsize = 2048;
			char *message = (char *) psg_pnalloc(req->pool, bufsize);
			snprintf(message, bufsize, "{ \"status\": \"error\", "
				"\"message\": \"Error reconfiguring logging system: %s\" }",
				ConfigKit::toString(errors).c_str());
			writeSimpleResponse(client, 500, &headers, message);
			if (!req->ended()) {
				endRequest(&client, &req);
			}
			return;
		}

		LoggingKit::context->commitConfigChange(configReq);
		writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	bool authorizeFdPassingOperation(Client *client, Request *req) {
		const LString *password = req->headers.lookup("fd-passing-password");
		if (password == NULL) {
			return false;
		}

		password = psg_lstr_make_contiguous(password, req->pool);
		return constantTimeCompare(StaticString(password->start->data, password->size),
			config["fd_passing_password"].asString());
	}

	void processConfigLogFileFd(Client *client, Request *req) {
		if (req->method != HTTP_GET) {
			apiServerRespondWith405(this, client, req);
		} else if (authorizeFdPassingOperation(client, req)) {
			ConfigKit::Store config = LoggingKit::context->getConfig();
			HeaderTable headers;
			headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
			headers.insert(req->pool, "Content-Type", "text/plain");
			if (config["target"].isMember("path")) {
				headers.insert(req->pool, "Filename",
					psg_pstrdup(req->pool, config["target"]["path"].asString()));
			}
			req->wantKeepAlive = false;
			writeSimpleResponse(client, 200, &headers, "");
			if (req->ended()) {
				return;
			}

			unsigned long long timeout = 1000000;
			setBlocking(client->getFd());
			ScopeGuard guard(boost::bind(setNonBlocking, client->getFd()));
			writeFileDescriptorWithNegotiation(client->getFd(),
				LoggingKit::context->getConfigRealization()->targetFd,
				&timeout);
			guard.runNow();

			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			apiServerRespondWith401(this, client, req);
		}
	}

protected:
	virtual void onRequestBegin(Client *client, Request *req) {
		const StaticString path(req->path.start->data, req->path.size);

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
		if (buffer.size() > 0) {
			// Data
			req->body.append(buffer.start, buffer.size());
		} else if (errcode == 0) {
			// EOF
			Json::Reader reader;
			if (reader.parse(req->body, req->jsonBody)) {
				try {
					processConfigBody(client, req);
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

	virtual void deinitializeRequest(Client *client, Request *req) {
		req->body.clear();
		if (!req->jsonBody.isNull()) {
			req->jsonBody = Json::Value();
		}
		ParentClass::deinitializeRequest(client, req);
	}

public:
	typedef ApiAccountUtils::ApiAccount ApiAccount;

	// Dependencies
	EventFd *exitEvent;

	ApiServer(ServerKit::Context *context, const Schema &schema,
		const Json::Value &initialConfig,
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: ParentClass(context, schema, initialConfig, translator),
		  exitEvent(NULL)
	{
		apiAccountDatabase = ApiAccountUtils::ApiAccountDatabase(
			config["authorizations"]);
	}

	virtual void initialize() {
		if (exitEvent == NULL) {
			throw RuntimeException("exitEvent must be non-NULL");
		}
		ParentClass::initialize();
	}

	virtual StaticString getServerName() const {
		return P_STATIC_STRING("WatchdogApiServer");
	}

	virtual unsigned int getClientName(const Client *client, char *buf, size_t size) const {
		return ParentClass::getClientName(client, buf, size);
	}

	const ApiAccountUtils::ApiAccountDatabase &getApiAccountDatabase() const {
		return apiAccountDatabase;
	}

	bool authorizeByUid(uid_t uid) const {
		return uid == 0 || uid == geteuid();
	}

	bool authorizeByApiKey(const ApplicationPool2::ApiKey &apiKey) const {
		return apiKey.isSuper();
	}


	bool prepareConfigChange(const Json::Value &updates,
		vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
	{
		if (ParentClass::prepareConfigChange(updates, errors, req.forParent)) {
			req.apiAccountDatabase.reset(new ApiAccountUtils::ApiAccountDatabase(
				req.forParent.forParent.config->get("authorizations")));
		}
		return errors.empty();
	}

	void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
		ParentClass::commitConfigChange(req.forParent);
		apiAccountDatabase.swap(*req.apiAccountDatabase);
	}
};


} // namespace ApiServer
} // namespace Watchdog
} // namespace Passenger

#endif /* _PASSENGER_WATCHDOG_API_SERVER_H_ */
