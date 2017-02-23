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
#ifndef _PASSENGER_CONFIG_KIT_STORE_H_
#define _PASSENGER_CONFIG_KIT_STORE_H_

#include <string>
#include <vector>

#include <ConfigKit/Common.h>
#include <ConfigKit/Schema.h>
#include <jsoncpp/json.h>
#include <DataStructures/StringKeyTable.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


// See the ConfigKit README for a description.
class Store {
private:
	struct Entry {
		const Schema::Entry *schemaEntry;
		Json::Value userValue;
		mutable Json::Value cachedDefaultValue;
		mutable bool defaultValueCachePopulated;

		Entry()
			: schemaEntry(NULL)
			{ }

		Entry(const Schema::Entry &_schemaEntry)
			: schemaEntry(&_schemaEntry),
			  userValue(Json::nullValue),
			  defaultValueCachePopulated(false)
			{ }

		Json::Value getDefaultValue(const Store *store) const {
			if (defaultValueCachePopulated) {
				return cachedDefaultValue;
			} else if (schemaEntry->defaultValueGetter) {
				if (schemaEntry->flags & CACHE_DEFAULT_VALUE) {
					defaultValueCachePopulated = true;
					cachedDefaultValue = schemaEntry->defaultValueGetter(store);
					return cachedDefaultValue;
				} else {
					return schemaEntry->defaultValueGetter(store);
				}
			} else {
				return Json::Value(Json::nullValue);
			}
		}

		Json::Value getEffectiveValue(const Store *store) const {
			if (userValue.isNull()) {
				return getDefaultValue(store);
			} else {
				return userValue;
			}
		}
	};

	const Schema &schema;
	StringKeyTable<Entry> entries;

	static Json::Value getEffectiveValue(const Json::Value &userValue,
		const Json::Value &defaultValue)
	{
		if (userValue.isNull()) {
			return defaultValue;
		} else {
			return userValue;
		}
	}

public:
	Store(const Schema &_schema)
		: schema(_schema)
	{
		Schema::ConstIterator it = _schema.getIterator();

		while (*it != NULL) {
			Entry entry(it.getValue());
			entries.insert(it.getKey(), entry);
			it.next();
		}

		entries.compact();
	}

	const Schema &getSchema() const {
		return schema;
	}

	/**
	 * Returns the effective value of the given configuration key.
	 * That is: either the user-supplied value, or the default value,
	 * or null (whichever is first applicable).
	 *
	 * Note that `key` *must* be NULL-terminated!
	 */
	Json::Value get(const HashedStaticString &key) const {
		const Entry *entry;

		if (entries.lookup(key, &entry)) {
			return entry->getEffectiveValue(this);
		} else {
			return Json::Value(Json::nullValue);
		}
	}

	Json::Value operator[](const HashedStaticString &key) const {
		return get(key);
	}

	/**
	 * Given a JSON document containing configuration updates, returns
	 * a JSON document that describes how the new configuration would
	 * look like (when the updates are merged with the existing configuration),
	 * and whether it passes validation, without actually updating the
	 * stored configuration.
	 *
	 * You can use the `forceApplyUpdatePreview` method to apply the result, but
	 * be sure to do that only if validation passes.
	 *
	 * If validation fails then any validation errors will be added to `errors`.
	 *
	 * Any keys in `updates` that are not registered are omitted from the result.
	 * Any keys not in `updates` do not affect existing values stored in the store.
	 *
	 * The format returned by this method is the same as that of `dump()`.
	 */
	Json::Value previewUpdate(const Json::Value &updates, vector<Error> &errors) const {
		if (!updates.isNull() && !updates.isObject()) {
			errors.push_back(Error(string(), "The JSON document must be an object"));
			return inspect();
		}

		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(entries);
		Error error;

		while (*it != NULL) {
			const Entry &entry = it.getValue();
			Json::Value subdoc(Json::objectValue);

			if (updates.isMember(it.getKey())) {
				subdoc["user_value"] = updates[it.getKey()];
			} else {
				subdoc["user_value"] = entry.userValue;
			}
			if (entry.schemaEntry->defaultValueGetter) {
				subdoc["default_value"] = entry.getDefaultValue(this);
			}

			const Json::Value &effectiveValue =
				subdoc["effective_value"] =
					getEffectiveValue(subdoc["user_value"],
						subdoc["default_value"]);
			if (!schema.validateValue(it.getKey(), effectiveValue, error)) {
				errors.push_back(error);
			}

			entry.schemaEntry->inspect(subdoc);

			result[it.getKey()] = subdoc;
			it.next();
		}

		return result;
	}

	/**
	 * Applies the result of `updatePreview()` without performing any
	 * validation. Be sure to only call this if you've verified that
	 * `updatePreview()` passes validation, otherwise you will end up
	 * with invalid data in the store.
	 */
	void forceApplyUpdatePreview(const Json::Value &preview) {
		StringKeyTable<Entry>::Iterator it(entries);
		while (*it != NULL) {
			Entry &entry = it.getValue();
			const Json::Value &subdoc =
				const_cast<const Json::Value &>(preview)[it.getKey()];
			entry.userValue = subdoc["user_value"];
			it.next();
		}
	}

	/**
	 * Attempts to merge the given configuration updates into this store.
	 * Only succeeds if the merged result passes validation. Any
	 * validation errors are stored in `errors`.
	 * Returns whether the update succeeded.
	 *
	 * Any keys in `updates` that are not registered will not participate in the update.
	 * Any keys not in `updates` do not affect existing values stored in the store.
	 */
	bool update(const Json::Value &updates, vector<Error> &errors) {
		Json::Value preview = previewUpdate(updates, errors);
		if (errors.empty()) {
			forceApplyUpdatePreview(preview);
			return true;
		} else {
			return false;
		}
	}

	/**
	 * Inspects the current store's configuration keys and values in a format
	 * that displays user-supplied and effective values, as well as
	 * other useful information. See the README's "Inspecting all data"
	 * section to learn about the format.
	 */
	Json::Value inspect() const {
		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(entries);

		while (*it != NULL) {
			const Entry &entry = it.getValue();
			Json::Value subdoc(Json::objectValue);

			subdoc["user_value"] = entry.userValue;
			if (entry.schemaEntry->defaultValueGetter) {
				subdoc["default_value"] = entry.getDefaultValue(this);
			}
			subdoc["effective_value"] = entry.getEffectiveValue(this);
			entry.schemaEntry->inspect(subdoc);

			result[it.getKey()] = subdoc;
			it.next();
		}

		return result;
	}
};


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_STORE_H_ */
