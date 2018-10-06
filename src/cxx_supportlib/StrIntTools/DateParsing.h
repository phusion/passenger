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
#ifndef _PASSENGER_STRINTTOOLS_DATE_PARSING_H_
#define _PASSENGER_STRINTTOOLS_DATE_PARSING_H_

#include <algorithm>
#include <time.h>
#include <cassert>
#include <cstring>
#include <StaticString.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {


inline bool skipImfFixdate_comment(const char **pos, const char *end);
inline bool parseImfFixdate_dayOfWeek(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_date(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_day(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_month(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_year(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_time(const char **pos, const char *end, struct tm &tm, int &zone);
inline bool parseImfFixdate_hour(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_minute(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_second(const char **pos, const char *end, struct tm &tm);
inline bool parseImfFixdate_zone(const char **pos, const char *end, int &zone);


/**
 * Parses an IMF-fixdate, as defined by RFC 7231 (while referencing RFC 5322).
 * Returns whether parsing succeeded.
 */
inline bool
parseImfFixdate(const char *date, const char *end, struct tm &tm, int &zone) {
	// We're not parsing the grammar exactly, but whatever.
	// It's too complicated and nobody uses CFWS.

	tm.tm_yday = -1;
	tm.tm_isdst = 0;

	if (!parseImfFixdate_dayOfWeek(&date, end, tm)) {
		return false;
	}
	if (date == end || *date != ',') {
		return false;
	}
	date++;
	if (!parseImfFixdate_date(&date, end, tm)) {
		return false;
	}
	return parseImfFixdate_time(&date, end, tm, zone);
}

/**
 * Converts a parsed IMF-fixdate, as outputted by `parseImfFixdate()`,
 * into a Unix timestamp.
 */
inline time_t
parsedDateToTimestamp(struct tm &tm, int zone) {
	time_t timeUsingLocalTZ = mktime(&tm);
	// tmUsingLocalTZ now contains tm interpreted as being in our local timezone instead of the intended UTC
	// Example: 10 UTC interpreted as 10 GMT+1 instead of 11 GMT+1

#if !defined(sun) && !defined(__sun) && !defined(BOOST_OS_MACOS)
	// tm_gmtoff = "seconds east of UTC", so the example 10 GMT+1 would now be corrected to (10+1) GMT+1
	timeUsingLocalTZ += tm.tm_gmtoff;
#else
	// Solaris doesn't have tm_gmtoff, so we calculate the current offset by converting to UTC (gmtime) and pretending
	// that is local time again (mktime), resulting in a "corrected" timestamp that we can delta with the original timestamp.
	// In the example: gmtime(10 GMT+1) = 9 UTC, interpreted as 9 GMT+1 gives use delta (10 - 9) = +1 to correct 10 GMT+1 with.
	struct tm *tmAsUTC = gmtime(&timeUsingLocalTZ);
	time_t utcUsingLocalTZ = mktime(tmAsUTC);
	timeUsingLocalTZ += (timeUsingLocalTZ - utcUsingLocalTZ);
#endif

	// The final result also needs to take into account the desired zone.
	return timeUsingLocalTZ - (zone / 100 * 60 * 60 + zone % 100 * 60);
}

inline void
skipImfFixdate_FWS(const char **pos, const char *end) {
	while (*pos < end && (**pos == '\r' || **pos == '\n' || **pos == ' ' || **pos == '\t')) {
		(*pos)++;
	}
}

inline bool
skipImfFixdate_CFWS(const char **pos, const char *end) {
	while (*pos < end) {
		skipImfFixdate_FWS(pos, end);
		if (*pos == end || **pos != '(') {
			break;
		} else if (!skipImfFixdate_comment(pos, end)) {
			return false;
		}
	}
	return true;
}

inline bool
skipImfFixdate_comment(const char **pos, const char *end) {
	assert(**pos == '(');
	(*pos)++;

	char *result = (char *) std::memchr(*pos, ')', end - *pos);
	if (result == NULL) {
		return false;
	} else {
		*pos = result + 1;
		return true;
	}
}

inline bool
parseImfFixdate_dayOfWeek(const char **pos, const char *end, struct tm &tm) {
	if (!skipImfFixdate_CFWS(pos, end)) {
		return false;
	}
	if (end - *pos >= 3) {
		StaticString str(*pos, 3);
		(*pos) += 3;
		if (str == P_STATIC_STRING("Mon")) {
			tm.tm_wday = 0;
		} else if (str == P_STATIC_STRING("Tue")) {
			tm.tm_wday = 1;
		} else if (str == P_STATIC_STRING("Wed")) {
			tm.tm_wday = 2;
		} else if (str == P_STATIC_STRING("Thu")) {
			tm.tm_wday = 3;
		} else if (str == P_STATIC_STRING("Fri")) {
			tm.tm_wday = 4;
		} else if (str == P_STATIC_STRING("Sat")) {
			tm.tm_wday = 5;
		} else if (str == P_STATIC_STRING("Sun")) {
			tm.tm_wday = 6;
		} else {
			return false;
		}
		return true;
	} else {
		return false;
	}
}

inline bool
parseImfFixdate_date(const char **pos, const char *end, struct tm &tm) {
	return parseImfFixdate_day(pos, end, tm)
		&& parseImfFixdate_month(pos, end, tm)
		&& parseImfFixdate_year(pos, end, tm);
}

inline bool
parseImfFixdate_day(const char **pos, const char *end, struct tm &tm) {
	if (!skipImfFixdate_CFWS(pos, end)) {
		return false;
	}

	const char *dayBegin = *pos;
	while (*pos < end && **pos >= '0' && **pos <= '9') {
		(*pos)++;
	}

	size_t size = *pos - dayBegin;
	if (size == 0 || size > 2) {
		return false;
	}

	if (!skipImfFixdate_CFWS(pos, end)) {
		return false;
	}

	tm.tm_mday = stringToInt(StaticString(dayBegin, size));
	return true;
}

inline bool
parseImfFixdate_month(const char **pos, const char *end, struct tm &tm) {
	if (end - *pos >= 3) {
		StaticString str(*pos, 3);
		(*pos) += 3;
		if (str == P_STATIC_STRING("Jan")) {
			tm.tm_mon = 0;
		} else if (str == P_STATIC_STRING("Feb")) {
			tm.tm_mon = 1;
		} else if (str == P_STATIC_STRING("Mar")) {
			tm.tm_mon = 2;
		} else if (str == P_STATIC_STRING("Apr")) {
			tm.tm_mon = 3;
		} else if (str == P_STATIC_STRING("May")) {
			tm.tm_mon = 4;
		} else if (str == P_STATIC_STRING("Jun")) {
			tm.tm_mon = 5;
		} else if (str == P_STATIC_STRING("Jul")) {
			tm.tm_mon = 6;
		} else if (str == P_STATIC_STRING("Aug")) {
			tm.tm_mon = 7;
		} else if (str == P_STATIC_STRING("Sep")) {
			tm.tm_mon = 8;
		} else if (str == P_STATIC_STRING("Oct")) {
			tm.tm_mon = 9;
		} else if (str == P_STATIC_STRING("Nov")) {
			tm.tm_mon = 10;
		} else if (str == P_STATIC_STRING("Dec")) {
			tm.tm_mon = 11;
		} else {
			return false;
		}
		return true;
	} else {
		return false;
	}
}

inline bool
parseImfFixdate_year(const char **pos, const char *end, struct tm &tm) {
	if (!skipImfFixdate_CFWS(pos, end)) {
		return false;
	}

	const char *yearBegin = *pos;
	while (*pos < end && **pos >= '0' && **pos <= '9') {
		(*pos)++;
	}

	size_t size = *pos - yearBegin;
	if (size != 4) {
		return false;
	}

	if (!skipImfFixdate_CFWS(pos, end)) {
		return false;
	}

	tm.tm_year = stringToInt(StaticString(yearBegin, size)) - 1900;
	return true;
}

inline bool
parseImfFixdate_time(const char **pos, const char *end, struct tm &tm, int &zone) {
	if (!parseImfFixdate_hour(pos, end, tm)) {
		return false;
	}

	if (*pos == end || **pos != ':') {
		return false;
	}
	(*pos)++;
	if (!parseImfFixdate_minute(pos, end, tm)) {
		return false;
	}

	if (*pos == end) {
		return false;
	} else if (**pos == ':') {
		(*pos)++;
		if (!parseImfFixdate_second(pos, end, tm)) {
			return false;
		}
	}

	return parseImfFixdate_zone(pos, end, zone);
}

inline int
parseImfFixdate_timeComponent(const char **pos, const char *end) {
	if (!skipImfFixdate_CFWS(pos, end)) {
		return -1;
	}

	const char *begin = *pos;
	while (*pos < end && **pos >= '0' && **pos <= '9') {
		(*pos)++;
	}

	size_t size = *pos - begin;
	if (size != 2) {
		return -1;
	}

	if (!skipImfFixdate_CFWS(pos, end)) {
		return -1;
	}

	return stringToInt(StaticString(begin, size));
}

inline bool
parseImfFixdate_hour(const char **pos, const char *end, struct tm &tm) {
	int result = parseImfFixdate_timeComponent(pos, end);
	if (result != -1 && result >= 0 && result <= 23) {
		tm.tm_hour = result;
		return true;
	} else {
		return false;
	}
}

inline bool
parseImfFixdate_minute(const char **pos, const char *end, struct tm &tm) {
	int result = parseImfFixdate_timeComponent(pos, end);
	if (result != -1 && result >= 0 && result <= 59) {
		tm.tm_min = result;
		return true;
	} else {
		return false;
	}
}

inline bool
parseImfFixdate_second(const char **pos, const char *end, struct tm &tm) {
	int result = parseImfFixdate_timeComponent(pos, end);
	// up to 60 to allow leap seconds
	if (result != -1 && result >= 0 && result <= 60) {
		tm.tm_sec = result;
		return true;
	} else {
		return false;
	}
}

inline bool
parseImfFixdate_zone(const char **pos, const char *end, int &zone) {
	if (!skipImfFixdate_CFWS(pos, end)
	 || *pos == end)
	{
		return false;
	}

	if (**pos == '+' || **pos == '-') {
		// Time zone offset
		int sign = (**pos == '+') ? 1 : -1;

		(*pos)++;
		if (*pos == end) {
			return false;
		}

		const char *begin = *pos;
		while (*pos < end && **pos >= '0' && **pos <= '9') {
			(*pos)++;
		}

		size_t size = *pos - begin;
		if (size != 4) {
			return false;
		}

		zone = sign * stringToInt(StaticString(begin, size));
	} else {
		// obs-zone
		StaticString str(*pos, std::min<size_t>(end - *pos, 3));
		if (str.substr(0, 2) == P_STATIC_STRING("UT")
		 || str == P_STATIC_STRING("UTC")
		 || str == P_STATIC_STRING("GMT"))
		{
			zone = 0;
		} else if (str == P_STATIC_STRING("EST")) {
			zone = -500;
		} else if (str == P_STATIC_STRING("EDT")) {
			zone = -400;
		} else if (str == P_STATIC_STRING("CST")) {
			zone = -600;
		} else if (str == P_STATIC_STRING("CDT")) {
			zone = -500;
		} else if (str == P_STATIC_STRING("MST")) {
			zone = -700;
		} else if (str == P_STATIC_STRING("MDT")) {
			zone = -600;
		} else if (str == P_STATIC_STRING("PST")) {
			zone = -800;
		} else if (str == P_STATIC_STRING("PDT")) {
			zone = -700;
		} else {
			// No support for military zones
			return false;
		}
	}
	return true;
}


} // namespace Passenger

#endif /* _PASSENGER_STRINTTOOLS_DATE_PARSING_H_ */
