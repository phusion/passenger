/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_LIBEV_JSON_UTILS_H_
#define _PASSENGER_LIBEV_JSON_UTILS_H_

#include <ev.h>
#include <jsoncpp/json.h>
#include <cstring>
#include <ctime>
#include <StrIntTools/StrIntUtils.h>
#include <SystemTools/SystemTime.h>

namespace Passenger {

using namespace std;


/**
 * Encodes the given libev timestamp into a JSON object that
 * describes it.
 *
 *     evTimeToJson(ev_now(loop) - 10);
 *     // {
 *     //   "timestamp": 1424887842,
 *     //   "local": "Wed Feb 25 19:10:34 CET 2015",
 *     //   "relative_timestamp": -10,
 *     //   "relative": "10s ago"
 *     // }
 */
inline Json::Value
evTimeToJson(ev_tstamp evTime, ev_tstamp evNow, unsigned long long now = 0) {
	if (evTime <= 0) {
		return Json::Value(Json::nullValue);
	}

	if (now == 0) {
		now = SystemTime::getUsec();
	}

	Json::Value doc;
	unsigned long long wallClockTimeUsec = now
		+ (evTime - evNow) * 1000000ull;
	time_t wallClockTime = (time_t) (wallClockTimeUsec / 1000000ull);
	char buf[32];
	size_t len;

	ctime_r(&wallClockTime, buf);
	len = strlen(buf);
	if (len > 0) {
		// Get rid of trailing newline
		buf[len - 1] = '\0';
	}

	doc["timestamp"] = wallClockTimeUsec / 1000000.0;
	doc["local"] = buf;

	doc["relative_timestamp"] = evTime - evNow;
	if (evTime > evNow) {
		doc["relative"] = distanceOfTimeInWords(evTime, evNow) + " from now";
	} else {
		doc["relative"] = distanceOfTimeInWords(evTime, evNow) + " ago";
	}
	return doc;
}


} // namespace Passenger

#endif /* _PASSENGER_LIBEV_JSON_UTILS_H_ */
