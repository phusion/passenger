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
#ifndef _PASSENGER_CONFIG_KIT_UTILS_H_
#define _PASSENGER_CONFIG_KIT_UTILS_H_

#include <string>
#include <vector>

#include <jsoncpp/json.h>

#include <ConfigKit/Common.h>
#include <StaticString.h>
#include <Utils/FastStringStream.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;

class Error;


template<typename Component, typename Translator>
inline bool
previewConfigUpdateSubComponent(Component &component,
	const Json::Value &updates, const Translator &translator,
	vector<Error> &errors)
{
	vector<Error> tempErrors;

	component.previewConfigUpdate(translator.translate(updates),
		tempErrors);
	tempErrors = translator.reverseTranslate(tempErrors);
	errors.insert(errors.end(), tempErrors.begin(), tempErrors.end());
	return errors.empty();
}

template<typename Component, typename Translator>
inline void
configureSubComponent(Component &component,
	const Json::Value &updates, const Translator &translator,
	vector<ConfigKit::Error> &errors)
{
	vector<ConfigKit::Error> tempErrors;

	component.configure(translator.translate(updates), tempErrors);
	tempErrors = translator.reverseTranslate(tempErrors);
	errors.insert(errors.end(), tempErrors.begin(), tempErrors.end());
}

template<typename Component>
inline void
callPreviewConfigUpdateAndCallback(Component *component, Json::Value updates,
	ConfigKit::ConfigCallback callback)
{
	vector<ConfigKit::Error> errors;
	Json::Value config = component->previewConfigUpdate(updates, errors);
	callback(config, errors);
}

template<typename Component>
inline void
callConfigureAndCallback(Component *component, Json::Value updates,
	ConfigKit::ConfigCallback callback)
{
	vector<ConfigKit::Error> errors;
	if (component->configure(updates, errors)) {
		callback(component->inspectConfig(), errors);
	} else {
		callback(Json::nullValue, errors);
	}
}

template<typename Component>
inline void
callInspectConfigAndCallback(Component *component,
	ConfigKit::InspectCallback callback)
{
	callback(component->inspectConfig());
}

inline StaticString
getTypeString(Type type) {
	switch (type) {
	case STRING_TYPE:
		return P_STATIC_STRING("string");
	case PASSWORD_TYPE:
		return P_STATIC_STRING("password");
	case INT_TYPE:
		return P_STATIC_STRING("integer");
	case UINT_TYPE:
		return P_STATIC_STRING("unsigned integer");
	case FLOAT_TYPE:
		return P_STATIC_STRING("float");
	case BOOL_TYPE:
		return P_STATIC_STRING("boolean");
	default:
		return P_STATIC_STRING("unknown");
	}
}

inline string
toString(const vector<Error> &errors) {
	FastStringStream<> stream;
	vector<Error>::const_iterator it, end = errors.end();

	for (it = errors.begin(); it != end; it++) {
		if (it != errors.begin()) {
			stream << "; ";
		}
		stream << it->getMessage();
	}
	return string(stream.data(), stream.size());
}


} // ConfigKit
} // Passenger

#endif /* _PASSENGER_CONFIG_KIT_UTILS_H_ */
