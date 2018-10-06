/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017-2018 Phusion Holding B.V.
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
#include <cassert>
// for std::swap()
#if __cplusplus >= 201103L
	#include <utility>
#else
	#include <algorithm>
#endif
#include <boost/config.hpp>

#include <jsoncpp/json.h>

#include <ConfigKit/Common.h>
#include <ConfigKit/Schema.h>
#include <ConfigKit/Utils.h>
#include <ConfigKit/Translator.h>
#include <LoggingKit/Assert.h>
#include <Exceptions.h>
#include <DataStructures/StringKeyTable.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


// See the ConfigKit README for a description.
class Store {
private:
	friend class Schema;

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

		Json::Value getDefaultValue(const Store &store) const {
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

		Json::Value getEffectiveValue(const Store &store) const {
			if (userValue.isNull()) {
				return getDefaultValue(store);
			} else if (schemaEntry->nestedSchema == NULL) {
				return userValue;
			} else {
				// The user value may contain nulls that should
				// be populated with the default value from the
				// corresponding nested schema.
				Json::Value result;
				schemaEntry->tryTypecastArrayOrObjectValueWithNestedSchema(
					userValue, result, "effective_value");
				return result;
			}
		}
	};

	const Schema *schema;
	StringKeyTable<Entry> entries;
	bool updatedOnce;

	static Json::Value getEffectiveValue(const Json::Value &userValue,
		const Json::Value &defaultValue, const Schema::Entry &schemaEntry)
	{
		if (userValue.isNull()) {
			return defaultValue;
		} else if (schemaEntry.nestedSchema == NULL) {
			return userValue;
		} else {
			// The user value may contain nulls that should
			// be populated with the default value from the
			// corresponding nested schema.
			Json::Value result;
			schemaEntry.tryTypecastArrayOrObjectValueWithNestedSchema(
				userValue, result, "effective_value");
			return result;
		}
	}

	static Json::Value maybeFilterSecret(const Entry &entry, const Json::Value &value) {
		if (entry.schemaEntry->flags & SECRET) {
			if (value.isNull()) {
				return Json::nullValue;
			} else {
				return "[FILTERED]";
			}
		} else {
			return value;
		}
	}

	void initialize() {
		Schema::ConstIterator it = schema->getIterator();

		while (*it != NULL) {
			Entry entry(it.getValue());
			entries.insert(it.getKey(), entry);
			it.next();
		}

		entries.compact();
	}

	bool isWritable(const Entry &entry) const {
		return !(entry.schemaEntry->flags & READ_ONLY) || !updatedOnce;
	}

	void applyCustomValidators(const Json::Value &updates, vector<Error> &errors) const {
		Store tempStore(*schema);
		StringKeyTable<Entry>::Iterator it(tempStore.entries);

		while (*it != NULL) {
			const HashedStaticString &key = it.getKey();
			Entry &entry = it.getValue();

			if (isWritable(entry) && updates.isMember(key)) {
				entry.userValue = updates[key];
			}

			it.next();
		}

		boost::container::vector<Schema::Validator>::const_iterator v_it, v_end
			= schema->getValidators().end();
		for (v_it = schema->getValidators().begin(); v_it != v_end; v_it++) {
			const Schema::Validator &validator = *v_it;
			validator(tempStore, errors);
		}
	}

	void applyNormalizers(Json::Value &doc) const {
		boost::container::vector<Schema::Normalizer>::const_iterator n_it, n_end;

		n_it = schema->getNormalizers().begin();
		n_end = schema->getNormalizers().end();
		for (; n_it != n_end; n_it++) {
			const Schema::Normalizer &normalizer = *n_it;
			Json::Value effectiveValues(Json::objectValue);
			Json::Value::iterator it, end = doc.end();

			for (it = doc.begin(); it != end; it++) {
				string name = it.name();
				effectiveValues[name] = doc[name]["effective_value"];
			}

			Json::Value updates = normalizer(effectiveValues);
			if (OXT_UNLIKELY(!updates.isNull() && !updates.isObject())) {
				P_BUG("ConfigKit normalizers may only return null or object values");
			}
			if (updates.isNull() || updates.empty()) {
				continue;
			}

			end = updates.end();
			for (it = updates.begin(); it != end; it++) {
				string name = it.name();
				if (doc.isMember(name)) {
					Json::Value &subdoc = doc[name];
					subdoc["user_value"] = *it;
					subdoc["effective_value"] = *it;
				} else {
					P_BUG("A ConfigKit normalizer returned a key that is not part of the schema: "
						<< name);
				}
			}
		}
	}

