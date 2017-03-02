/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_CONFIG_KIT_COMMON_H_
#define _PASSENGER_CONFIG_KIT_COMMON_H_

#include <boost/function.hpp>
#include <string>
#include <vector>

#include <jsoncpp/json.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


class Store;

enum Type {
	STRING_TYPE,
	INTEGER_TYPE,
	UNSIGNED_INTEGER_TYPE,
	FLOAT_TYPE,
	BOOLEAN_TYPE,
	UNKNOWN_TYPE
};

enum Flags {
	OPTIONAL = 0,
	REQUIRED = 1 << 0,
	CACHE_DEFAULT_VALUE = 1 << 1,
	READ_ONLY = 1 << 2,

	_DYNAMIC_DEFAULT_VALUE = 1 << 30,
	_FROM_SUBSCHEMA = 1 << 31
};

/** Represents a validation error. */
struct Error {
	/** The configuration key on which validation failed. */
	string key;
	/** The error message. */
	string message;

	Error() { }

	Error(const string &_key, const string &_message)
		: key(_key),
		  message(_message)
		{ }

	string getFullMessage() const {
		if (key.empty()) {
			return message;
		} else {
			return "'" + key + "' " + message;
		}
	}

	bool operator<(const Error &other) const {
		return getFullMessage() < other.getFullMessage();
	}
};

typedef boost::function<Json::Value (const Store *store)> ValueGetter;
typedef boost::function<void (const Json::Value &config, const vector<Error> &errors)> ConfigCallback;
typedef boost::function<void (const Json::Value &config)> InspectCallback;


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_COMMON_H_ */
