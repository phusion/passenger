/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_CLIENT_H_
#define _PASSENGER_UST_ROUTER_CLIENT_H_

#include <set>
#include <string>
#include <UstRouter/Transaction.h>
#include <ServerKit/Server.h>
#include <MessageReadersWriters.h>

namespace Passenger {
namespace UstRouter {

using namespace std;
using namespace boost;


class Client: public ServerKit::BaseClient {
public:
	enum State {
		READING_AUTH_USERNAME,
		READING_AUTH_PASSWORD,
		READING_MESSAGE,
		READING_MESSAGE_BODY
	};

	enum Type {
		UNINITIALIZED,
		LOGGER
	};

	ArrayMessage arrayReader;
	ScalarMessage scalarReader;

	State state;
	Type type;
	string nodeName;

	/**
	 * Set of transaction IDs opened by this client.
	 * @invariant This is a subset of the transaction IDs in the 'transactions' member.
	 */
	set<string> openTransactions;

	struct {
		TransactionPtr transaction;
		string timestamp;
		bool ack;
	} logCommandParams;

	Client(void *server)
		: ServerKit::BaseClient(server)
		{ }

	const char *getStateName() const {
		switch (state) {
		case READING_AUTH_USERNAME:
			return "UNINITIALIZED";
		case READING_AUTH_PASSWORD:
			return "READING_AUTH_PASSWORD";
		case READING_MESSAGE:
			return "READING_MESSAGE";
		case READING_MESSAGE_BODY:
			return "READING_MESSAGE_BODY";
		default:
			return "UNKNOWN";
		}
	}

	const char *getTypeName() const {
		switch (type) {
		case UNINITIALIZED:
			return "UNINITIALIZED";
		case LOGGER:
			return "LOGGER";
		default:
			return "UNKNOWN";
		}
	}

	DEFINE_SERVER_KIT_BASE_CLIENT_FOOTER(Client);
};


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_TRANSACTION_H_ */