	void applyInspectFilters(Json::Value &doc) const {
		StringKeyTable<Entry>::ConstIterator it(entries);
		while (*it != NULL) {
			const Entry &entry = it.getValue();
			if (entry.schemaEntry->inspectFilter.empty()) {
				it.next();
				continue;
			}

			const HashedStaticString &key = it.getKey();
			Json::Value &subdoc = doc[key];

			Json::Value &userValue = subdoc["user_value"];
			userValue = entry.schemaEntry->inspectFilter(userValue);

			if (subdoc.isMember("default_value")) {
				Json::Value &defaultValue = subdoc["default_value"];
				defaultValue = entry.schemaEntry->inspectFilter(defaultValue);
			}

			Json::Value &effectiveValue = subdoc["effective_value"];
			effectiveValue = entry.schemaEntry->inspectFilter(effectiveValue);

			it.next();
		}
	}

	void doFilterSecrets(Json::Value &doc) const {
		StringKeyTable<Entry>::ConstIterator it(entries);
		while (*it != NULL) {
			const HashedStaticString &key = it.getKey();
			const Entry &entry = it.getValue();
			Json::Value &subdoc = doc[key];

			Json::Value &userValue = subdoc["user_value"];
			userValue = maybeFilterSecret(entry, userValue);

			if (subdoc.isMember("default_value")) {
				Json::Value &defaultValue = subdoc["default_value"];
				defaultValue = maybeFilterSecret(entry, defaultValue);
			}

			Json::Value &effectiveValue = subdoc["effective_value"];
			effectiveValue = maybeFilterSecret(entry, effectiveValue);

			it.next();
		}
	}

public:
	struct PreviewOptions {
		bool filterSecrets;
		bool shouldApplyInspectFilters;

		PreviewOptions()
			: filterSecrets(true),
			  shouldApplyInspectFilters()
			{ }
	};

	Store()
		: schema(NULL),
		  entries(0, 0),
		  updatedOnce(false)
		{ }

	Store(const Schema &_schema)
		: schema(&_schema),
		  updatedOnce(false)
	{
		initialize();
	}

	Store(const Schema &_schema, const Json::Value &initialValues)
		: schema(&_schema),
		  updatedOnce(false)
	{
		vector<Error> errors;
		initialize();
		if (!update(initialValues, errors)) {
			throw ArgumentException("Invalid initial configuration: "
				+ toString(errors));
		}
	}

	Store(const Schema &_schema, const Json::Value &initialValues,
		const Translator &translator)
		: schema(&_schema),
		  updatedOnce(false)
	{
		vector<Error> errors;
		initialize();
		if (!update(translator.translate(initialValues), errors)) {
			errors = translator.reverseTranslate(errors);
			throw ArgumentException("Invalid initial configuration: "
				+ toString(errors));
		}
	}

	Store(const Store &other, const Json::Value &updates, vector<Error> &errors)
		: schema(other.schema),
		  updatedOnce(false)
	{
		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(other.entries);

		while (*it != NULL) {
			const Entry &entry = it.getValue();
			if (updates.isMember(it.getKey())) {
				result[it.getKey()] = updates[it.getKey()];
			} else if (!entry.userValue.isNull()) {
				result[it.getKey()] = entry.userValue;
			}
			it.next();
		}

		initialize();
		update(result, errors);
	}

	const Schema &getSchema() const {
		return *schema;
	}

