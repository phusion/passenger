/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013-2014 Phusion
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
#ifndef _PASSENGER_SERVER_AGENT_ADMIN_SERVER_H_
#define _PASSENGER_SERVER_AGENT_ADMIN_SERVER_H_

#include <oxt/thread.hpp>
#include <sstream>
#include <string>

#include <agents/HelperAgent/RequestHandler.h>
#include <ApplicationPool2/Pool.h>
#include <ServerKit/HttpServer.h>
#include <DataStructures/LString.h>
#include <FileDescriptor.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <Utils/StrIntUtils.h>
#include <Utils/Base64.h>
#include <Utils/json.h>
#include <Utils/JsonUtils.h>

namespace Passenger {
namespace ServerAgent {

using namespace std;


class Request: public ServerKit::BaseHttpRequest {
public:
	string body;
	Json::Value jsonBody;

	DEFINE_SERVER_KIT_BASE_HTTP_REQUEST_FOOTER(Request);
};

class AdminServer: public ServerKit::HttpServer<AdminServer, ServerKit::HttpClient<Request> > {
public:
	enum PrivilegeLevel {
		NONE,
		READONLY,
		FULL
	};

	struct Authorization {
		PrivilegeLevel level;
		string username;
		string password;
	};

private:
	typedef ServerKit::HttpServer<AdminServer, ServerKit::HttpClient<Request> > ParentClass;
	typedef ServerKit::HttpClient<Request> Client;
	typedef ServerKit::HeaderTable HeaderTable;

	bool parseAuthorizationHeader(Request *req, string &username,
		string &password) const
	{
		const LString *auth = req->headers.lookup("authorization");

		if (auth == NULL || auth->size <= 6 || !psg_lstr_cmp(auth, "Basic ", 6)) {
			return false;
		}

		auth = psg_lstr_make_contiguous(auth, req->pool);
		string authData = Base64::decode(
			(const unsigned char *) auth->start->data + sizeof("Basic ") - 1,
			auth->size - (sizeof("Basic ") - 1));
		string::size_type pos = authData.find(':');
		if (pos == string::npos) {
			return false;
		}

		username = authData.substr(0, pos);
		password = authData.substr(pos + 1);
		return true;
	}

	const Authorization *lookupAuthorizationRecord(const StaticString &username) const {
		vector<Authorization>::const_iterator it, end = authorizations.end();

		for (it = authorizations.begin(); it != end; it++) {
			if (it->username == username) {
				return &(*it);
			}
		}

		return NULL;
	}

	bool authorize(Client *client, Request *req, PrivilegeLevel level) const {
		if (authorizations.empty()) {
			return true;
		}

		string username, password;
		if (!parseAuthorizationHeader(req, username, password)) {
			return false;
		}

		const Authorization *auth = lookupAuthorizationRecord(username);
		return auth != NULL
			&& auth->level >= level
			&& constantTimeCompare(password, auth->password);
	}

