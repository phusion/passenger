/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2015-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_API_SERVER_UTILS_H_
#define _PASSENGER_API_SERVER_UTILS_H_

/**
 * Utility code shared by agent/Core/ApiServer.h and agent/Watchdog/ApiServer.h.
 * This code handles authentication and authorization
 * of connected ApiServer clients.
 *
 * This file consists of the following items.
 *
 * ## Authorization
 *
 * The authorizeXXX() family of functions implement authorization checking on a
 * connected client. Given a client and a request, they perform various
 * checks and return information on what the client is authorized to do.
 *
 * ## Utility
 *
 * Various utility functions
 *
 * ## Common endpoints
 *
 * The apiServerProcessXXX() family of functions implement common endpoints
 * in the various API servers.
 */

#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/thread.hpp>
#include <sys/types.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstddef>
#include <cstring>
#include <jsoncpp/json.h>
#include <modp_b64.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <DataStructures/LString.h>
#include <DataStructures/StringKeyTable.h>
#include <ServerKit/Server.h>
#include <ServerKit/HeaderTable.h>
#include <LoggingKit/LoggingKit.h>
#include <LoggingKit/Context.h>
#include <Utils.h>
#include <IOTools/IOUtils.h>
#include <IOTools/BufferedIO.h>
#include <StrIntTools/StrIntUtils.h>
#include <Utils/VariantMap.h>
#include <Shared/ApplicationPoolApiKey.h>

namespace Passenger {

using namespace std;


// Forward declarations
inline string truncateApiKey(const StaticString &apiKey);


/*******************************
 *
 * Authorization functions
 *
 *******************************/


struct Authorization {
	uid_t  uid;
	ApplicationPool2::ApiKey apiKey;
	bool   canReadPool;
	bool   canModifyPool;
	bool   canInspectState;
	bool   canAdminister;