	bool hasBeenUpdatedAtLeastOnce() const {
		return updatedOnce;
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
			return entry->getEffectiveValue(*this);
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
	 * If validation fails then any validation errors will be added to `errors`.
	 *
	 * Any keys in `updates` that are not registered are omitted from the result.
	 * Any keys not in `updates` do not affect existing values stored in the store.
	 *
	 * The format returned by this method is the same as that of `inspect()`,
	 * with the following exceptions:
	 *
	 *  - If `filterSecrets` is set to false, values of fields
	 *    marked with the `SECRET` flag are not filtered.
	 *  - If `shouldApplyInspectFilters` is set to false, values of fields
	 *    are not passed through inspect filters.
	 */
	Json::Value previewUpdate(const Json::Value &updates, vector<Error> &errors,
		const PreviewOptions &options = PreviewOptions()) const
	{
		if (!updates.isNull() && !updates.isObject()) {
			errors.push_back(Error("The JSON document must be an object"));
			return inspect();
		}

		Json::Value result(Json::objectValue);
		Store storeWithPreviewData(*this);
		StringKeyTable<Entry>::Iterator p_it(storeWithPreviewData.entries);
		StringKeyTable<Entry>::ConstIterator it(entries);
		vector<Error> tmpErrors;

		while (*p_it != NULL) {
			const HashedStaticString &key = p_it.getKey();
			Entry &entry = p_it.getValue();

			if (isWritable(entry) && updates.isMember(key)) {
				bool ok = entry.schemaEntry->tryTypecastValue(
					updates[key], entry.userValue);
				if (!ok) {
					entry.userValue = updates[key];
				}
			}

			p_it.next();
		}

		while (*it != NULL) {
			const HashedStaticString &key = it.getKey();
			const Entry &entry = it.getValue();
			Json::Value subdoc(Json::objectValue);

			entry.schemaEntry->inspect(subdoc);

			if (isWritable(entry) && updates.isMember(key)) {
				bool ok = entry.schemaEntry->tryTypecastValue(updates[key],
					subdoc["user_value"]);
				if (!ok) {
					subdoc["user_value"] = updates[key];
				}
			} else {
				subdoc["user_value"] = entry.userValue;
			}

			if (entry.schemaEntry->defaultValueGetter) {
				subdoc["default_value"] = entry.getDefaultValue(storeWithPreviewData);
			}

			const Json::Value &effectiveValue =
				subdoc["effective_value"] =
					getEffectiveValue(subdoc["user_value"],
						subdoc["default_value"],
						*entry.schemaEntry);
			schema->validateValue(it.getKey(), effectiveValue, tmpErrors);

			result[it.getKey()] = subdoc;
			it.next();
		}

		if (!schema->getValidators().empty()) {
			applyCustomValidators(updates, tmpErrors);
		}

		if (tmpErrors.empty()) {
			applyNormalizers(result);
		}

		if (options.shouldApplyInspectFilters) {
			applyInspectFilters(result);
		}

		if (options.filterSecrets) {
			doFilterSecrets(result);
		}

		errors.insert(errors.end(), tmpErrors.begin(), tmpErrors.end());

		return result;
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
		PreviewOptions options;
		options.filterSecrets = false;
		options.shouldApplyInspectFilters = false;
		Json::Value preview = previewUpdate(updates, errors, options);
		if (errors.empty()) {
			StringKeyTable<Entry>::Iterator it(entries);
			while (*it != NULL) {
				Entry &entry = it.getValue();
				if (isWritable(entry)) {
					const Json::Value &subdoc =
						const_cast<const Json::Value &>(preview)[it.getKey()];
					entry.userValue = subdoc["user_value"];
				}
				it.next();
			}
			updatedOnce = true;
			return true;
		} else {
			return false;
		}
	}

	Store extractDataForSubSchema(const Schema &subSchema,
		const Translator &translator) const
	{
		Store result(subSchema);
		StringKeyTable<Entry>::Iterator it(result.entries);

		while (*it != NULL) {
			const HashedStaticString &subSchemaKey = it.getKey();
			Entry &subSchemaEntry = it.getValue();
			const string mainSchemaKey = translator.reverseTranslateOne(
				subSchemaKey);
			const Entry *mainSchemaEntry;

			if (entries.lookup(mainSchemaKey, &mainSchemaEntry)) {
				subSchemaEntry.userValue = mainSchemaEntry->userValue;
			}

			it.next();
		}

		return result;
	}

	void swap(Store &other) BOOST_NOEXCEPT_OR_NOTHROW {
		std::swap(schema, other.schema);
		entries.swap(other.entries);
		std::swap(updatedOnce, other.updatedOnce);
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

			entry.schemaEntry->inspect(subdoc);
			subdoc["user_value"] = entry.userValue;
			subdoc["effective_value"] = entry.getEffectiveValue(*this);
			if (entry.schemaEntry->defaultValueGetter && entry.schemaEntry->flags & _DYNAMIC_DEFAULT_VALUE) {
				subdoc["default_value"] = entry.getDefaultValue(*this);
			}

			result[it.getKey()] = subdoc;
			it.next();
		}

		applyInspectFilters(result);
		doFilterSecrets(result);

		return result;
	}