	static VariantMap parseQueryString(const StaticString &query) {
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

	void processConnectionsStatus(Client *client, Request *req) {
		if (authorize(client, req, READONLY)) {
			HeaderTable headers;
			headers.insert(req->pool, "content-type", "application/json");
			writeSimpleResponse(client, 200, &headers,
				psg_pstrdup(req->pool,
					requestHandler->inspectStateAsJson().toStyledString()));
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			respondWith401(client, req);
		}
	}

	void processPoolStatusXml(Client *client, Request *req) {
		if (authorize(client, req, READONLY)) {
			try {
				VariantMap params = parseQueryString(req->getQueryString());
				HeaderTable headers;
				headers.insert(req->pool, "content-type", "text/xml");
				writeSimpleResponse(client, 200, &headers,
					psg_pstrdup(req->pool, appPool->toXml(
						params.getBool("secrets", false, false))));
			} catch (const SyntaxError &e) {
				SKC_ERROR(client, e.what());
			}
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			respondWith401(client, req);
		}
	}

	void processPoolStatusTxt(Client *client, Request *req) {
		if (authorize(client, req, READONLY)) {
			try {
				ApplicationPool2::Pool::InspectOptions options(
					parseQueryString(req->getQueryString()));
				HeaderTable headers;
				headers.insert(req->pool, "content-type", "text/plain");
				writeSimpleResponse(client, 200, &headers,
					psg_pstrdup(req->pool, appPool->inspect(options)));
			} catch (const SyntaxError &e) {
				SKC_ERROR(client, e.what());
			}
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			respondWith401(client, req);
		}
	}

	void processPoolRestartAppGroup(Client *client, Request *req) {
		if (req->method != HTTP_POST) {
			respondWith405(client, req);
		} else if (!authorize(client, req, FULL)) {
			respondWith401(client, req);
		} else if (!req->hasBody()) {
			endAsBadRequest(&client, &req, "Body required");
		} else if (requestBodyExceedsLimit(client, req)) {
			respondWith413(client, req);
		}
		// Continues in processPoolRestartAppGroupBody().
	}

	void processPoolRestartAppGroupBody(Client *client, Request *req) {
		HeaderTable headers;
		RestartMethod method = ApplicationPool2::RM_DEFAULT;

		headers.insert(req->pool, "content-type", "application/json");
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");

		if (!req->jsonBody.isMember("name")) {
			endAsBadRequest(&client, &req, "Name required");
			return;
		}

		if (req->jsonBody.isMember("restart_method")) {
			string restartMethodString = req->jsonBody["restart_method"].asString();
			if (restartMethodString == "blocking") {
				method = RM_BLOCKING;
			} else if (restartMethodString == "rolling") {
				method = RM_ROLLING;
			} else {
				endAsBadRequest(&client, &req, "Unsupported restart method");
				return;
			}
		}

		const char *response;
		if (appPool->restartGroupByName(req->jsonBody["name"].asString(), method)) {
			response = "{ \"restarted\": true }";
		} else {
			response = "{ \"restarted\": false }";
		}
		writeSimpleResponse(client, 200, &headers, response);

		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	void processPoolDetachProcess(Client *client, Request *req) {
		if (req->method != HTTP_POST) {
			respondWith405(client, req);
		} else if (!authorize(client, req, FULL)) {
			respondWith401(client, req);
		} else if (!req->hasBody()) {
			endAsBadRequest(&client, &req, "Body required");
		} else if (requestBodyExceedsLimit(client, req)) {
			respondWith413(client, req);
		}
		// Continues in processPoolDetachProcessBody().
	}

	void processPoolDetachProcessBody(Client *client, Request *req) {
		HeaderTable headers;
		const char *response;

		headers.insert(req->pool, "content-type", "application/json");
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");

		if (req->jsonBody.isMember("pid")) {
			pid_t pid = (pid_t) req->jsonBody["pid"].asUInt();
			if (appPool->detachProcess(pid)) {
				response = "{ \"detached\": true }";
			} else {
				response = "{ \"detached\": false }";
			}
			writeSimpleResponse(client, 200, &headers, response);
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			endAsBadRequest(&client, &req, "PID required");
		}
	}

	void processBacktraces(Client *client, Request *req) {
		if (authorize(client, req, READONLY)) {
			HeaderTable headers;
			headers.insert(req->pool, "content-type", "text/plain");
			writeSimpleResponse(client, 200, &headers,
				psg_pstrdup(req->pool, oxt::thread::all_backtraces()));
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			respondWith401(client, req);
		}
	}

	void processPing(Client *client, Request *req) {
		if (authorize(client, req, READONLY)) {
			HeaderTable headers;
			headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
			headers.insert(req->pool, "content-type", "application/json");
			writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			respondWith401(client, req);
		}
	}

	void processShutdown(Client *client, Request *req) {
		if (req->method != HTTP_PUT) {
			respondWith405(client, req);
		} else if (authorize(client, req, FULL)) {
			HeaderTable headers;
			headers.insert(req->pool, "content-type", "application/json");
			exitEvent->notify();
			writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			respondWith401(client, req);
		}
	}

	void processConfig(Client *client, Request *req) {
		if (req->method == HTTP_GET) {
			if (!authorize(client, req, READONLY)) {
				respondWith401(client, req);
			}

			HeaderTable headers;
			headers.insert(req->pool, "content-type", "application/json");
			Json::Value doc = requestHandler->getConfigAsJson();
			doc["log_level"] = getLogLevel();

			writeSimpleResponse(client, 200, &headers,
				psg_pstrdup(req->pool, doc.toStyledString()));
			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else if (req->method == HTTP_PUT) {
			if (!authorize(client, req, FULL)) {
				respondWith401(client, req);
			} else if (!req->hasBody()) {
				endAsBadRequest(&client, &req, "Body required");
			}
			// Continue in processConfigBody()
		} else {
			respondWith405(client, req);
		}
	}

	void processConfigBody(Client *client, Request *req) {
		HeaderTable headers;

		headers.insert(req->pool, "content-type", "application/json");
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");

		if (req->jsonBody.isMember("log_level")) {
			setLogLevel(req->jsonBody["log_level"].asInt());
		}

		requestHandler->configure(req->jsonBody);
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

	void respondWith401(Client *client, Request *req) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		headers.insert(req->pool, "www-authenticate", "Basic realm=\"admin\"");
		writeSimpleResponse(client, 401, &headers, "Unauthorized");
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	void respondWith404(Client *client, Request *req) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 404, &headers, "Not found");
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	void respondWith405(Client *client, Request *req) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 405, &headers, "Method not allowed");
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	void respondWith413(Client *client, Request *req) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 413, &headers, "Request body too large");
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	void respondWith422(Client *client, Request *req, const StaticString &body) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 422, &headers, body);
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

protected:
	virtual void onRequestBegin(Client *client, Request *req) {
		TRACE_POINT();
		StaticString path = req->getPathWithoutQueryString();

		P_INFO("Admin request: " << http_method_str(req->method) <<
			" " << StaticString(req->path.start->data, req->path.size));

		try {
			if (path == P_STATIC_STRING("/connections.json")) {
				processConnectionsStatus(client, req);
			} else if (path == P_STATIC_STRING("/pool.xml")) {
				processPoolStatusXml(client, req);
			} else if (path == P_STATIC_STRING("/pool.txt")) {
				processPoolStatusTxt(client, req);
			} else if (path == P_STATIC_STRING("/pool/restart_app_group.json")) {
				processPoolRestartAppGroup(client, req);
			} else if (path == P_STATIC_STRING("/pool/detach_process.json")) {
				processPoolDetachProcess(client, req);
			} else if (path == P_STATIC_STRING("/backtraces.txt")) {
				processBacktraces(client, req);
			} else if (path == P_STATIC_STRING("/ping.json")) {
				processPing(client, req);
			} else if (path == P_STATIC_STRING("/shutdown.json")) {
				processShutdown(client, req);
			} else if (path == P_STATIC_STRING("/config.json")) {
				processConfig(client, req);
			} else {
				respondWith404(client, req);
			}
		} catch (const oxt::tracable_exception &e) {
			SKC_ERROR(client, "Exception: " << e.what() << "\n" << e.backtrace());
			if (!req->ended()) {
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
				respondWith413(client, req);
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
						endRequest(&client, &req);
					}
				}
			} else {
				respondWith422(client, req, reader.getFormattedErrorMessages());
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
	RequestHandler *requestHandler;
	ApplicationPool2::PoolPtr appPool;
	EventFd *exitEvent;
	vector<Authorization> authorizations;

	AdminServer(ServerKit::Context *context)
		: ParentClass(context),
		  requestHandler(NULL),
		  exitEvent(NULL)
		{ }

	virtual StaticString getServerName() const {
		return P_STATIC_STRING("AdminServer");
	}

	static PrivilegeLevel parseLevel(const StaticString &level) {
		if (level == "readonly") {
			return READONLY;
		} else if (level == "full") {
			return FULL;
		} else {
			throw RuntimeException("Invalid privilege level " + level);
		}
	}
};


} // namespace ServerAgent
} // namespace Passenger

#endif /* _PASSENGER_SERVER_AGENT_ADMIN_SERVER_H_ */
