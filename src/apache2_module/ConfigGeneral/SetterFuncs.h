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
#ifndef _PASSENGER_APACHE2_MODULE_CONFIG_GENERAL_SETTER_FUNCS_H_
#define _PASSENGER_APACHE2_MODULE_CONFIG_GENERAL_SETTER_FUNCS_H_

#include <limits>
#include <cstdlib>
#include <http_config.h>

namespace Passenger {
namespace Apache2Module {

using namespace std;


inline const char *
setIntConfig(cmd_parms *cmd, const char *rawValue, int &parsedValue,
	int minValue = std::numeric_limits<int>::min())
{
	char *end;
	long result;

	result = strtol(rawValue, &end, 10);
	if (*end != '\0') {
		return apr_psprintf(cmd->temp_pool, "Invalid number specified for %s.",
			cmd->directive->directive);
	}

	if (minValue != std::numeric_limits<int>::min() && result < (long) minValue) {
		return apr_psprintf(cmd->temp_pool, "%s must be at least %d.",
			cmd->directive->directive, minValue);
	}

	parsedValue = (int) result;
	return NULL;
}


} // namespace Apache2Module
} // namespace Passenger

#endif /* _PASSENGER_APACHE2_MODULE_CONFIG_GENERAL_SETTER_FUNCS_H_ */
