/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_WRAPPER_REGISTRY_WRAPPER_H_
#define _PASSENGER_WRAPPER_REGISTRY_WRAPPER_H_

#include <boost/container/small_vector.hpp>
#include <StaticString.h>

namespace Passenger {
namespace WrapperRegistry {

using namespace std;


struct Entry {
	/**
	 * A identifier-like name for this language. All-lowercase, no spaces.
	 * Related to passenger_app_type.
	 * Example: "ruby"
	 */
	StaticString language;

	/**
	 * A human-readable name for this language.
	 * Example: "Ruby"
	 */
	StaticString languageDisplayName;

	/**
	 * Path to the wrapper to use. If `suppliedByThirdParty` is false, then
	 * the path is considered relative to helperScriptsDir.
	 */
	StaticString path;

	/**
	 * The title that spawned processes for this language should assume.
	 * Example: "Passenger RubyApp"
	 */
	StaticString processTitle;

	/**
	 * A default command for the interpreter of this language.
	 * Will be looked up in $PATH.
	 * Example: "ruby"
	 */
	StaticString defaultInterpreter;

	/**
	 * A list of startup file names that we should look for
	 * in order to autodetect whether an app belongs to this
	 * language. Any of these files are also considered to be
	 * the entrypoint to the app.
	 * Example: "config.ru", "index.js", "app.js"
	 */
	boost::container::small_vector<StaticString, 2> defaultStartupFiles;

	bool suppliedByThirdParty;

	Entry()
		: suppliedByThirdParty(false)
		{ }

	// In C++98, `boost::container::small_vector` causes our default
	// assignment operator to become `operator=(Entry &)`, which is
	// incompatible with the Registry's usage of StringKeyTable.
	// We fix this by writing out own assignment operator.
	Entry &operator=(const Entry &other) {
		if (this != &other) {
			language = other.language;
			languageDisplayName = other.languageDisplayName;
			path = other.path;
			processTitle = other.processTitle;
			defaultInterpreter = other.defaultInterpreter;
			defaultStartupFiles = other.defaultStartupFiles;
			suppliedByThirdParty = other.suppliedByThirdParty;
		}
		return *this;
	}

	bool isNull() const {
		return language.empty();
	}
};


} // namespace WrapperRegistry
} // namespace Passenger

#endif /* _PASSENGER_WRAPPER_REGISTRY_WRAPPER_H_ */
