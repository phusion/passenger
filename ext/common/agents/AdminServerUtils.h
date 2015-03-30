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
#ifndef _PASSENGER_ADMIN_SERVER_UTILS_H_
#define _PASSENGER_ADMIN_SERVER_UTILS_H_

/**
 * Utility code shared by HelperAgent/AdminServer.h, LoggingAgent/AdminServer.h
 * and Watchdog/AdminServer.h. This code handles authentication and authorization
 * of connected AdminServer clients.
 */

#include <oxt/macros.hpp>
#include <oxt/backtrace.hpp>
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
#include <Utils/IOUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/modp_b64.h>
#include <Utils/VariantMap.h>

namespace Passenger {

using namespace std;


struct AdminAccount {
	string username;
	string password;
	bool readonly;
};

class AdminAccountDatabase {
private:
	vector<AdminAccount> database;

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
			throw ArgumentException("It is not allowed to register an admin account with username 'api'");
		}

		AdminAccount account;
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
		AdminAccount account;
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
			throw ArgumentException("It is not allowed to register an admin account with username 'api'");
		}
		database.push_back(account);
	}

	bool empty() const {
		return database.empty();
	}

	const AdminAccount *lookup(const StaticString &username) const {
		vector<AdminAccount>::const_iterator it, end = database.end();

		for (it = database.begin(); it != end; it++) {
			if (it->username == username) {
				return &(*it);
			}
		}

		return NULL;
	}
};

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

inline string
truncateApiKey(const StaticString &apiKey) {
	assert(apiKey.size() == ApplicationPool2::ApiKey::SIZE);
	char prefix[3];
	memcpy(prefix, apiKey.data(), 3);
	return string(prefix, 3) + "*****";
}

/*
 * @throws oxt::tracable_exception
 */
template<typename AdminServer, typename Client, typename Request>
inline Authorization
authorize(AdminServer *server, Client *client, Request *req) {
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

	if (server->adminAccountDatabase->empty()) {
		SKC_INFO_FROM_STATIC(server, client,
			"Authenticated as administrator because admin account database is empty");
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
			const AdminAccount *account = server->adminAccountDatabase->lookup(username);
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

template<typename AdminServer, typename Client, typename Request>
inline bool
authorizeStateInspectionOperation(AdminServer *server, Client *client, Request *req) {
	return authorize(server, client, req).canInspectState;
}

template<typename AdminServer, typename Client, typename Request>
inline bool
authorizeAdminOperation(AdminServer *server, Client *client, Request *req) {
	return authorize(server, client, req).canAdminister;
}

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


} // namespace Passenger

#endif /* _PASSENGER_ADMIN_SERVER_UTILS_H_ */
