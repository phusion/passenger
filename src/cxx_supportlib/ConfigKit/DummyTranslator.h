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
#ifndef _PASSENGER_CONFIG_KIT_DUMMY_TRANSLATOR_H_
#define _PASSENGER_CONFIG_KIT_DUMMY_TRANSLATOR_H_

#include <vector>
#include <ConfigKit/Translator.h>
#include <StaticString.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


/**
 * A translator that does nothing.
 *
 * You can learn more about translators in the ConfigKit README, section
 * "The special problem of overlapping configuration names and translation".
 */
class DummyTranslator: public Translator {
public:
	virtual Json::Value translate(const Json::Value &doc) const {
		return doc;
	}

	virtual Json::Value reverseTranslate(const Json::Value &doc) const {
		return doc;
	}

	virtual vector<Error> translate(const vector<Error> &errors) const {
		return errors;
	}

	virtual vector<Error> reverseTranslate(const vector<Error> &errors) const {
		return errors;
	}

	virtual string translateOne(const StaticString &key) const {
		return key;
	}

	virtual string reverseTranslateOne(const StaticString &key) const {
		return key;
	}
};


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_DUMMY_TRANSLATOR_H_ */
