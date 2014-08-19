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
#ifndef _PASSENGER_SERVER_KIT_CLIENT_REF_H_
#define _PASSENGER_SERVER_KIT_CLIENT_REF_H_

#include <boost/move/core.hpp>

namespace Passenger {
namespace ServerKit {


template<typename Server, typename Client>
class ClientRef {
private:
	BOOST_COPYABLE_AND_MOVABLE(ClientRef);
	Client *client;

	static Server *getServer(Client *client) {
		return (Server *) client->getServer();
	}

public:
	explicit
	ClientRef(Client *_client)
		: client(_client)
	{
		if (_client != NULL) {
			getServer(_client)->_refClient(_client);
		}
	}

	ClientRef(const ClientRef &ref)
		: client(ref.client)
	{
		if (ref.client != NULL) {
			getServer(ref.client)->_refClient(ref.client);
		}
	}

	explicit
	ClientRef(BOOST_RV_REF(ClientRef) ref)
		: client(ref.client)
	{
		ref.client = NULL;
	}

	~ClientRef() {
		if (client != NULL) {
			getServer(client)->_unrefClient(client);
		}
	}

	Client *get() const {
		return client;
	}

	ClientRef &operator=(BOOST_COPY_ASSIGN_REF(ClientRef) ref) {
		if (client == ref.client) {
			Client *oldClient = client;
			client = ref.client;
			if (client != NULL) {
				getServer(client)->_refClient(client);
			}
			if (oldClient != NULL) {
				getServer(oldClient)->_unrefClient(oldClient);
			}
		}
		return *this;
	}

	ClientRef &operator=(BOOST_RV_REF(ClientRef) ref) {
		Client *oldClient = client;
		client = ref.client;
		ref.client = NULL;
		if (oldClient != NULL) {
			getServer(oldClient)->_unrefClient(oldClient);
		}
		return *this;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CLIENT_REF_H_ */
