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
#ifndef _PASSENGER_LOGGING_AGENT_ADMIN_SERVER_H_
#define _PASSENGER_LOGGING_AGENT_ADMIN_SERVER_H_

#include <sstream>
#include <string>

#include <agents/LoggingAgent/LoggingServer.h>
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
namespace LoggingAgent {

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

	void processStatusTxt(Client *client, Request *req) {
		if (authorize(client, req, READONLY)) {
			HeaderTable headers;
			stringstream stream;
			headers.insert(req->pool, "content-type", "text/plain");
			loggingServer->dump(stream);
			writeSimpleResponse(client, 200, &headers, stream.str());
			endRequest(&client, &req);
		} else {
			respondWith401(client, req);
		}
	}

	void processPing(Client *client, Request *req) {
		if (authorize(client, req, READONLY)) {
			HeaderTable headers;
			headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
			headers.insert(req->pool, "content-type", "application/json");
			exitEvent->notify();
			writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
			endRequest(&client, &req);
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
			endRequest(&client, &req);
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
			Json::Value doc;

			headers.insert(req->pool, "content-type", "application/json");
			doc["log_level"] = getLogLevel();

			writeSimpleResponse(client, 200, &headers, stringifyJson(doc));
			endRequest(&client, &req);
		} else if (req->method == HTTP_POST) {
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

		if (!req->jsonBody["log_level"].isInt()) {
			respondWith422(client, req, "{\"status\": \"error\", \"message\": \"log_level required\"}");
			return;
		}

		setLogLevel(req->jsonBody["log_level"].asInt());
		writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
		endRequest(&client, &req);
	}

	void respondWith401(Client *client, Request *req) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		headers.insert(req->pool, "www-authenticate", "Basic realm=\"admin\"");
		writeSimpleResponse(client, 401, &headers, "Unauthorized");
		endRequest(&client, &req);
	}

	void respondWith404(Client *client, Request *req) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 404, &headers, "Not found");
		endRequest(&client, &req);
	}

	void respondWith405(Client *client, Request *req) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 405, &headers, "Method not allowed");
		endRequest(&client, &req);
	}

	void respondWith422(Client *client, Request *req, const StaticString &body) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		writeSimpleResponse(client, 422, &headers, body);
		endRequest(&client, &req);
	}

protected:
	virtual void onRequestBegin(Client *client, Request *req) {
		if (psg_lstr_cmp(&req->path, P_STATIC_STRING("/status.txt"))) {
			processStatusTxt(client, req);
		} else if (psg_lstr_cmp(&req->path, P_STATIC_STRING("/ping.json"))) {
			processPing(client, req);
		} else if (psg_lstr_cmp(&req->path, P_STATIC_STRING("/shutdown.json"))) {
			processShutdown(client, req);
		} else if (psg_lstr_cmp(&req->path, P_STATIC_STRING("/config.json"))) {
			processConfig(client, req);
		} else {
			respondWith404(client, req);
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
				processConfigBody(client, req);
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
	LoggingServer *loggingServer;
	EventFd *exitEvent;
	vector<Authorization> authorizations;

	AdminServer(ServerKit::Context *context)
		: ParentClass(context),
		  loggingServer(NULL),
		  exitEvent(NULL)
		{ }

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


} // namespace LoggingAgent
} // namespace Passenger

#endif /* _PASSENGER_LOGGING_AGENT_ADMIN_SERVER_H_ */
