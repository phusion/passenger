/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2015 Phusion
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
#include <cstdio>
#include <cstddef>
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

inline Json::Value
timeToJson(unsigned long long timestamp) {
	Json::Value doc;
	time_t time = (time_t) timestamp / 1000000;
	char buf[32];
	size_t len;

	doc["timestamp"] = timestamp / (double) 1000000;

	ctime_r(&time, buf);
	len = strlen(buf);
	if (len > 0) {
		// Get rid of trailing newline
		buf[len - 1] = '\0';
	}
	doc["local"] = buf;
	doc["relative"] = distanceOfTimeInWords(time) + " ago";

	return doc;
}

inline string
formatFloat(double val) {
	char buf[64];
	int size = snprintf(buf, sizeof(buf), "%.1f", val);
	return string(buf, size);
}

inline Json::Value
byteSizeToJson(size_t size) {
	Json::Value doc;
	doc["bytes"] = (Json::UInt64) size;
	if (size < 1024) {
		doc["human_readable"] = toString(size) + " bytes";
	} else if (size < 1024 * 1024) {
		doc["human_readable"] = formatFloat(size / 1024.0) + " KB";
	} else {
		doc["human_readable"] = formatFloat(size / 1024.0 / 1024.0) + " MB";
	}
	return doc;
}

inline Json::Value
signedByteSizeToJson(long long size) {
	Json::Value doc;
	long long absSize = (size < 0) ? -size : size;
	doc["bytes"] = (Json::Int64) size;
	if (absSize < 1024) {
		doc["human_readable"] = toString(size) + " bytes";
	} else if (absSize < 1024 * 1024) {
		doc["human_readable"] = formatFloat(size / 1024.0) + " KB";
	} else {
		doc["human_readable"] = formatFloat(size / 1024.0 / 1024.0) + " MB";
	}
	return doc;
}

} // namespace Passenger

#endif /* _PASSENGER_UTILS_JSON_UTILS_H_ */
