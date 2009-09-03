/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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
#include <boost/shared_ptr.hpp>
#include "StaticString.h"

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
 *   with ZeroMemoryGuard (Utils.h) as soon as possible. Do not use memset(), the code
 *   for ZeroMemoryGuard explains why.
 */

class Account {
public:
	enum Rights {
		ALL                     = ~0,
		NONE                    = 0,
		GET                     = 1 << 0,
		CLEAR                   = 1 << 1,
		GET_PARAMETERS          = 1 << 2,
		SET_PARAMETERS          = 1 << 3,
		INSPECT_BASIC_INFO      = 1 << 4,
		INSPECT_SENSITIVE_INFO  = 1 << 5,
	};

private:
	string username;
	string passwordOrHash;
	bool hashGiven;
	Rights rights;

public:
	Account(const string &username, const string &passwordOrHash, bool hashGiven, Rights rights = ALL) {
		this->username       = username;
		this->passwordOrHash = passwordOrHash;
		this->hashGiven      = hashGiven;
		this->rights         = rights;
	}
	
	bool checkPasswordOrHash(const StaticString &userSuppliedPassword) const {
		if (hashGiven) {
			return passwordOrHash == createHash(userSuppliedPassword);
		} else {
			return userSuppliedPassword == passwordOrHash;
		}
	}
	
	// Urgh, I can't use 'Rights' here as type because apparently bitwise
	// ORing two enums result in an int type.
	
	bool hasRights(int rights) const {
		return this->rights & rights;
	}
	
	void setRights(int rights) {
		this->rights = (Rights) rights;
	}
	
	static string createHash(const StaticString &userSuppliedPassword) {
		// TODO: use bcrypt
		return userSuppliedPassword;
	}
};

typedef shared_ptr<Account> AccountPtr;

} // namespace Passenger

#endif /* _PASSENGER_ACCOUNT_H_ */
