/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2015 Phusion
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
#ifndef _PASSENGER_API_SERVER_UTILS_H_
#define _PASSENGER_API_SERVER_UTILS_H_

/**
 * Utility code shared by HelperAgent/ApiServer.h, LoggingAgent/ApiServer.h
 * and Watchdog/ApiServer.h. This code handles authentication and authorization
 * of connected ApiServer clients.
 *
 * This file consists of the following items.
 *
 * ## API accounts
 *
 * API servers can be password protected. They support multiple accounts,
 * each with its own privilege level. These accounts are represented by
 * ApiAccount, stored in ApiAccountDatabase objects.
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
#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/thread.hpp>
#include <sys/types.h>
#include <string>
#include <vector>
#include <cstddef>
#include <cstring>
#include <ApplicationPool2/Pool.h>
#include <ApplicationPool2/ApiKey.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <DataStructures/LString.h>
#include <ServerKit/Server.h>
#include <ServerKit/HeaderTable.h>
#include <Utils.h>
#include <Utils/IOUtils.h>
#include <Utils/BufferedIO.h>
#include <Utils/StrIntUtils.h>
#include <Utils/modp_b64.h>
#include <Utils/json.h>
#include <Utils/VariantMap.h>

namespace Passenger {

using namespace std;


// Forward declarations
inline string truncateApiKey(const StaticString &apiKey);


/*******************************
 *
 * API accounts
 *
 *******************************/

struct ApiAccount {
	string username;
	string password;
	bool readonly;
};

class ApiAccountDatabase {
private:
	vector<ApiAccount> database;

	bool levelDescriptionIsReadOnly(const StaticString &level) const {
		if (level == "readonly") {
			return true;
		} else if (level == "full") {
			return false;
		} else {
			throw ArgumentException("Invalid privilege level " + level);
		}
	}

public:
	/**
	 * Add an account to the database with the given parameters.
	 *
	 * @throws ArgumentException One if the input arguments contain a disallowed value.
	 */
	void add(const string &username, const string &password, bool readonly) {
		if (OXT_UNLIKELY(username == "api")) {
			throw ArgumentException("It is not allowed to register an API account with username 'api'");
		}

		ApiAccount account;
		account.username = username;
		account.password = password;
		account.readonly = readonly;
		database.push_back(account);
	}

	/**
	 * Add an account to the database. The account parameters are determined
	 * by a description string in the form of [LEVEL]:USERNAME:PASSWORDFILE.
	 * LEVEL is one of:
	 *
	 *   readonly    Read-only access
     *   full        Full access (default)
	 *
	 * @throws ArgumentException One if the input arguments contain a disallowed value.
	 */
	void add(const StaticString &description) {
		ApiAccount account;
		vector<string> args;

		split(description, ':', args);

		if (args.size() == 2) {
			account.username = args[0];
			account.password = strip(readAll(args[1]));
			account.readonly = false;
		} else if (args.size() == 3) {
			account.username = args[1];
			account.password = strip(readAll(args[2]));
			account.readonly = levelDescriptionIsReadOnly(args[0]);
		} else {
			throw ArgumentException("Invalid authorization description '" + description + "'");
		}

		if (OXT_UNLIKELY(account.username == "api")) {
			throw ArgumentException("It is not allowed to register an API account with username 'api'");
		}
		database.push_back(account);
	}

	bool empty() const {
		return database.empty();
	}

