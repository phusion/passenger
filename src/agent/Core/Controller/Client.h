/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#ifndef _PASSENGER_REQUEST_HANDLER_CLIENT_H_
#define _PASSENGER_REQUEST_HANDLER_CLIENT_H_

#include <ev++.h>
#include <ServerKit/HttpClient.h>
#include <Core/Controller/Request.h>

namespace Passenger {
namespace Core {

using namespace std;
using namespace boost;
using namespace ApplicationPool2;


class Client: public ServerKit::BaseHttpClient<Request> {
public:
	ev_tstamp connectedAt;

	Client(void *server)
		: ServerKit::BaseHttpClient<Request>(server)
	{
		SERVER_KIT_BASE_HTTP_CLIENT_INIT();
	}

	DEFINE_SERVER_KIT_BASE_HTTP_CLIENT_FOOTER(Passenger::Core::Client,
		Passenger::Core::Request);
};


} // namespace Client
} // namespace Passenger

#endif /* _PASSENGER_REQUEST_HANDLER_CLIENT_H_ */
