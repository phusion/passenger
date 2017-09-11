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
#ifndef _PASSENGER_CONFIG_KIT_SCHEMA_UTILS_H_
#define _PASSENGER_CONFIG_KIT_SCHEMA_UTILS_H_

#include <ConfigKit/Store.h>
#include <string>
#include <vector>

namespace Passenger {
namespace ConfigKit {

using namespace std;


inline Json::Value
getDefaultStandaloneEngine(const Store &store) {
	if (store["integration_mode"].asString() == "standalone") {
		return "builtin";
	} else {
		return Json::nullValue;
	}
}

inline void
validateIntegrationMode(const Store &config, vector<Error> &errors) {
	if (config["integration_mode"].isNull()) {
		return;
	}
	string integrationMode = config["integration_mode"].asString();
	if (integrationMode != "apache" && integrationMode != "nginx" && integrationMode != "standalone") {
		errors.push_back(Error("'{{integration_mode}}' may only be one of 'apache', 'nginx', 'standalone'"));
	}
}

inline void
validateStandaloneEngine(const Store &config, vector<Error> &errors) {
	if (config["integration_mode"].asString() != "standalone") {
		return;
	}
	string standaloneEngine = config["standalone_engine"].asString();
	if (standaloneEngine.empty()) {
		errors.push_back(Error("'{{standalone_engine}}' is required when '{{integration_mode}}' is 'standalone'"));
		return;
	}
	if (standaloneEngine != "nginx" && standaloneEngine != "builtin") {
		errors.push_back(Error("'{{standalone_engine}}' is must be either 'nginx' or 'builtin'"));
	}
}


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_SCHEMA_UTILS_H_ */
