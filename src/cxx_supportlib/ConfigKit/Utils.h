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
#ifndef _PASSENGER_CONFIG_KIT_UTILS_H_
#define _PASSENGER_CONFIG_KIT_UTILS_H_

#include <string>
#include <vector>

#include <ConfigKit/Common.h>
#include <StaticString.h>
#include <DataStructures/StringKeyTable.h>
#include <Utils/FastStringStream.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;

class Error;


inline StaticString
getTypeString(Type type) {
	switch (type) {
	case STRING_TYPE:
		return P_STATIC_STRING("string");
	case INT_TYPE:
		return P_STATIC_STRING("integer");
	case UINT_TYPE:
		return P_STATIC_STRING("unsigned integer");
	case FLOAT_TYPE:
		return P_STATIC_STRING("float");
	case BOOL_TYPE:
		return P_STATIC_STRING("boolean");
	case ARRAY_TYPE:
		return P_STATIC_STRING("array");
	case STRING_ARRAY_TYPE:
		return P_STATIC_STRING("array of strings");
	case OBJECT_TYPE:
		return P_STATIC_STRING("object");
	case ANY_TYPE:
		return P_STATIC_STRING("any");
	default:
		return P_STATIC_STRING("unknown");
	}
}

inline vector<ConfigKit::Error>
deduplicateErrors(const vector<ConfigKit::Error> &errors) {
	StringKeyTable<bool> messagesSeen;
	vector<ConfigKit::Error>::const_iterator it, end = errors.end();
	vector<ConfigKit::Error> result;

	for (it = errors.begin(); it != end; it++) {
		bool *tmp;
		string message = it->getMessage();

		if (!messagesSeen.lookup(message, &tmp)) {
			messagesSeen.insert(message, true);
			result.push_back(*it);
		}
	}

	return result;
}

inline string
toString(const vector<Error> &errors) {
	FastStringStream<> stream;
	vector<Error>::const_iterator it, end = errors.end();

	for (it = errors.begin(); it != end; it++) {
		if (it != errors.begin()) {
			stream << "; ";
		}
		stream << it->getMessage();
	}
	return string(stream.data(), stream.size());
}


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_UTILS_H_ */
