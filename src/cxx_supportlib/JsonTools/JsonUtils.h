/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_JSON_TOOLS_JSON_UTILS_H_
#define _PASSENGER_JSON_TOOLS_JSON_UTILS_H_

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <jsoncpp/json.h>
#include <boost/cstdint.hpp>
#include <StaticString.h>
#include <SystemTools/SystemTime.h>
#include <StrIntTools/StrIntUtils.h>
#include <Utils/VariantMap.h>

namespace Passenger {

using namespace std;


/**************************************************************
 *
 * Methods for querying fields from a JSON document.
 * If the field is missing, thhese methods can either return
 * a default value, or throw an exception.
 *
 **************************************************************/

inline const Json::Value &
getJsonField(const Json::Value &json, const char *key) {
	Json::StaticString theKey(key);
	if (json.isMember(theKey)) {
		return json[theKey];
	} else {
		throw VariantMap::MissingKeyException(key);
	}
}

inline Json::Value &
getJsonField(Json::Value &json, const char *key) {
	Json::StaticString theKey(key);
	if (json.isMember(theKey)) {
		return json[theKey];
	} else {
		throw VariantMap::MissingKeyException(key);
	}
}


inline int
getJsonIntField(const Json::Value &json, const char *key) {
	if (json.isMember(key)) {
		return json[key].asInt();
	} else {
		throw VariantMap::MissingKeyException(key);
	}
}

inline int
getJsonIntField(const Json::Value &json, const Json::StaticString &key) {
	if (json.isMember(key)) {
		return json[key].asInt();
	} else {
		throw VariantMap::MissingKeyException(key.c_str());
	}
}

inline int
getJsonIntField(const Json::Value &json, const Json::StaticString &key, int defaultValue) {
	if (json.isMember(key)) {
		return json[key].asInt();
	} else {
		return defaultValue;
	}
}

inline void
getJsonIntField(const Json::Value &json, const Json::StaticString &key, int *result) {
	if (json.isMember(key)) {
		*result = json[key].asInt();
	}
}

inline void
getJsonIntField(const Json::Value &json, const string &key, int *result) {
	if (json.isMember(key)) {
		*result = json[key].asInt();
	}
}


inline unsigned int
getJsonUintField(const Json::Value &json, const Json::StaticString &key) {
	if (json.isMember(key)) {
		return json[key].asUInt();
	} else {
		throw VariantMap::MissingKeyException(key.c_str());
	}
}

inline unsigned int
getJsonUintField(const Json::Value &json, const Json::StaticString &key, unsigned int defaultValue) {
	if (json.isMember(key)) {
		return json[key].asUInt();
	} else {
		return defaultValue;
	}
}

inline void
getJsonUintField(const Json::Value &json, const Json::StaticString &key, unsigned int *result) {
	if (json.isMember(key)) {
		*result = json[key].asUInt();
	}
}

inline void
getJsonUintField(const Json::Value &json, const string &key, unsigned int *result) {
	if (json.isMember(key)) {
		*result = json[key].asUInt();
	}
}


inline boost::uint64_t
getJsonUint64Field(const Json::Value &json, const char *key) {
	Json::StaticString theKey(key);
	if (json.isMember(theKey)) {
		return json[theKey].asUInt64();
	} else {
		throw VariantMap::MissingKeyException(key);
	}
}

inline boost::uint64_t
getJsonUint64Field(const Json::Value &json, const char *key, unsigned int defaultValue) {
	Json::StaticString theKey(key);
	if (json.isMember(theKey)) {
		return json[theKey].asUInt64();
	} else {
		return defaultValue;
	}
}


inline bool
getJsonBoolField(const Json::Value &json, const char *key) {
	if (json.isMember(key)) {
		return json[key].asBool();
	} else {
		throw VariantMap::MissingKeyException(key);
	}
}


inline StaticString
getJsonStaticStringField(const Json::Value &json, const char *key) {
	if (json.isMember(key)) {
		return json[key].asCString();
	} else {
		throw VariantMap::MissingKeyException(key);
	}
}

inline StaticString
getJsonStaticStringField(const Json::Value &json, const Json::StaticString &key) {
	if (json.isMember(key)) {
		return json[key].asCString();
	} else {
		throw VariantMap::MissingKeyException(key.c_str());
	}
}

inline StaticString
getJsonStaticStringField(const Json::Value &json, const Json::StaticString &key,
	const StaticString &defaultValue)
{
	if (json.isMember(key)) {
		return json[key].asCString();
	} else {
		return defaultValue;
	}
}


/**************************************************************
 *
 * Methods for generating JSON.
 *
 **************************************************************/

/**
 * Returns a JSON document as its string representation.
 * This string is not prettified and does not contain a
 * trailing newline.
 *
 *     Json::Value doc;
 *     doc["foo"] = "bar";
 *     cout << stringifyJson(doc) << endl;
 *     // Prints:
 *     // {"foo": "bar"}
 */
inline string
stringifyJson(const Json::Value &value) {
	Json::FastWriter writer;
	string str = writer.write(value);
	str.erase(str.size() - 1, 1);
	return str;
}

/**
 * Encodes the given string as a JSON string. `str` MUST be NULL-terminated!
 *
 *     cout << jsonString("hello \"user\"") << endl;
 *     // Prints:
 *     // "hello \"user\""
 */
inline string
jsonString(const Passenger::StaticString &str) {
	return stringifyJson(Json::Value(Json::StaticString(str.data())));
}

/**
 * Encodes the given Unix timestamp (in microseconds) into a JSON object that
 * describes it.
 *
 *     timeToJson((time(NULL) - 10) * 1000000.0);
 *     // {
 *     //   "timestamp": 1424887842,
 *     //   "local": "Wed Feb 25 19:10:34 CET 2015",
 *     //   "relative": "10s ago"
 *     // }
 */
inline Json::Value
timeToJson(unsigned long long timestamp, unsigned long long now = 0) {
	if (timestamp == 0) {
		return Json::Value(Json::nullValue);
	}

	Json::Value doc;
	time_t wallClockTime = (time_t) (timestamp / 1000000ull);
	char wallClockTimeStr[32];
	size_t len;

	if (now == 0) {
		now = SystemTime::getUsec();
	}

	ctime_r(&wallClockTime, wallClockTimeStr);
	len = strlen(wallClockTimeStr);
	if (len > 0) {
		// Get rid of trailing newline
		wallClockTimeStr[len - 1] = '\0';
	}

	doc["timestamp"] = timestamp / 1000000.0;
	doc["local"] = wallClockTimeStr;
	if (timestamp > now) {
		doc["relative_timestamp"] = (timestamp - now) / 1000000.0;
		doc["relative"] = distanceOfTimeInWords(wallClockTime, now / 1000000ull) + " from now";
	} else {
		doc["relative_timestamp"] = (now - timestamp) / -1000000.0;
		doc["relative"] = distanceOfTimeInWords(wallClockTime, now / 1000000ull) + " ago";
	}

	return doc;
}

/**
 * Encodes the given monotonic timestamp into a JSON object that
 * describes it.
 *
 *     MonotonicTimeUsec t = SystemTime::getMonotonicUsec();
 *     monoTimeToJson(t - 10000000, t);
 *     // {
 *     //   "timestamp": 1424887842,
 *     //   "local": "Wed Feb 25 19:10:34 CET 2015",
 *     //   "relative_timestamp": -10,
 *     //   "relative": "10s ago"
 *     // }
 */
inline Json::Value
monoTimeToJson(MonotonicTimeUsec t, MonotonicTimeUsec monoNow, unsigned long long now = 0) {
	if (t == 0) {
		return Json::Value(Json::nullValue);
	}

	if (now == 0) {
		now = SystemTime::getUsec();
	}

	unsigned long long wallClockTimeUsec;
	if (monoNow > t) {
		wallClockTimeUsec = now - (monoNow - t);
	} else {
		wallClockTimeUsec = now + (monoNow - t);
	}

	time_t wallClockTime = (time_t) (wallClockTimeUsec / 1000000ull);
	char timeStr[32], *ctimeResult;
	size_t len;
	ctimeResult = ctime_r(&wallClockTime, timeStr);
	if (ctimeResult != NULL) {
		len = strlen(timeStr);
		if (len > 0) {
			// Get rid of trailing newline
			timeStr[len - 1] = '\0';
		}
	}

	Json::Value doc;
	doc["timestamp"] = wallClockTimeUsec / 1000000.0;
	if (ctimeResult != NULL) {
		doc["local"] = timeStr;
	}
	if (t > monoNow) {
		doc["relative_timestamp"] = (t - monoNow) / 1000000.0;
		doc["relative"] = distanceOfTimeInWords(t / 1000000ull, monoNow / 1000000ull) + " from now";
	} else {
		doc["relative_timestamp"] = (monoNow - t) / -1000000.0;
		doc["relative"] = distanceOfTimeInWords(t / 1000000ull, monoNow / 1000000ull) + " ago";
	}
	return doc;
}

inline Json::Value
durationToJson(unsigned long long duration) {
	Json::Value doc;
	char buf[64];

	doc["microseconds"] = (Json::UInt64) duration;
	if (duration >= 10 * 1000000) {
		snprintf(buf, sizeof(buf), "%.1fs", duration / 1000000.0);
	} else {
		snprintf(buf, sizeof(buf), "%.1fms", duration / 1000.0);
	}
	doc["human_readable"] = buf;

	return doc;
}

inline string
formatFloat(double val) {
	char buf[64];
	int size = snprintf(buf, sizeof(buf), "%.1f", val);
	return string(buf, size);
}

inline double
capFloatPrecision(double val) {
	char buf[64];
	snprintf(buf, sizeof(buf), "%.2f", val);
	return atof(buf);
}

inline Json::Value
speedToJson(double speed, const string &per, double nullValue = -1) {
	Json::Value doc;
	if (speed == nullValue) {
		doc["value"] = Json::Value(Json::nullValue);
	} else {
		doc["value"] = speed;
	}
	doc["per"] = per;
	return doc;
}

inline Json::Value
averageSpeedToJson(double speed, const string &per, const string &averagedOver, double nullValue = -1) {
	Json::Value doc;
	if (speed == nullValue) {
		doc["value"] = Json::Value(Json::nullValue);
	} else {
		doc["value"] = speed;
	}
	doc["per"] = per;
	doc["averaged_over"] = averagedOver;
	return doc;
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

inline Json::Value
byteSpeedToJson(double speed, const string &per) {
	Json::Value doc;
	if (speed >= 0) {
		doc = byteSizeToJson(speed);
	} else {
		doc = signedByteSizeToJson(speed);
	}
	doc["per"] = per;
	return doc;
}

inline Json::Value
byteSpeedToJson(double speed, double nullValue, const string &per) {
	Json::Value doc;
	if (speed == nullValue) {
		doc["bytes"] = Json::Value(Json::nullValue);
	} else if (speed >= 0) {
		doc = byteSizeToJson(speed);
	} else {
		doc = signedByteSizeToJson(speed);
	}
	doc["per"] = per;
	return doc;
}

inline Json::Value
byteSizeAndCountToJson(size_t size, unsigned int count) {
	Json::Value doc = byteSizeToJson(size);
	doc["count"] = count;
	return doc;
}


} // namespace Passenger

#endif /* _PASSENGER_JSON_TOOLS_JSON_UTILS_H_ */
