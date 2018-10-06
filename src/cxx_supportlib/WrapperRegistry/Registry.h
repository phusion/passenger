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
#ifndef _PASSENGER_WRAPPER_REGISTRY_REGISTRY_H_
#define _PASSENGER_WRAPPER_REGISTRY_REGISTRY_H_

#include <cassert>
#include <cstddef>

#include <boost/foreach.hpp>
#include <boost/shared_array.hpp>
#include <oxt/macros.hpp>

#include <WrapperRegistry/Entry.h>
#include <DataStructures/StringKeyTable.h>
#include <Constants.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace WrapperRegistry {

using namespace std;


class Registry {
public:
	typedef StringKeyTable<Entry>::ConstIterator ConstIterator;

private:
	StringKeyTable<Entry> entries;
	StringKeyTable<HashedStaticString> aliases;
	boost::shared_array<char> storage;
	const Entry nullEntry;
	bool finalized;

	void internStrings() {
		size_t totalSize = 0;
		size_t tmpSize;
		char *newStorage, *pos, *end;

		// Calculate required storage size
		{
			StringKeyTable<Entry>::ConstIterator it(entries);
			while (*it != NULL) {
				const Entry &entry = it.getValue();

				totalSize += entry.language.size() + 1;
				totalSize += entry.languageDisplayName.size() + 1;
				totalSize += entry.path.size() + 1;
				totalSize += entry.processTitle.size() + 1;
				totalSize += entry.defaultInterpreter.size() + 1;
				foreach (const StaticString &defaultStartupFile,
					entry.defaultStartupFiles)
				{
					totalSize += defaultStartupFile.size() + 1;
				}

				it.next();
			}
		}
		{
			StringKeyTable<HashedStaticString>::ConstIterator it(aliases);
			while (*it != NULL) {
				const HashedStaticString &name = it.getValue();
				totalSize += name.size() + 1;
				it.next();
			}
		}

		// Allocate new storage
		newStorage = pos = new char[totalSize];
		end = newStorage + totalSize;

		// Fill new storage
		{
			StringKeyTable<Entry>::ConstIterator it(entries);
			while (*it != NULL) {
				const Entry &entry = it.getValue();

				pos = appendData(pos, end, entry.language);
				pos = appendData(pos, end, "\0", 1);

				pos = appendData(pos, end, entry.languageDisplayName);
				pos = appendData(pos, end, "\0", 1);

				pos = appendData(pos, end, entry.path);
				pos = appendData(pos, end, "\0", 1);

				pos = appendData(pos, end, entry.processTitle);
				pos = appendData(pos, end, "\0", 1);

				pos = appendData(pos, end, entry.defaultInterpreter);
				pos = appendData(pos, end, "\0", 1);

				foreach (const StaticString &defaultStartupFile,
					entry.defaultStartupFiles)
				{
					pos = appendData(pos, end, defaultStartupFile);
					pos = appendData(pos, end, "\0", 1);
				}

				it.next();
			}
		}
		{
			StringKeyTable<HashedStaticString>::ConstIterator it(aliases);
			while (*it != NULL) {
				const HashedStaticString &name = it.getValue();
				pos = appendData(pos, end, name);
				pos = appendData(pos, end, "\0", 1);
				it.next();
			}
		}

		// Move over pointers to new storage
		{
			StringKeyTable<Entry>::Iterator it(entries);
			pos = newStorage;
			while (*it != NULL) {
				Entry &entry = it.getValue();

				tmpSize = entry.language.size();
				entry.language = StaticString(pos, tmpSize);
				pos += tmpSize + 1;

				tmpSize = entry.languageDisplayName.size();
				entry.languageDisplayName = StaticString(pos, tmpSize);
				pos += tmpSize + 1;

				tmpSize = entry.path.size();
				entry.path = StaticString(pos, tmpSize);
				pos += tmpSize + 1;

				tmpSize = entry.processTitle.size();
				entry.processTitle = StaticString(pos, tmpSize);
				pos += tmpSize + 1;

				tmpSize = entry.defaultInterpreter.size();
				entry.defaultInterpreter = StaticString(pos, tmpSize);
				pos += tmpSize + 1;

				foreach (StaticString &defaultStartupFile,
					entry.defaultStartupFiles)
				{
					tmpSize = defaultStartupFile.size();
					defaultStartupFile = StaticString(pos, tmpSize);
					pos += tmpSize + 1;
				}

				it.next();
			}
		}
		{
			StringKeyTable<HashedStaticString>::Iterator it(aliases);
			while (*it != NULL) {
				HashedStaticString &name = it.getValue();
				tmpSize = name.size();
				name = StaticString(pos, tmpSize);
				pos += tmpSize + 1;
				it.next();
			}
		}

		// Commit current storage
		storage.reset(newStorage);
	}

