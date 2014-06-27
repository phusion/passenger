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
#ifndef _PASSENGER_UTILS_JSON_UTILS_H_
#define _PASSENGER_UTILS_JSON_UTILS_H_

#include <string>
#include <StaticString.h>
#include <Utils/json.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;

inline string
stringifyJson(const Json::Value &value) {
	Json::FastWriter writer;
	string str = writer.write(value);
	str.erase(str.size() - 1, 1);
	return str;
}

/** `str` MUST be NULL-terminated! */
inline string
jsonString(const Passenger::StaticString &str) {
	return stringifyJson(Json::Value(Json::StaticString(str.data())));
}

} // namespace Passenger

#endif /* _PASSENGER_UTILS_JSON_UTILS_H_ */
