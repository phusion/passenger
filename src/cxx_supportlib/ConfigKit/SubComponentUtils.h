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
#ifndef _PASSENGER_CONFIG_KIT_SUB_COMPONENT_UTILS_H_
#define _PASSENGER_CONFIG_KIT_SUB_COMPONENT_UTILS_H_

#include <string>
#include <vector>
#include <stdexcept>

#include <jsoncpp/json.h>
#include <ConfigKit/Common.h>
#include <ConfigKit/Translator.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


template<typename Component>
inline void
prepareConfigChangeForSubComponent(Component &component, const Translator &translator,
	const Json::Value &updates, vector<ConfigKit::Error> &errors,
	typename Component::ConfigChangeRequest &req)
{
	vector<Error> tempErrors;
	component.prepareConfigChange(translator.translate(updates),
		tempErrors, req);
	tempErrors = translator.reverseTranslate(tempErrors);
	errors.insert(errors.end(), tempErrors.begin(), tempErrors.end());
}


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_SUB_COMPONENT_UTILS_H_ */