	/**
	 * Inspects the current store's configuration keys and effective
	 * values only. This is like `inspect()` but much less verbose.
	 * See the README's "Inspecting all data" section to learn more
	 * about the format.
	 * Note that values with the SECRET flag are not filtered.
	 */
	Json::Value inspectEffectiveValues() const {
		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(entries);

		while (*it != NULL) {
			const Entry &entry = it.getValue();
			result[it.getKey()] = entry.getEffectiveValue(*this);
			it.next();
		}

		return result;
	}

	/**
	 * Inspects the current store's configuration keys and user
	 * values only. This is like `inspect()` but much less verbose.
	 * Note that values with the SECRET flag are not filtered.
	 */
	Json::Value inspectUserValues() const {
		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(entries);

		while (*it != NULL) {
			const Entry &entry = it.getValue();
			result[it.getKey()] = entry.userValue;
			it.next();
		}

		return result;
	}
};


inline bool
Schema::Entry::tryTypecastArrayOrObjectValueWithNestedSchema(const Json::Value &val,
	Json::Value &result, const char *userOrEffectiveValue) const
{
	assert(type == ARRAY_TYPE || type == OBJECT_TYPE);
	assert(nestedSchema != NULL);
	assert(!val.isNull());
	assert(val.isConvertibleTo(Json::arrayValue)
		|| val.isConvertibleTo(Json::objectValue));

	bool ok = true;
	result = val;

	Json::Value::iterator it, end = result.end();
	for (it = result.begin(); it != end; it++) {
		Json::Value &userSubdoc = *it;
		if (!userSubdoc.isConvertibleTo(Json::objectValue)) {
			ok = false;
			continue;
		}

		vector<Error> errors;
		Json::Value preview = Store(*nestedSchema).previewUpdate(
			userSubdoc, errors);
		Json::Value::const_iterator p_it, p_end = preview.end();
		for (p_it = preview.begin(); p_it != p_end; p_it++) {
			const Json::Value &previewSubdoc = *p_it;
			userSubdoc[p_it.name()] = previewSubdoc[userOrEffectiveValue];
		}
	}
	return ok;
}

inline Json::Value
Schema::getValueFromSubSchema(
	const Store &store,
	const Schema *subschema, const Translator *translator,
	const HashedStaticString &key)
{
	Store tempStore = store.extractDataForSubSchema(*subschema, *translator);
	Store::Entry *tempEntry;
	if (tempStore.entries.lookup(translator->translateOne(key), &tempEntry)) {
		if (tempEntry->schemaEntry->defaultValueGetter) {
			return tempEntry->schemaEntry->defaultValueGetter(tempStore);
		} else {
			return Json::nullValue;
		}
	} else {
		return Json::nullValue;
	}
}