	const ApiAccount *lookup(const StaticString &username) const {
		vector<ApiAccount>::const_iterator it, end = database.end();

		for (it = database.begin(); it != end; it++) {
			if (it->username == username) {
				return &(*it);
			}
		}

		return NULL;
	}
};


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
		: uid(-1),
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

	if (server->apiAccountDatabase->empty()) {
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
			const ApiAccount *account = server->apiAccountDatabase->lookup(username);
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
apiServerProcessVersion(Server *server, Client *client, Request *req) {
	Authorization auth(authorize(server, client, req));
	if (auth.canReadPool || auth.canInspectState) {
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "application/json");

		Json::Value response;
		response["program_name"] = PROGRAM_NAME;
		response["program_version"] = PASSENGER_VERSION;
		response["api_version"] = PASSENGER_API_VERSION;
		response["api_version_major"] = PASSENGER_API_VERSION_MAJOR;
		response["api_version_minor"] = PASSENGER_API_VERSION_MINOR;
		#ifdef PASSENGER_IS_ENTERPRISE
			response["passenger_enterprise"] = true;
		#endif

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
		int e;
		ServerKit::HeaderTable headers;
		headers.insert(req->pool, "Content-Type", "application/json");

		string logFile = getLogFile();
		if (logFile.empty()) {
			server->writeSimpleResponse(client, 500, &headers, "{ \"status\": \"error\", "
				"\"code\": \"NO_LOG_FILE\", "
				"\"message\": \"" PROGRAM_NAME " was not configured with a log file.\" }\n");
			if (!req->ended()) {
				server->endRequest(&client, &req);
			}
			return;
		}

		if (!setLogFile(logFile, &e)) {
			unsigned int bufsize = 1024;
			char *message = (char *) psg_pnalloc(req->pool, bufsize);
			snprintf(message, bufsize, "{ \"status\": \"error\", "
				"\"code\": \"LOG_FILE_OPEN_ERROR\", "
				"\"message\": \"Cannot reopen log file %s: %s (errno=%d)\" }",
				logFile.c_str(), strerror(e), e);
			server->writeSimpleResponse(client, 500, &headers, message);
			if (!req->ended()) {
				server->endRequest(&client, &req);
			}
			return;
		}
		P_NOTICE("Log file reopened.");

		if (hasFileDescriptorLogFile()) {
			if (!setFileDescriptorLogFile(getFileDescriptorLogFile(), &e)) {
				unsigned int bufsize = 1024;
				char *message = (char *) psg_pnalloc(req->pool, bufsize);
				snprintf(message, bufsize, "{ \"status\": \"error\", "
					"\"code\": \"FD_LOG_FILE_OPEN_ERROR\", "
					"\"message\": \"Cannot reopen file descriptor log file %s: %s (errno=%d)\" }",
					getFileDescriptorLogFile().c_str(), strerror(e), e);
				server->writeSimpleResponse(client, 500, &headers, message);
				if (!req->ended()) {
					server->endRequest(&client, &req);
				}
				return;
			}
			P_NOTICE("File descriptor log file reopened.");
		}

		server->writeSimpleResponse(client, 200, &headers, "{ \"status\": \"ok\" }\n");

		if (!req->ended()) {
			server->endRequest(&client, &req);
		}
	} else {
		apiServerRespondWith401(server, client, req);
	}
}

template<typename Server, typename Client, typename Request>
struct ApiServerProcessReinheritLogsParams {
	struct Result {
		Server *server;
		Client *client;
		Request *req;
		vector<string> debugLogs;
		string errorLogs;
		int status;
		string response;
	};

	typedef void (*Callback)(Result result);

