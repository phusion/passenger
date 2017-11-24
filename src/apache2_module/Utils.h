/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_APACHE2_MODULE_UTILS_H_
#define _PASSENGER_APACHE2_MODULE_UTILS_H_

#include <string>
#include <StaticString.h>

// The APR headers must come after the Passenger headers.
// See Hooks.cpp to learn why.
#include <httpd.h>
#include <http_config.h>
#include <apr_pools.h>
#include <apr_strings.h>

namespace Passenger {
namespace Apache2Module {

using namespace std;


inline void
addHeader(string &headers, const StaticString &name, const char *value) {
	if (value != NULL) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		headers.append(value);
		headers.append("\r\n", 2);
	}
}

inline void
addHeader(string &headers, const StaticString &name, const StaticString &value) {
	if (!value.empty()) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		headers.append(value.data(), value.size());
		headers.append("\r\n", 2);
	}
}

inline void
addHeader(request_rec *r, string &headers, const StaticString &name, int value) {
	if (value != UNSET_INT_VALUE) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		headers.append(apr_psprintf(r->pool, "%d", value));
		headers.append("\r\n", 2);
	}
}

inline void
addHeader(string &headers, const StaticString &name, Apache2Module::Threeway value) {
	if (value != Apache2Module::UNSET) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		if (value == Apache2Module::ENABLED) {
			headers.append("t", 1);
		} else {
			headers.append("f", 1);
		}
		headers.append("\r\n", 2);
	}
}


} // namespace Apache2Module
} // namespace Passenger

#endif /* _PASSENGER_APACHE2_MODULE_UTILS_H_ */