inline void
Schema::validateSubSchema(const Store &store, vector<Error> &errors,
	const Schema *subschema, const Translator *translator,
	const Validator &origValidator)
{
	Store tempStore = store.extractDataForSubSchema(*subschema, *translator);
	vector<Error> tempErrors;
	origValidator(tempStore, tempErrors);
	if (!tempErrors.empty()) {
		tempErrors = translator->reverseTranslate(tempErrors);
		errors.insert(errors.end(), tempErrors.begin(), tempErrors.end());
	}
}

inline Json::Value
Schema::normalizeSubSchema(const Json::Value &effectiveValues,
	const Schema *mainSchema, const Schema *subschema,
	const Translator *translator, const Normalizer &origNormalizer)
{
	Json::Value translatedEffectiveValues(Json::objectValue);
	StringKeyTable<Entry>::ConstIterator it(subschema->entries);

	while (*it != NULL) {
		const HashedStaticString &subSchemaKey = it.getKey();
		const string mainSchemaKey = translator->reverseTranslateOne(
			subSchemaKey);
		const Entry *mainSchemaEntry;

		if (mainSchema->entries.lookup(mainSchemaKey, &mainSchemaEntry)) {
			translatedEffectiveValues[subSchemaKey] = effectiveValues[mainSchemaKey];
		}

		it.next();
	}

	return translator->reverseTranslate(origNormalizer(translatedEffectiveValues));
}

inline Json::Value
Schema::getStaticDefaultValue(const Schema::Entry &entry) {
	Store::Entry storeEntry(entry);
	return Store::maybeFilterSecret(storeEntry, storeEntry.getDefaultValue(Store()));
}

inline bool
Schema::validateNestedSchemaArrayValue(const HashedStaticString &key,
	const Schema::Entry &entry, const Json::Value &value, vector<Error> &errors)
{
	bool warnedAboutNonObjectValue = false;
	bool result = true;

	Json::Value::const_iterator it, end = value.end();
	for (it = value.begin(); it != end; it++) {
		if (!it->isConvertibleTo(Json::objectValue)) {
			if (!warnedAboutNonObjectValue) {
				warnedAboutNonObjectValue = true;
				result = false;
				errors.push_back(Error(
					"'{{" + key + "}}' may only contain JSON objects"));
			}
			continue;
		}

		Store store(*entry.nestedSchema);
		vector<Error> nestedSchemaErrors;
		if (store.update(*it, nestedSchemaErrors)) {
			continue;
		}

		vector<Error>::const_iterator e_it, e_end = nestedSchemaErrors.end();
		for (e_it = nestedSchemaErrors.begin(); e_it != e_end; e_it++) {
			errors.push_back(Error("'{{" + key + "}}' element "
				+ Passenger::toString(it.index() + 1) + " is invalid: "
				+ e_it->getMessage()));
		}
		result = false;
	}

	return result;
}

inline bool
Schema::validateNestedSchemaObjectValue(const HashedStaticString &key,
	const Schema::Entry &entry, const Json::Value &value, vector<Error> &errors)
{
	bool warnedAboutNonObjectValue = false;
	bool result = true;

	Json::Value::const_iterator it, end = value.end();
	for (it = value.begin(); it != end; it++) {
		if (!it->isConvertibleTo(Json::objectValue)) {
			if (!warnedAboutNonObjectValue) {
				warnedAboutNonObjectValue = true;
				result = false;
				errors.push_back(Error(
					"'{{" + key + "}}' may only contain JSON objects"));
			}
			continue;
		}

		Store store(*entry.nestedSchema);
		vector<Error> nestedSchemaErrors;
		if (store.update(*it, nestedSchemaErrors)) {
			continue;
		}

		vector<Error>::const_iterator e_it, e_end = nestedSchemaErrors.end();
		for (e_it = nestedSchemaErrors.begin(); e_it != e_end; e_it++) {
			errors.push_back(Error("'{{" + key + "}}' key '"
				+ it.name() + "' is invalid: "
				+ e_it->getMessage()));
		}
		result = false;
	}

	return result;
}


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_STORE_H_ */