	Authorization()
		: uid((uid_t) -1),
		  canReadPool(false),
		  canModifyPool(false),
		  canInspectState(false),
		  canAdminister(false)
		{ }
};


template<typename Request>
inline bool
parseBasicAuthHeader(Request *req, string &username, string &password) {
	const LString *auth = req->headers.lookup("authorization");

	if (auth == NULL || auth->size <= 6 || !psg_lstr_cmp(auth, "Basic ", 6)) {
		return false;
	}

	auth = psg_lstr_make_contiguous(auth, req->pool);
	string authData = modp::b64_decode(
		auth->start->data + sizeof("Basic ") - 1,
		auth->size - (sizeof("Basic ") - 1));
	string::size_type pos = authData.find(':');
	if (pos == string::npos) {
		return false;
	}

	username = authData.substr(0, pos);
	password = authData.substr(pos + 1);
	return true;
}

/*
 * @throws oxt::tracable_exception
 */
template<typename ApiServer, typename Client, typename Request>
inline Authorization
authorize(ApiServer *server, Client *client, Request *req) {
	TRACE_POINT();
	Authorization auth;
	uid_t uid = -1;
	gid_t gid = -1;
	string username, password;

	try {
		readPeerCredentials(client->getFd(), &uid, &gid);
		if (server->authorizeByUid(uid)) {
			SKC_INFO_FROM_STATIC(server, client, "Authenticated with UID: " << uid);
			auth.uid = uid;
			auth.canReadPool = true;
			auth.canModifyPool = true;
			auth.canInspectState = auth.canInspectState || uid == 0 || uid == geteuid();
			auth.canAdminister = auth.canAdminister || uid == 0 || uid == geteuid();
		} else {
			SKC_INFO_FROM_STATIC(server, client, "Authentication failed for UID: " << uid);
		}
	} catch (const SystemException &e) {
		if (e.code() != ENOSYS && e.code() != EPROTONOSUPPORT) {
			throw;
		}
	}

	if (server->getApiAccountDatabase().empty()) {
		SKC_INFO_FROM_STATIC(server, client,
			"Authenticated as administrator because API account database is empty");
		auth.apiKey = ApplicationPool2::ApiKey::makeSuper();
		auth.canReadPool = true;
		auth.canModifyPool = true;
		auth.canInspectState = true;
		auth.canAdminister = true;
	} else if (parseBasicAuthHeader(req, username, password)) {
		SKC_DEBUG_FROM_STATIC(server, client,
			"HTTP basic authentication supplied: " << username);
		if (username == "api") {
			auth.apiKey = ApplicationPool2::ApiKey(password);
			if (server->authorizeByApiKey(auth.apiKey)) {
				SKC_INFO_FROM_STATIC(server, client,
					"Authenticated with API key: " << truncateApiKey(password));
				assert(!auth.apiKey.isSuper());
				auth.canReadPool = true;
				auth.canModifyPool = true;
			}
		} else {
			const typename ApiServer::ApiAccount *account =
				server->getApiAccountDatabase().lookup(username);
			if (account != NULL && constantTimeCompare(password, account->password)) {
				SKC_INFO_FROM_STATIC(server, client,
					"Authenticated with administrator account: " << username);
				auth.apiKey = ApplicationPool2::ApiKey::makeSuper();
				auth.canReadPool = true;
				auth.canModifyPool = auth.canModifyPool || !account->readonly;
				auth.canInspectState = true;
				auth.canAdminister = auth.canAdminister || !account->readonly;
			}
		}
	}

	return auth;
}

template<typename ApiServer, typename Client, typename Request>
inline bool
authorizeStateInspectionOperation(ApiServer *server, Client *client, Request *req) {
	return authorize(server, client, req).canInspectState;
}

template<typename ApiServer, typename Client, typename Request>
inline bool
authorizeAdminOperation(ApiServer *server, Client *client, Request *req) {
	return authorize(server, client, req).canAdminister;
}


/*******************************
 *
 * Utility functions
 *
 *******************************/

inline VariantMap
parseQueryString(const StaticString &query) {
	VariantMap params;
	const char *pos = query.data();
	const char *end = query.data() + query.size();

	while (pos < end) {
		const char *assignmentPos = (const char *) memchr(pos, '=', end - pos);
		if (assignmentPos != NULL) {
			string name = urldecode(StaticString(pos, assignmentPos - pos));
			const char *sepPos = (const char *) memchr(assignmentPos + 1, '&',
				end - assignmentPos - 1);
			if (sepPos != NULL) {
				string value = urldecode(StaticString(assignmentPos + 1,
					sepPos - assignmentPos - 1));
				params.set(name, value);
				pos = sepPos + 1;
			} else {
				StaticString value(assignmentPos + 1, end - assignmentPos - 1);
				params.set(name, value);
				pos = end;
			}
		} else {
			throw SyntaxError("Invalid query string format");
		}
	}

	return params;
}

inline string
truncateApiKey(const StaticString &apiKey) {
	assert(apiKey.size() == ApplicationPool2::ApiKey::SIZE);
	char prefix[3];
	memcpy(prefix, apiKey.data(), 3);
	return string(prefix, 3) + "*****";
}

template<typename Server, typename Client, typename Request>
struct ApiServerInternalHttpResponse {
	static const int ERROR_INVALID_HEADER = -1;
	static const int ERROR_INVALID_BODY = -2;
	static const int ERROR_INTERNAL = -3;

	Server *server;
	Client *client;
	Request *req;
	int status;
	StringKeyTable<string> headers;
	string body;

	vector<string> debugLogs;
	string errorLogs;
	BufferedIO io;
};

template<typename Server, typename Client, typename Request>
struct ApiServerInternalHttpRequest {
	Server *server;
	Client *client;
	Request *req;

	string address;
	http_method method;
	string uri;
	StringKeyTable<string> headers;
	boost::function<void (ApiServerInternalHttpResponse<Server, Client, Request>)> callback;

	unsigned long long timeout;
	boost::function<
		void (ApiServerInternalHttpRequest<Server, Client, Request> &req,
			ApiServerInternalHttpResponse<Server, Client, Request> &resp,
			BufferedIO &io
		)> bodyProcessor;

