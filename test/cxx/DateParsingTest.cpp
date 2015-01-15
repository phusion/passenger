#include "TestSupport.h"
#include <cstring>
#include <Utils/DateParsing.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct DateParsingTest {
		struct tm tm;
		int zone;

		DateParsingTest() {
			memset(&tm, 0, sizeof(tm));
			zone = 0;
		}

		void parse(const char *datestr) {
			const char *end = datestr + strlen(datestr);
			ensure("Parsing succeeds", parseImfFixdate(datestr, end, tm, zone));
		}
	};

	DEFINE_TEST_GROUP(DateParsingTest);

	/***** Valid dates *****/

	TEST_METHOD(1) {
		parse("Thu, 08 Jan 2015 16:20:46 GMT");
		ensure_equals(tm.tm_year, 2015 - 1900);
		ensure_equals(tm.tm_mon, 0);
		ensure_equals(tm.tm_mday, 8);
		ensure_equals(tm.tm_hour, 16);
		ensure_equals(tm.tm_min, 20);
		ensure_equals(tm.tm_sec, 46);
		ensure_equals(zone, 0);
		ensure_equals(parsedDateToTimestamp(tm, zone), 1420734046);
	}

	TEST_METHOD(2) {
		parse("Thu, 01 Dec 1994 16:00:00 GMT");
		ensure_equals(tm.tm_year, 1994 - 1900);
		ensure_equals(tm.tm_mon, 11);
		ensure_equals(tm.tm_mday, 1);
		ensure_equals(tm.tm_hour, 16);
		ensure_equals(tm.tm_min, 0);
		ensure_equals(tm.tm_sec, 0);
		ensure_equals(zone, 0);
		ensure_equals(parsedDateToTimestamp(tm, zone), 786297600);
	}

	TEST_METHOD(3) {
		parse("Sun, 06 Nov 1994 08:49:37 GMT");
		ensure_equals(tm.tm_year, 1994 - 1900);
		ensure_equals(tm.tm_mon, 10);
		ensure_equals(tm.tm_mday, 6);
		ensure_equals(tm.tm_hour, 8);
		ensure_equals(tm.tm_min, 49);
		ensure_equals(tm.tm_sec, 37);
		ensure_equals(zone, 0);
		ensure_equals(parsedDateToTimestamp(tm, zone), 784111777);
	}

	TEST_METHOD(4) {
		parse("Thu, 08 Jan 2015 17:33:14 -0300");
		ensure_equals(tm.tm_year, 2015 - 1900);
		ensure_equals(tm.tm_mon, 0);
		ensure_equals(tm.tm_mday, 8);
		ensure_equals(tm.tm_hour, 17);
		ensure_equals(tm.tm_min, 33);
		ensure_equals(tm.tm_sec, 14);
		ensure_equals(zone, -300);
		ensure_equals(parsedDateToTimestamp(tm, zone), 1420749194);
	}
}
