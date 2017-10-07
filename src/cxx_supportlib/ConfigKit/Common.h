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

#include <StaticString.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


class Store;

enum Type {
	STRING_TYPE,
	INT_TYPE,
	UINT_TYPE,
	FLOAT_TYPE,
	BOOL_TYPE,

	ARRAY_TYPE,
	STRING_ARRAY_TYPE,

	OBJECT_TYPE,

	ANY_TYPE,

	UNKNOWN_TYPE
};

enum Flags {
	OPTIONAL = 0,
	REQUIRED = 1 << 0,
	CACHE_DEFAULT_VALUE = 1 << 1,
	READ_ONLY = 1 << 2,
	SECRET = 1 << 3,

	_DYNAMIC_DEFAULT_VALUE = 1 << 30,
	_FROM_SUBSCHEMA = 1 << 31
};

/** Represents a validation error. */
class Error {
private:
	static string dummyKeyProcessor(const StaticString &key) {
		return key.toString();
	}

	string rawMessage;

public:
	typedef boost::function<string (const StaticString &key)> KeyProcessor;

	Error() { }

	Error(const string &_rawMessage)
		: rawMessage(_rawMessage)
		{ }

	string getMessage() const {
		return getMessage(dummyKeyProcessor);
	}

	string getMessage(const KeyProcessor &processor) const {
		string result = rawMessage;
		string::size_type searchBegin = 0;
		bool done = false;

		while (!done) {
			string::size_type pos = result.find("{{", searchBegin);
			if (pos == string::npos) {
				done = true;
				break;
			}

			string::size_type endPos = result.find("}}", pos + 2);
			if (endPos == string::npos) {
				done = true;
				break;
			}

			string key = result.substr(pos + 2, endPos - pos - 2);
			string replacement = processor(key);
			result.replace(pos, endPos - pos + 2, replacement);
			searchBegin = pos + replacement.size();
		}
		return result;
	}

	bool operator<(const Error &other) const {
		return rawMessage < other.rawMessage;
	}
};

typedef boost::function<Json::Value (const Store &store)> ValueGetter;
typedef boost::function<Json::Value (const Json::Value &value)> ValueFilter;


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_COMMON_H_ */