	ApiServerInternalHttpRequest()
		: server(NULL),
		  client(NULL),
		  req(NULL),
		  method(HTTP_GET),
		  timeout(60 * 1000000)
		{ }
};

template<typename Server, typename Client, typename Request>
inline void
apiServerMakeInternalHttpRequestCallbackWrapper(
	boost::function<void (ApiServerInternalHttpResponse<Server, Client, Request>)> callback,
	ApiServerInternalHttpResponse<Server, Client, Request> resp)
{
	if (!resp.debugLogs.empty()) {
		foreach (string log, resp.debugLogs) {
			SKC_DEBUG_FROM_STATIC(resp.server, resp.client, log);
		}
	}
	if (!resp.errorLogs.empty()) {
		SKC_ERROR_FROM_STATIC(resp.server, resp.client, resp.errorLogs);
	}
	callback(resp);
	resp.server->unrefRequest(resp.req, __FILE__, __LINE__);
}

template<typename Server, typename Client, typename Request>
inline void
apiServerMakeInternalHttpRequestThreadMain(ApiServerInternalHttpRequest<Server, Client, Request> req) {
	typedef ApiServerInternalHttpRequest<Server, Client, Request> InternalRequest;
	typedef ApiServerInternalHttpResponse<Server, Client, Request> InternalResponse;

	struct Guard {
		InternalRequest &req;
		InternalResponse &resp;
		SafeLibevPtr libev;
		bool cleared;

		Guard(InternalRequest &_req, InternalResponse &_resp, const SafeLibevPtr &_libev)
			: req(_req),
			  resp(_resp),
			  libev(_libev),
			  cleared(false)
			{ }

		~Guard() {
			if (!cleared) {
				resp.status = InternalResponse::ERROR_INTERNAL;
				resp.headers.clear();
				resp.body.clear();
				libev->runLater(boost::bind(
					apiServerMakeInternalHttpRequestCallbackWrapper<Server, Client, Request>,
					req.callback, resp));
			}
		}

		void clear() {
			cleared = true;
		}
	};

	InternalResponse resp;
	SafeLibevPtr libev = req.server->getContext()->libev;

	resp.server = req.server;
	resp.client = req.client;
	resp.req    = req.req;
	resp.status = InternalResponse::ERROR_INTERNAL;

	Guard guard(req, resp, libev);


	try {
		FileDescriptor conn(connectToServer(req.address, NULL, 0), __FILE__, __LINE__);
		BufferedIO io(conn);

		string header;
		header.append(http_method_str(req.method));
		header.append(" ", 1);
		header.append(req.uri);
		header.append(" HTTP/1.1\r\n");

		StringKeyTable<string>::ConstIterator it(req.headers);
		while (*it != NULL) {
			header.append(it.getKey());
			header.append(": ", 2);
			header.append(it.getValue());
			header.append("\r\n", 2);
			it.next();
		}
		header.append("Connection: close\r\n\r\n");

		writeExact(conn, header, &req.timeout);


		string statusLine = io.readLine();
		resp.debugLogs.push_back("Internal request response data: \"" + cEscapeString(statusLine) + "\"");
		boost::regex statusLineRegex("^HTTP/.*? ([0-9]+) (.*)$");
		boost::smatch results;
		if (!boost::regex_match(statusLine, results, statusLineRegex)) {
			guard.clear();
			resp.status = InternalResponse::ERROR_INVALID_HEADER;
			libev->runLater(boost::bind(
				apiServerMakeInternalHttpRequestCallbackWrapper<Server, Client, Request>,
				req.callback, resp));
			return;
		}
		resp.status = (int) stringToUint(results.str(1));
		if (resp.status <= 0 || resp.status >= 1000) {
			guard.clear();
			resp.status = InternalResponse::ERROR_INVALID_HEADER;
			libev->runLater(boost::bind(
				apiServerMakeInternalHttpRequestCallbackWrapper<Server, Client, Request>,
				req.callback, resp));
			return;
		}

		string response;
		while (true) {
			response = io.readLine();
			resp.debugLogs.push_back("Internal request response data: \"" + cEscapeString(response) + "\"");
			if (response.empty()) {
				guard.clear();
				resp.status = InternalResponse::ERROR_INVALID_HEADER;
				libev->runLater(boost::bind(
					apiServerMakeInternalHttpRequestCallbackWrapper<Server, Client, Request>,
					req.callback, resp));
				return;
			} else if (response == "\r\n") {
				break;
			} else {
				const char *pos = (const char *) memchr(response.data(), ':', response.size());
				if (pos == NULL) {
					guard.clear();
					resp.status = InternalResponse::ERROR_INVALID_HEADER;
					libev->runLater(boost::bind(
						apiServerMakeInternalHttpRequestCallbackWrapper<Server, Client, Request>,
						req.callback, resp));
					return;
				}

				string key(strip(response.substr(0, pos - response.data())));
				string value(response.substr(pos - response.data()));
				value.erase(0, 1);
				value = strip(value);
				if (!value.empty() && value[value.size() - 1] == '\r') {
					value.erase(value.size() - 1, 1);
				}

				if (key.empty() || value.empty()) {
					guard.clear();
					resp.status = InternalResponse::ERROR_INVALID_HEADER;
					libev->runLater(boost::bind(
						apiServerMakeInternalHttpRequestCallbackWrapper<Server, Client, Request>,
						req.callback, resp));
					return;
				}

				resp.headers.insert(key, value);
			}
		}

		if (req.bodyProcessor) {
			req.bodyProcessor(req, resp, io);
		} else {
			resp.body = io.readAll(&req.timeout);
		}
		guard.clear();
		libev->runLater(boost::bind(
			apiServerMakeInternalHttpRequestCallbackWrapper<Server, Client, Request>,
			req.callback, resp));
	} catch (const oxt::tracable_exception &e) {
		resp.errorLogs.append("Exception: ");
		resp.errorLogs.append(e.what());
		resp.errorLogs.append("\n");
		resp.errorLogs.append(e.backtrace());
	}
}

/**
 * Utility function for API servers for making an internal HTTP request,
 * usually to another agent. The request is made in a background thread.
 * When done, the callback is called on the event loop. While the request
 * is being made, a reference to the ServerKit request object is held.
 *
 * This is not a fully featured HTTP client and doesn't fully correctly
 * parse HTTP, so it can't be used with arbitrary servers. It doesn't
 * support keep-alive and chunked transfer-encodings.
 */
template<typename Server, typename Client, typename Request>
inline void
apiServerMakeInternalHttpRequest(const ApiServerInternalHttpRequest<Server, Client, Request> &params) {
	params.server->refRequest(params.req, __FILE__, __LINE__);
	oxt::thread(boost::bind(
		apiServerMakeInternalHttpRequestThreadMain<Server, Client, Request>,
		params), "Internal HTTP request", 1024 * 128);
}


/*******************************
 *
 * Common endpoints
 *
 *******************************/

template<typename Server, typename Client, typename Request>
inline void
apiServerRespondWith401(Server *server, Client *client, Request *req) {
	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	headers.insert(req->pool, "WWW-Authenticate", "Basic realm=\"api\"");
	server->writeSimpleResponse(client, 401, &headers, "Unauthorized");
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerRespondWith404(Server *server, Client *client, Request *req) {
	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	server->writeSimpleResponse(client, 404, &headers, "Not found");
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerRespondWith405(Server *server, Client *client, Request *req) {
	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	server->writeSimpleResponse(client, 405, &headers, "Method not allowed");
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerRespondWith413(Server *server, Client *client, Request *req) {
	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	server->writeSimpleResponse(client, 413, &headers, "Request body too large");
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerRespondWith422(Server *server, Client *client, Request *req, const StaticString &body) {
	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	headers.insert(req->pool, "Content-Type", "text/plain; charset=utf-8");
	server->writeSimpleResponse(client, 422, &headers, body);
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerRespondWith500(Server *server, Client *client, Request *req, const StaticString &body) {
	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	headers.insert(req->pool, "Content-Type", "text/plain; charset=utf-8");
	server->writeSimpleResponse(client, 500, &headers, body);
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessPing(Server *server, Client *client, Request *req) {
	Authorization auth(authorize(server, client, req));
	if (auth.canReadPool || auth.canInspectState) {
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "application/json");
		server->writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
		if (!req->ended()) {
			server->endRequest(&client, &req);
		}
	} else {
		apiServerRespondWith401(server, client, req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessInfo(Server *server, Client *client, Request *req,
	const boost::function<void (Json::Value &response)> &postprocessResponse =
		boost::function<void (Json::Value &)>())
{
	Authorization auth(authorize(server, client, req));
	if (auth.canReadPool || auth.canInspectState) {
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "application/json");

		Json::Value response;
		response["pid"] = (Json::UInt64) getpid();
		response["program_name"] = PROGRAM_NAME;
		response["program_version"] = PASSENGER_VERSION;
		response["api_version"] = PASSENGER_API_VERSION;
		response["api_version_major"] = PASSENGER_API_VERSION_MAJOR;
		response["api_version_minor"] = PASSENGER_API_VERSION_MINOR;
		#ifdef PASSENGER_IS_ENTERPRISE
			response["passenger_enterprise"] = true;
		#endif

		if (postprocessResponse) {
			postprocessResponse(response);
		}

		server->writeSimpleResponse(client, 200, &headers,
			response.toStyledString());
		if (!req->ended()) {
			server->endRequest(&client, &req);
		}
	} else {
		apiServerRespondWith401(server, client, req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessBacktraces(Server *server, Client *client, Request *req) {
	if (authorizeStateInspectionOperation(server, client, req)) {
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "text/plain");
		server->writeSimpleResponse(client, 200, &headers,
			psg_pstrdup(req->pool, oxt::thread::all_backtraces()));
		if (!req->ended()) {
			server->endRequest(&client, &req);
		}
	} else {
		apiServerRespondWith401(server, client, req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessShutdown(Server *server, Client *client, Request *req) {
	if (req->method != HTTP_POST) {
		apiServerRespondWith405(server, client, req);
	} else if (authorizeAdminOperation(server, client, req)) {
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "application/json");
		server->exitEvent->notify();
		server->writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
		if (!req->ended()) {
			server->endRequest(&client, &req);
		}
	} else {
		apiServerRespondWith401(server, client, req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessReopenLogs(Server *server, Client *client, Request *req) {
	if (req->method != HTTP_POST) {
		apiServerRespondWith405(server, client, req);
	} else if (authorizeAdminOperation(server, client, req)) {
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "application/json");

		ConfigKit::Store config = LoggingKit::context->getConfig();
		if (!config["target"].isMember("path")) {
			server->writeSimpleResponse(client, 500, &headers, "{ \"status\": \"error\", "
				"\"code\": \"NO_LOG_FILE\", "
				"\"message\": \"" PROGRAM_NAME " was not configured with a log file.\" }\n");
			if (!req->ended()) {
				server->endRequest(&client, &req);
			}
			return;
		}

		LoggingKit::ConfigChangeRequest configReq;
		vector<ConfigKit::Error> errors;
		bool ok;
		try {
			// We deliberately ignore the target.stderr key.
			// If the log file was equal to stderr then we'll want
			// to reopen the log file anyway.
			Json::Value updates;
			updates["target"] = config["target"]["path"];
			ok = LoggingKit::context->prepareConfigChange(updates,
				errors, configReq);
		} catch (const SystemException &e) {
			unsigned int bufsize = 2048;
			char *message = (char *) psg_pnalloc(req->pool, bufsize);
			snprintf(message, bufsize, "{ \"status\": \"error\", "
				"\"code\": \"OS_ERROR\", "
				"\"message\": \"Cannot reopen log files: %s\" }",
				e.what());
			server->writeSimpleResponse(client, 500, &headers, message);
			if (!req->ended()) {
				server->endRequest(&client, &req);
			}
			return;
		} catch (const std::exception &e) {
			unsigned int bufsize = 2048;
			char *message = (char *) psg_pnalloc(req->pool, bufsize);
			snprintf(message, bufsize, "{ \"status\": \"error\", "
				"\"code\": \"GENERIC_ERROR\", "
				"\"message\": \"Cannot reopen log files: %s\" }",
				e.what());
			server->writeSimpleResponse(client, 500, &headers, message);
			if (!req->ended()) {
				server->endRequest(&client, &req);
			}
			return;
		}
		if (!ok) {
			unsigned int bufsize = 2048;
			char *message = (char *) psg_pnalloc(req->pool, bufsize);
			snprintf(message, bufsize, "{ \"status\": \"error\", "
				"\"code\": \"CONFIG_VALIDATION_ERROR\", "
				"\"message\": \"Cannot reopen log files:"
					" invalid logging system configuration: %s\" }",
				ConfigKit::toString(errors).c_str());
			server->writeSimpleResponse(client, 500, &headers, message);
			if (!req->ended()) {
				server->endRequest(&client, &req);
			}
			return;
		}

		LoggingKit::context->commitConfigChange(configReq);
		P_NOTICE("All log file(s) reopened.");

		server->writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }\n");
		if (!req->ended()) {
			server->endRequest(&client, &req);
		}
	} else {
		apiServerRespondWith401(server, client, req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
_apiServerProcessReinheritLogsResponseBody(
	ApiServerInternalHttpRequest<Server, Client, Request> &req,
	ApiServerInternalHttpResponse<Server, Client, Request> &resp,
	BufferedIO &io)
{
	typedef ApiServerInternalHttpResponse<Server, Client, Request> InternalResponse;

	string logFilePath = resp.headers.lookupCopy("Filename");
	if (logFilePath.empty()) {
		resp.status = InternalResponse::ERROR_INVALID_BODY;
		resp.errorLogs.append("Error communicating with Watchdog process: "
			"no log filename received in response");
		return;
	}

	int fd = readFileDescriptorWithNegotiation(io.getFd(), &req.timeout);
	FdGuard guard(fd, __FILE__, __LINE__);
	P_LOG_FILE_DESCRIPTOR_PURPOSE(fd, "Reinherited log file handle");

	ConfigKit::Store oldConfig = LoggingKit::context->getConfig();
	Json::Value config;
	vector<ConfigKit::Error> errors;
	LoggingKit::ConfigChangeRequest configReq;
	bool ok;

	config["target"] = oldConfig["target"];
	config["target"]["fd"] = fd;
	config["target"].removeMember("stderr");
	try {
		ok = LoggingKit::context->prepareConfigChange(config,
			errors, configReq);
	} catch (const std::exception &e) {
		resp.status = InternalResponse::ERROR_INTERNAL;
		resp.errorLogs.append("Error reconfiguring logging system: ");
		resp.errorLogs.append(e.what());
		return;
	}
	if (!ok) {
		resp.status = InternalResponse::ERROR_INTERNAL;
		resp.errorLogs.append("Error reconfiguring logging system: ");
		resp.errorLogs.append(ConfigKit::toString(errors));
		return;
	}

	LoggingKit::context->commitConfigChange(configReq);
	guard.clear();
	P_NOTICE("All log file(s) reinherited.");
}

template<typename Server, typename Client, typename Request>
inline void
_apiServerProcessReinheritLogsDone(ApiServerInternalHttpResponse<Server, Client, Request> resp) {
	typedef ApiServerInternalHttpResponse<Server, Client, Request> InternalResponse;

	Server *server = resp.server;
	Client *client = resp.client;
	Request *req   = resp.req;
	int status;
	StaticString body;

	if (req->ended()) {
		return;
	}

	if (resp.status < 0) {
		status = 500;
		switch (resp.status) {
		case InternalResponse::ERROR_INVALID_HEADER:
			body = "{ \"status\": \"error\", "
				"\"code\": \"INHERIT_ERROR\", "
				"\"message\": \"Error communicating with Watchdog process: "
					"invalid response headers from Watchdog\" }\n";
			break;
		case InternalResponse::ERROR_INVALID_BODY:
			body = "{ \"status\": \"error\", "
				"\"code\": \"INHERIT_ERROR\", "
				"\"message\": \"Error communicating with Watchdog process: "
					"invalid response body from Watchdog\" }\n";
			break;
		case InternalResponse::ERROR_INTERNAL:
			body = "{ \"status\": \"error\", "
				"\"code\": \"INHERIT_ERROR\", "
				"\"message\": \"Error communicating with Watchdog process: "
					"an internal error occurred\" }\n";
			break;
		default:
			body = "{ \"status\": \"error\", "
				"\"code\": \"INHERIT_ERROR\", "
				"\"message\": \"Error communicating with Watchdog process: "
					"unknown error\" }\n";
			break;
		}
	} else if (resp.status == 200) {
		status = 200;
		body = "{ \"status\": \"ok\" }\n";
	} else {
		status = 500;
		body = "{ \"status\": \"error\", "
			"\"code\": \"INHERIT_ERROR\", "
			"\"message\": \"Error communicating with Watchdog process: non-200 response\" }\n";
	}

	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	headers.insert(req->pool, "Content-Type", "application/json");
	req->wantKeepAlive = false;
	server->writeSimpleResponse(client, status, &headers, body);
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessReinheritLogs(Server *server, Client *client, Request *req,
	const StaticString &instanceDir, const StaticString &fdPassingPassword)
{
	if (req->method != HTTP_POST) {
		apiServerRespondWith405(server, client, req);
	} else if (authorizeAdminOperation(server, client, req)) {
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
		headers.insert(req->pool, "Content-Type", "application/json");

		if (instanceDir.empty() || fdPassingPassword.empty()) {
			server->writeSimpleResponse(client, 501, &headers,
				"{ \"status\": \"error\", "
				"\"code\": \"NO_WATCHDOG\", "
				"\"message\": \"No Watchdog process\" }\n");
			if (!req->ended()) {
				server->endRequest(&client, &req);
			}
			return;
		}

		ApiServerInternalHttpRequest<Server, Client, Request> params;
		params.server = server;
		params.client = client;
		params.req    = req;
		params.address = "unix:" + instanceDir + "/agents.s/watchdog_api";
		params.method = HTTP_GET;
		params.uri    = "/config/log_file.fd";
		params.headers.insert("Fd-Passing-Password", fdPassingPassword);
		params.callback = _apiServerProcessReinheritLogsDone<Server, Client, Request>;
		params.bodyProcessor = _apiServerProcessReinheritLogsResponseBody<Server, Client, Request>;
		apiServerMakeInternalHttpRequest(params);
	} else {
		apiServerRespondWith401(server, client, req);
	}
}


} // namespace Passenger

#endif /* _PASSENGER_API_SERVER_UTILS_H_ */
