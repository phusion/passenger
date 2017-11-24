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
#ifndef _PASSENGER_CONFIG_KIT_TABLE_TRANSLATOR_H_
#define _PASSENGER_CONFIG_KIT_TABLE_TRANSLATOR_H_

#include <ConfigKit/Translator.h>
#include <DataStructures/StringKeyTable.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


/**
 * A translator that translates keys according to a table of rules. Anything
 * not in the table is simply left as-is.
 *
 *     TableTranslator translator;
 *     translator.add("foo", "bar");
 *     translator.finalize();
 *
 *     translator.translateOne("foo");         // => "bar"
 *     translator.reverseTranslateOne("bar");  // => "foo"
 *
 *     translator.translateOne("baz");         // => "baz"
 *     translator.reverseTranslateOne("baz");  // => "baz"
 *
 * You can learn more about translators in the ConfigKit README, section
 * "The special problem of overlapping configuration names and translation".
 */
class TableTranslator: public Translator {
private:
	StringKeyTable<string> table, reverseTable;
	bool finalized;

	static string internalTranslateOne(const StringKeyTable<string> &table,
		const StaticString &key)
	{
		const string *entry;

		if (table.lookup(key, &entry)) {
			return *entry;
		} else {
			return key;
		}
	}

public:
	TableTranslator()
		: finalized(false)
		{ }

	void add(const StaticString &mainSchemaKeyName, const StaticString &subSchemaKeyName) {
		assert(!finalized);
		table.insert(mainSchemaKeyName, subSchemaKeyName);
		reverseTable.insert(subSchemaKeyName, mainSchemaKeyName);
	}

	void finalize() {
		assert(!finalized);
		table.compact();
		reverseTable.compact();
		finalized = true;
	}

	bool isFinalized() const {
		return finalized;
	}

	virtual Json::Value translate(const Json::Value &doc) const {
		assert(finalized);
		return Translator::translate(doc);
	}

	virtual Json::Value reverseTranslate(const Json::Value &doc) const {
		assert(finalized);
		return Translator::reverseTranslate(doc);
	}

	virtual vector<Error> translate(const vector<Error> &errors) const {
		assert(finalized);
		return Translator::translate(errors);
	}

	virtual vector<Error> reverseTranslate(const vector<Error> &errors) const {
		assert(finalized);
		return Translator::reverseTranslate(errors);
	}

	virtual string translateOne(const StaticString &key) const {
		assert(finalized);
		return internalTranslateOne(table, key);
	}

	virtual string reverseTranslateOne(const StaticString &key) const {
		return internalTranslateOne(reverseTable, key);
	}
};


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_TABLE_TRANSLATOR_H_ */