	void
	addBuiltinEntries() {
		{
			Entry entry;
			entry.language = "ruby";
			entry.languageDisplayName = "Ruby";
			entry.path = "rack-loader.rb";
			entry.processTitle = SHORT_PROGRAM_NAME " RubyApp";
			entry.defaultInterpreter = "ruby";
			entry.defaultStartupFiles.push_back("config.ru");
			entries.insert(entry.language, entry);
			aliases.insert("rack", "ruby");
		}

		{
			Entry entry;
			entry.language = "nodejs";
			entry.languageDisplayName = "Node.js";
			entry.path = "node-loader.js";
			entry.processTitle = SHORT_PROGRAM_NAME " NodejsApp";
			entry.defaultInterpreter = "node";
			// Other code in Passenger does not yet support the notion
			// of multiple defaultStartupFiles.
			//entry.defaultStartupFiles.push_back("index.js");
			entry.defaultStartupFiles.push_back("app.js");
			entries.insert(entry.language, entry);
			aliases.insert("node", "nodejs");
		}

		{
			Entry entry;
			entry.language = "python";
			entry.languageDisplayName = "Python";
			entry.path = "wsgi-loader.py";
			entry.processTitle = SHORT_PROGRAM_NAME " PythonApp";
			entry.defaultInterpreter = "python";
			entry.defaultStartupFiles.push_back("passenger_wsgi.py");
			entries.insert(entry.language, entry);
			aliases.insert("wsgi", "python");
		}

		{
			Entry entry;
			entry.language = "meteor";
			entry.languageDisplayName = "Meteor";
			entry.path = "meteor-loader.rb";
			entry.processTitle = SHORT_PROGRAM_NAME " MeteorApp";
			entry.defaultInterpreter = "ruby"; // because meteor-loader.rb is in Ruby
			entry.defaultStartupFiles.push_back(".meteor");
			entries.insert(entry.language, entry);
		}

		internStrings();
	}

public:
	Registry()
		: finalized(false)
	{
		addBuiltinEntries();
	}

	bool add(const Entry &entry) {
		assert(!isFinalized());
		// Disallow overwriting builtin entries for security reasons.
		// Not sure whether overwriting builtin entries can be harmful
		// but let's err on the safe side.
		bool result = entries.insert(entry.language, entry, false);
		internStrings();
		return result;
	}

	bool isFinalized() const {
		return finalized;
	}

	void finalize() {
		assert(!isFinalized());
		entries.compact();
		aliases.compact();
		finalized = true;
	}

	const Entry &lookup(const HashedStaticString &name) const {
		assert(isFinalized());

		if (OXT_UNLIKELY(name.empty())) {
			return nullEntry;
		}

		const Entry *result;
		HashedStaticString aliasTarget = aliases.lookupCopy(name);
		if (aliasTarget.empty()) {
			entries.lookup(name, &result);
		} else {
			entries.lookup(aliasTarget, &result);
		}
		if (result != NULL) {
			return *result;
		} else {
			return nullEntry;
		}
	}

	const Entry &getNullEntry() const {
		return nullEntry;
	}

	ConstIterator getIterator() const {
		assert(isFinalized());
		return ConstIterator(entries);
	}
};


} // namespace WrapperRegistry
} // namespace Passenger

#endif /* _PASSENGER_WRAPPER_REGISTRY_REGISTRY_H_ */
