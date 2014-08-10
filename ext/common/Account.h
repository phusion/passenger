/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_ACCOUNT_H_
#define _PASSENGER_ACCOUNT_H_

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <StaticString.h>
#include <Exceptions.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace boost;
using namespace std;

/*
 * SECURITY NOTES
 *
 * We want to avoid storing plain text passwords in memory, because attackers may be able
 * to read this process's memory, e.g. through core dumps or debuggers. So in this source
 * file, as well as in several others, we follow these guidelines:
 *
 * - Variables and arguments named userSuppliedPassword represent passwords
 *   supplied by a human, i.e. user input. These variables and arguments have the type
 *   'StaticString' instead of 'string', because we want to avoid accidentally copying
 *   the password values.
 * - Variables and arguments named passwordOrHash might also represent passwords. However,
 *   if it is a password, then it is guaranteed NOT to be supplied by a human, e.g. it's
 *   randomly generated. Therefore it's okay for passwordOrHash to be of type 'string'.
 * - If there is a need to copy the password for whatever reason, then it must be cleared
 *   with ZeroMemoryGuard as soon as possible. Do not use memset(), the code
 *   for ZeroMemoryGuard explains why.
 */

class Account {
public:
	enum Rights {
		ALL                       = ~0,
		NONE                      = 0,

		// HelperAgent ApplicationPool rights.
		CLEAR                     = 1 << 0,
		DETACH                    = 1 << 1,
		SET_PARAMETERS            = 1 << 2,
		RESTART                   = 1 << 3,
		INSPECT_BASIC_INFO        = 1 << 4,
		INSPECT_SENSITIVE_INFO    = 1 << 5,

		// HelperAgent admin rights.
		INSPECT_REQUESTS          = 1 << 8,
		INSPECT_BACKTRACES        = 1 << 9,

		// Other rights.
		EXIT                      = 1 << 31
	};

private:
	string username;
	string passwordOrHash;
	bool hashGiven;
	Rights rights;

public:
	// Urgh, I can't use 'Rights' here as type because apparently bitwise
	// ORing two enums results in an int type.

	static Rights parseRightsString(const string &str, int defaultValue = NONE) {
		vector<string> rights_vec;
		vector<string>::const_iterator it;
		int result = defaultValue;

		split(str, ',', rights_vec);
		for (it = rights_vec.begin(); it != rights_vec.end(); it++) {
			if (*it == "all") {
				result = ALL;
			} else if (*it == "none") {
				result = NONE;

			} else if (*it == "clear") {
				result |= CLEAR;
			} else if (*it == "detach") {
				result |= DETACH;
			} else if (*it == "set_parameters") {
				result |= SET_PARAMETERS;
			} else if (*it == "inspect_basic_info") {
				result |= INSPECT_BASIC_INFO;
			} else if (*it == "inspect_sensitive_info") {
				result |= INSPECT_SENSITIVE_INFO;

			} else if (*it == "inspect_requests") {
				result |= INSPECT_REQUESTS;
			} else if (*it == "inspect_backtraces") {
				result |= INSPECT_BACKTRACES;

			} else if (*it == "exit") {
				result |= EXIT;

			} else if (*it != "") {
				throw ArgumentException("Unknown right '" + *it + "'.");
			}
		}

		return (Rights) result;
	}

	Account(const string &username, const string &passwordOrHash, bool hashGiven, int rights = ALL) {
		this->username       = username;
		this->passwordOrHash = passwordOrHash;
		this->hashGiven      = hashGiven;
		this->rights         = (Rights) rights;
	}

	bool checkPasswordOrHash(const StaticString &userSuppliedPassword) const {
		if (hashGiven) {
			return passwordOrHash == createHash(userSuppliedPassword);
		} else {
			return userSuppliedPassword == passwordOrHash;
		}
	}

	bool hasRights(int rights) const {
		return this->rights & rights;
	}

	void setRights(int rights) {
		this->rights = (Rights) rights;
	}

	string getUsername() const {
		return username;
	}

	string getRawPassword() const {
		return passwordOrHash;
	}

	static string createHash(const StaticString &userSuppliedPassword) {
		// TODO: use bcrypt or something
		return userSuppliedPassword;
	}
};

typedef boost::shared_ptr<Account> AccountPtr;

} // namespace Passenger

#endif /* _PASSENGER_ACCOUNT_H_ */
