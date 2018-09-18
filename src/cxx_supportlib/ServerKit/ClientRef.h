/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
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
	const char *file;
	unsigned int line;

	static Server *getServer(Client *client) {
		return static_cast<Server *>(
			static_cast<typename Server::BaseClass *>(client->getServerBaseClassPointer())
		);
	}

public:
	explicit
	ClientRef(Client *_client, const char *_file, unsigned int _line)
		: client(_client),
		  file(_file),
		  line(_line)
	{
		if (_client != NULL) {
			getServer(_client)->_refClient(_client, _file, _line);
		}
	}

	ClientRef(const ClientRef &ref)
		: client(ref.client),
		  file(ref.file),
		  line(ref.line)
	{
		if (ref.client != NULL) {
			getServer(ref.client)->_refClient(ref.client, ref.file, ref.line);
		}
	}

	ClientRef(const ClientRef &ref, const char *_file, unsigned int _line)
		: client(ref.client),
		  file(_file),
		  line(_line)
	{
		if (ref.client != NULL) {
			getServer(ref.client)->_refClient(ref.client, _file, _line);
		}
	}

	explicit
	ClientRef(BOOST_RV_REF(ClientRef) ref)
		: client(ref.client),
		  file(ref.file),
		  line(ref.line)
	{
		ref.client = NULL;
		ref.file = NULL;
		ref.line = 0;
	}

	~ClientRef() {
		if (client != NULL) {
			getServer(client)->_unrefClient(client, file, line);
		}
	}

	Client *get() const {
		return client;
	}

	ClientRef &operator=(BOOST_COPY_ASSIGN_REF(ClientRef) ref) {
		if (this != &ref) {
			Client *oldClient = client;
			const char *oldFile = file;
			unsigned int oldLine = line;
			client = ref.client;
			file = ref.file;
			line = ref.line;
			if (client != NULL) {
				getServer(client)->_refClient(ref.client, ref.file, ref.line);
			}
			if (oldClient != NULL) {
				getServer(oldClient)->_unrefClient(oldClient, oldFile, oldLine);
			}
		}
		return *this;
	}

	ClientRef &operator=(BOOST_RV_REF(ClientRef) ref) {
		if (this != &ref) {
			Client *oldClient = client;
			client = ref.client;
			file = ref.file;
			line = ref.line;
			ref.client = NULL;
			ref.file = NULL;
			ref.line = 0;
			if (oldClient != NULL) {
				getServer(oldClient)->_unrefClient(oldClient, file, line);
			}
		}
		return *this;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CLIENT_REF_H_ */
