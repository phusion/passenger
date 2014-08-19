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
#ifndef _PASSENGER_SERVER_KIT_HTTP_REQUEST_REF_H_
#define _PASSENGER_SERVER_KIT_HTTP_REQUEST_REF_H_

#include <boost/move/core.hpp>

namespace Passenger {
namespace ServerKit {


template<typename Server, typename Request>
class HttpRequestRef {
private:
	BOOST_COPYABLE_AND_MOVABLE(HttpRequestRef);
	Request *request;

	static Server *getServer(Request *request) {
		return (Server *) request->client->getServer();
	}

public:
	explicit
	HttpRequestRef(Request *_request)
		: request(_request)
	{
		if (_request != NULL) {
			getServer(_request)->_refRequest(_request);
		}
	}

	HttpRequestRef(const HttpRequestRef &ref)
		: request(ref.request)
	{
		if (ref.request != NULL) {
			getServer(ref.request)->_refRequest(ref.request);
		}
	}

	explicit
	HttpRequestRef(BOOST_RV_REF(HttpRequestRef) ref)
		: request(ref.request)
	{
		ref.request = NULL;
	}

	~HttpRequestRef() {
		if (request != NULL) {
			getServer(request)->_unrefRequest(request);
		}
	}

	Request *get() const {
		return request;
	}

	HttpRequestRef &operator=(BOOST_COPY_ASSIGN_REF(HttpRequestRef) ref) {
		if (request == ref.request) {
			Request *oldRequest = request;
			request = ref.request;
			if (request != NULL) {
				getServer(request)->_refRequest(request);
			}
			if (oldRequest != NULL) {
				getServer(oldRequest)->_unrefRequest(oldRequest);
			}
		}
		return *this;
	}

	HttpRequestRef &operator=(BOOST_RV_REF(HttpRequestRef) ref) {
		Request *oldRequest = request;
		request = ref.request;
		ref.request = NULL;
		if (oldRequest != NULL) {
			getServer(oldRequest)->_unrefRequest(oldRequest);
		}
		return *this;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HTTP_REQUEST_REF_H_ */
