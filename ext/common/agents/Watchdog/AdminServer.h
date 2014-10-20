/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
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
#ifndef _PASSENGER_WATCHDOG_AGENT_ADMIN_SERVER_H_
#define _PASSENGER_WATCHDOG_AGENT_ADMIN_SERVER_H_

#include <sstream>
#include <string>
#include <cstring>

#include <ServerKit/HttpServer.h>
#include <DataStructures/LString.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <Logging.h>
#include <Constants.h>
#include <Utils/StrIntUtils.h>
#include <Utils/modp_b64.h>
#include <Utils/json.h>

namespace Passenger {
namespace WatchdogAgent {

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
			//stringstream stream;
			headers.insert(req->pool, "content-type", "text/plain");
			//loggingServer->dump(stream);
			//writeSimpleResponse(client, 200, &headers, stream.str());
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
		if (req->method != HTTP_POST) {
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
			Json::Value doc;
			string logFile = getLogFile();

			headers.insert(req->pool, "content-type", "application/json");
			doc["log_level"] = getLogLevel();
			if (!logFile.empty()) {
				doc["log_file"] = logFile;
			}

			writeSimpleResponse(client, 200, &headers, doc.toStyledString());
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
		Json::Value &json = req->jsonBody;

		headers.insert(req->pool, "content-type", "application/json");

		if (json.isMember("log_level")) {
			setLogLevel(json["log_level"].asInt());
		}
		if (json.isMember("log_file")) {
			if (!setLogFile(json["log_file"].asCString())) {
				int e = errno;
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

		writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }");
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

	void processReopenLogs(Client *client, Request *req) {
		if (req->method != HTTP_POST) {
			respondWith405(client, req);
		} else if (authorize(client, req, FULL)) {
			HeaderTable headers;
			headers.insert(req->pool, "content-type", "application/json");

			string logFile = getLogFile();
			if (logFile.empty()) {
				writeSimpleResponse(client, 500, &headers, "{ \"status\": \"error\", "
					"\"code\": \"NO_LOG_FILE\", "
					"\"message\": \"" PROGRAM_NAME " was not configured with a log file.\" }\n");
			} else {
				if (!setLogFile(logFile.c_str())) {
					int e = errno;
					unsigned int bufsize = 1024;
					char *message = (char *) psg_pnalloc(req->pool, bufsize);
					snprintf(message, bufsize, "{ \"status\": \"error\", "
						"\"code\": \"LOG_FILE_OPEN_ERROR\", "
						"\"message\": \"Cannot reopen log file: %s (errno=%d)\" }",
						strerror(e), e);
					writeSimpleResponse(client, 500, &headers, message);
					if (!req->ended()) {
						endRequest(&client, &req);
					}
					return;
				}
				P_NOTICE("Log file reopened.");
				writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }\n");
			}

			if (!req->ended()) {
				endRequest(&client, &req);
			}
		} else {
			respondWith401(client, req);
		}
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

	void respondWith422(Client *client, Request *req, const StaticString &body) {
		HeaderTable headers;
		headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
		headers.insert(req->pool, "content-type", "text/plain; charset=utf-8");
		writeSimpleResponse(client, 422, &headers, body);
		if (!req->ended()) {
			endRequest(&client, &req);
		}
	}

protected:
	virtual void onRequestBegin(Client *client, Request *req) {
		const StaticString path(req->path.start->data, req->path.size);

		P_INFO("Admin request: " << path);

		if (path == P_STATIC_STRING("/status.txt")) {
			processStatusTxt(client, req);
		} else if (path == P_STATIC_STRING("/ping.json")) {
			processPing(client, req);
		} else if (path == P_STATIC_STRING("/shutdown.json")) {
			processShutdown(client, req);
		} else if (path == P_STATIC_STRING("/config.json")) {
			processConfig(client, req);
		} else if (path == P_STATIC_STRING("/reopen_logs.json")) {
			processReopenLogs(client, req);
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
	EventFd *exitEvent;
	vector<Authorization> authorizations;

	AdminServer(ServerKit::Context *context)
		: ParentClass(context),
		  exitEvent(NULL)
		{ }

	virtual StaticString getServerName() const {
		return P_STATIC_STRING("WatchdogAdminServer");
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


} // namespace WatchdogAgent
} // namespace Passenger

#endif /* _PASSENGER_WATCHDOG_AGENT_ADMIN_SERVER_H_ */
