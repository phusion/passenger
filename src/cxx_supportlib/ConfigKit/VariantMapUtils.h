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
#ifndef _PASSENGER_CONFIG_KIT_VARIANT_MAP_UTILS_H_
#define _PASSENGER_CONFIG_KIT_VARIANT_MAP_UTILS_H_

#include <LoggingKit/LoggingKit.h>
#include <Exceptions.h>
#include <ConfigKit/Schema.h>
#include <Utils/StrIntUtils.h>
#include <Utils/VariantMap.h>

namespace Passenger {
namespace ConfigKit {


inline Json::Value
variantMapToJson(const Schema &schema, const VariantMap &options) {
	Json::Value doc;
	Schema::ConstIterator it(schema.getIterator());

	while (*it != NULL) {
		const StaticString &key = it.getKey();
		const Schema::Entry &entry = it.getValue();

		if (options.has(key)) {
			switch (entry.type) {
			case STRING_TYPE:
			case ANY_TYPE:
				doc[key.toString()] = options.get(key);
				break;
			case INT_TYPE:
				doc[key.toString()] = options.getInt(key);
				break;
			case UINT_TYPE:
				doc[key.toString()] = options.getUint(key);
				break;
			case FLOAT_TYPE:
				throw RuntimeException("variantMapToJson(): unsupported type FLOAT_TYPE");
				break;
			case BOOL_TYPE:
				doc[key.toString()] = options.getBool(key);
				break;
			case ARRAY_TYPE:
			case STRING_ARRAY_TYPE: {
				Json::Value subdoc(Json::arrayValue);
				vector<string> set = options.getStrSet(key);
				vector<string>::const_iterator it, end = set.end();
				for (it = set.begin(); it != end; it++) {
					subdoc.append(*it);
				}
				doc[key.toString()] = subdoc;
				break;
			}
			case OBJECT_TYPE:
				// Not supported
				break;
			default:
				P_BUG("Unknown type " + Passenger::toString((int) entry.type));
				break;
			}
		}

		it.next();
	}

	return doc;
}


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_VARIANT_MAP_UTILS_H_ */
