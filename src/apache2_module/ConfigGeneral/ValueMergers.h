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
#ifndef _PASSENGER_APACHE2_MODULE_CONFIG_GENERAL_VALUE_MERGERS_H_
#define _PASSENGER_APACHE2_MODULE_CONFIG_GENERAL_VALUE_MERGERS_H_

#include <set>
#include <string>
#include "../Config.h"

namespace Passenger {
namespace Apache2Module {

using namespace std;


inline StaticString
mergeStrValue(const StaticString &current, const StaticString &prev,
	const StaticString &defaultValue = StaticString())
{
	if (current.empty()) {
		if (prev.empty()) {
			return defaultValue;
		} else {
			return prev;
		}
	} else {
		return current;
	}
}

inline int
mergeIntValue(int current, int prev, int defaultValue = UNSET_INT_VALUE) {
	if (current == UNSET_INT_VALUE) {
		if (prev == UNSET_INT_VALUE) {
			return defaultValue;
		} else {
			return prev;
		}
	} else {
		return current;
	}
}

inline Threeway
mergeBoolValue(Threeway current, Threeway prev) {
	if (current == UNSET) {
		return prev;
	} else {
		return current;
	}
}

inline Threeway
mergeBoolValue(Threeway current, Threeway prev, bool defaultValue) {
	if (current == UNSET) {
		if (prev == UNSET) {
			if (defaultValue) {
				return ENABLED;
			} else {
				return DISABLED;
			}
		} else {
			return prev;
		}
	} else {
		return current;
	}
}

inline set<string>
mergeStrSetValue(const set<string> current, const set<string> prev,
	const set<string> defaultValue = set<string>())
{
	set<string> result = prev;
	result.insert(current.begin(), current.end());
	return result;
}


} // namespace Apache2Module
} // namespace Passenger

#endif /* _PASSENGER_APACHE2_MODULE_CONFIG_GENERAL_VALUE_MERGERS_H_ */
