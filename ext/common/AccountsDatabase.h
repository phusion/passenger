/*
 *  Phusion Passenger - http://www.modrails.com/
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
#ifndef _PASSENGER_ACCOUNTS_DATABASE_H_
#define _PASSENGER_ACCOUNTS_DATABASE_H_

#include <string>
#include <map>
#include <boost/shared_ptr.hpp>
#include "Account.h"
#include "ServerInstanceDir.h"
#include "StaticString.h"

/* This source file follows the security guidelines written in Account.h. */

namespace Passenger {

using namespace std;
using namespace boost;

class AccountsDatabase;
typedef shared_ptr<AccountsDatabase> AccountsDatabasePtr;


class AccountsDatabase {
private:
	map<string, AccountPtr> accounts;
	
public:
	static AccountsDatabasePtr createDefault(const ServerInstanceDir::GenerationPtr &generation,
	                                         bool userSwitching, const string &defaultUser);
	
	AccountPtr add(const string &username, const string &passwordOrHash, bool hashGiven, int rights = Account::ALL) {
		AccountPtr account(new Account(username, passwordOrHash, hashGiven, rights));
		accounts[username] = account;
		return account;
	}
	
	const AccountPtr get(const string &username) const {
		map<string, AccountPtr>::const_iterator it(accounts.find(username));
		if (it == accounts.end()) {
			return AccountPtr();
		} else {
			return it->second;
		}
	}
	
	AccountPtr authenticate(const string &username, const StaticString &userSuppliedPassword) {
		map<string, AccountPtr>::iterator it = accounts.find(username);
		if (it == accounts.end()) {
			return AccountPtr();
		} else {
			AccountPtr account(it->second);
			if (account->checkPasswordOrHash(userSuppliedPassword)) {
				return account;
			} else {
				return AccountPtr();
			}
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_ACCOUNTS_DATABASE_H_ */
