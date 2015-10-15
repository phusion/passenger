/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion Holding B.V.
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
#include <UnionStationFilterSupport.h>
#include <cstring>
#include <cstdlib>

using namespace Passenger;

extern "C" {

PassengerFilter *
passenger_filter_create(const char *source, int size, char **error) {
	if (size == -1) {
		size = strlen(source);
	}
	try {
		return (PassengerFilter *) new FilterSupport::Filter(StaticString(source, size));
	} catch (const SyntaxError &e) {
		if (error != NULL) {
			*error = strdup(e.what());
		}
		return NULL;
	}
}

void
passenger_filter_free(PassengerFilter *filter) {
	delete (FilterSupport::Filter *) filter;
}

char *
passenger_filter_validate(const char *source, int size) {
	if (size == -1) {
		size = strlen(source);
	}
	try {
		(void) FilterSupport::Filter(StaticString(source, size));
		return NULL;
	} catch (const SyntaxError &e) {
		return strdup(e.what());
	}
}

} // extern "C"
