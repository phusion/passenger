/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_OPTION_PARSING_H_
#define _PASSENGER_OPTION_PARSING_H_

#include <cstdio>
#include <cstring>

namespace Passenger {

using namespace std;


class OptionParser {
public:
	typedef void (*UsageFunction)();

private:
	UsageFunction usage;

public:
	OptionParser(UsageFunction _usage)
		: usage(_usage)
		{ }

	bool
	isFlag(const char *arg, char shortFlagName, const char *longFlagName) {
		return strcmp(arg, longFlagName) == 0
			|| (shortFlagName != '\0' && arg[0] == '-'
				&& arg[1] == shortFlagName && arg[2] == '\0');
	}

	inline bool
	isValueFlag(int argc, int i, const char *arg, char shortFlagName, const char *longFlagName) {
		if (isFlag(arg, shortFlagName, longFlagName)) {
			if (argc >= i + 2) {
				return true;
			} else {
				fprintf(stderr, "ERROR: extra argument required for %s\n", arg);
				usage();
				exit(1);
				return false; // Never reached
			}
		} else {
			return false;
		}
	}
};


} // namespace Passenger

#endif /* _PASSENGER_OPTION_PARSING_H_ */