	Server *server;
	Client *client;
	Request *req;
	string instanceDir;
	string fdPassingPassword;
	Callback callback;
};

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessReinheritLogsThreadMain(ApiServerProcessReinheritLogsParams<Server, Client, Request> params) {
	typedef ApiServerProcessReinheritLogsParams<Server, Client, Request> Params;
	typedef typename Params::Result Result;

	struct Guard {
		Params &params;
		Result &result;
		SafeLibevPtr libev;
		bool cleared;

		Guard(Params &_params, Result &_result, const SafeLibevPtr &_libev)
			: params(_params),
			  result(_result),
			  libev(_libev),
			  cleared(false)
			{ }

		~Guard() {
			if (!cleared) {
				result.status = 500;
				result.response = "{ \"status\": \"error\", "
					"\"code\": \"INTERNAL_ERROR\", "
					"\"message\": \"Internal error\" }\n";
				libev->runLater(boost::bind(params.callback, result));
			}
		}

		void clear() {
			cleared = true;
		}
	};

	Result result;
	SafeLibevPtr libev = params.server->getContext()->libev;

	result.server = params.server;
	result.client = params.client;
	result.req    = params.req;
	result.status = 500;

	Guard guard(params, result, libev);


	try {
		FileDescriptor watchdog(connectToUnixServer(
			params.instanceDir + "/agents.s/watchdog_api",
			NULL, 0), __FILE__, __LINE__);
		writeExact(watchdog,
			"GET /config/log_file.fd HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Fd-Passing-Password: " + params.fdPassingPassword + "\r\n"
			"\r\n");

		BufferedIO io(watchdog);
		string response = io.readLine();
		result.debugLogs.push_back("Watchdog response: \"" + cEscapeString(response) + "\"");

		if (response != "HTTP/1.1 200 OK\r\n") {
			watchdog.close();
			guard.clear();
			result.status = 500;
			result.response = "{ \"status\": \"error\", "
				"\"code\": \"INHERIT_ERROR\", "
				"\"message\": \"Error communicating with Watchdog process: non-200 response\" }\n";
			libev->runLater(boost::bind(params.callback, result));
			return;
		}

		string logFilePath;
		while (true) {
			response = io.readLine();
			result.debugLogs.push_back("Watchdog response: \"" + cEscapeString(response) + "\"");
			if (response.empty()) {
				watchdog.close();
				guard.clear();
				result.status = 500;
				result.response = "{ \"status\": \"error\", "
					"\"code\": \"INHERIT_ERROR\", "
					"\"message\": \"Error communicating with Watchdog process: "
						"premature EOF encountered in response\" }\n";
				libev->runLater(boost::bind(params.callback, result));
				return;
			} else if (response == "\r\n") {
				break;
			} else if (startsWith(response, "filename: ")
				|| startsWith(response, "Filename: "))
			{
				response.erase(0, strlen("filename: "));
				logFilePath = response;
			}
		}

		if (logFilePath.empty()) {
			watchdog.close();
			guard.clear();
			result.status = 500;
			result.response = "{ \"status\": \"error\", "
				"\"code\": \"INHERIT_ERROR\", "
				"\"message\": \"Error communicating with Watchdog process: "
					"no log filename received in response\" }\n";
			libev->runLater(boost::bind(params.callback, result));
			return;
		}

		unsigned long long timeout = 1000000;
		int fd = readFileDescriptorWithNegotiation(watchdog, &timeout);
		setLogFileWithFd(logFilePath, fd);
		safelyClose(fd);
		watchdog.close();

		guard.clear();
		result.status = 200;
		result.response = "{ \"status\": \"ok\" }\n";
		libev->runLater(boost::bind(params.callback, result));
	} catch (const oxt::tracable_exception &e) {
		result.errorLogs.append("Exception: ");
		result.errorLogs.append(e.what());
		result.errorLogs.append("\n");
		result.errorLogs.append(e.backtrace());
	}
}

template<typename Server, typename Client, typename Request>
inline void
apiServerProcessReinheritLogsDone(typename ApiServerProcessReinheritLogsParams<Server,
	Client, Request>::Result result)
{
	Server *server = result.server;
	Client *client = result.client;
	Request *req   = result.req;

	if (!result.debugLogs.empty()) {
		foreach (string log, result.debugLogs) {
			SKC_DEBUG_FROM_STATIC(server, client, log);
		}
	}
	if (!result.errorLogs.empty()) {
		SKC_ERROR_FROM_STATIC(server, client, result.errorLogs);
	}

	if (req->ended()) {
		server->unrefRequest(req, __FILE__, __LINE__);
		return;
	}

	ServerKit::HeaderTable headers;
	headers.insert(req->pool, "Cache-Control", "no-cache, no-store, must-revalidate");
	headers.insert(req->pool, "Content-Type", "application/json");

	req->wantKeepAlive = false;
	server->writeSimpleResponse(client, result.status, &headers, result.response);
	Request *req2 = req;
	if (!req->ended()) {
		server->endRequest(&client, &req);
	}
	server->unrefRequest(req2, __FILE__, __LINE__);
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

		ApiServerProcessReinheritLogsParams<Server, Client, Request> params;
		params.server = server;
		params.client = client;
		params.req    = req;
		params.instanceDir = instanceDir;
		params.fdPassingPassword = fdPassingPassword;
		params.callback = apiServerProcessReinheritLogsDone<Server, Client, Request>;

		server->refRequest(req, __FILE__, __LINE__);
		oxt::thread(boost::bind(
			apiServerProcessReinheritLogsThreadMain<Server, Client, Request>,
			params), "API command: reinherit logs", 1024 * 128);
	} else {
		apiServerRespondWith401(server, client, req);
	}
}


} // namespace Passenger

#endif /* _PASSENGER_API_SERVER_UTILS_H_ */
