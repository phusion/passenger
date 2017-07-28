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
#ifndef _PASSENGER_CONFIG_KIT_SCHEMA_H_
#define _PASSENGER_CONFIG_KIT_SCHEMA_H_

#include <boost/bind.hpp>
#include <boost/container/vector.hpp>
#include <string>
#include <cassert>

#include <oxt/backtrace.hpp>
#include <jsoncpp/json.h>

#include <Exceptions.h>
#include <Logging.h>
#include <ConfigKit/Common.h>
#include <ConfigKit/Utils.h>
#include <DataStructures/StringKeyTable.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {
namespace ConfigKit {

using namespace std;


/**
 * Represents a configuration schema. See the ConfigKit README for a description.
 *
 * Schema is thread-safe after finalization because it becomes immutable.
 */
class Schema {
public:
	struct Entry {
		Type type;
		Flags flags;
		ValueGetter defaultValueGetter;

		Entry()
			: type(UNKNOWN_TYPE),
			  flags(OPTIONAL)
			{ }

		Entry(Type _type, Flags _flags, const ValueGetter &_defaultValueGetter)
			: type(_type),
			  flags(_flags),
			  defaultValueGetter(_defaultValueGetter)
			{ }

		Json::Value inspect() const {
			Json::Value result(Json::objectValue);
			inspect(result);
			return result;
		}

		void inspect(Json::Value &doc) const {
			doc["type"] = getTypeString(type).data();
			if (flags & REQUIRED) {
				doc["required"] = true;
			}
			if (flags & READ_ONLY) {
				doc["read_only"] = true;
			}
			if (defaultValueGetter) {
				doc["has_default_value"] = true;
			}
		}
	};

	typedef StringKeyTable<Entry>::ConstIterator ConstIterator;
	typedef boost::function<void (const Store &store, vector<Error> &errors)> Validator;

private:
	class DummyTranslator {
	public:
		StaticString translateOne(const StaticString &key) const {
			return key;
		}

		StaticString reverseTranslateOne(const StaticString &key) const {
			return key;
		}

		vector<Error> reverseTranslate(const vector<Error> &errors) const {
			return errors;
		}
	};

	StringKeyTable<Entry> entries;
	boost::container::vector<Validator> validators;
	bool finalized;

	static Json::Value returnJsonValue(const Store &store, Json::Value v) {
		return v;
	}

	template<typename Translator>
	static Json::Value getValueFromSubSchema(
		const Store &storeWithMainSchema,
		const Schema *subschema, const Translator *translator,
		const HashedStaticString &key);

	template<typename Translator>
	static void validateSubSchema(const Store &store, vector<Error> &errors,
		const Schema *subschema, const Translator *translator,
		const Validator &origValidator);

public:
	Schema()
		: finalized(false)
		{ }

	/**
	 * Register a new schema entry, possibly with a static default value.
	 */
	void add(const HashedStaticString &key, Type type, unsigned int flags,
		const Json::Value &defaultValue = Json::Value(Json::nullValue))
	{
		assert(!finalized);
		if (defaultValue.isNull()) {
			Entry entry(type, (Flags) flags, ValueGetter());
			entries.insert(key, entry);
		} else {
			if (flags & REQUIRED) {
				throw ArgumentException(
					"A key cannot be required and have a default value at the same time");
			}
			Entry entry(type, (Flags) flags,
				boost::bind(returnJsonValue, boost::placeholders::_1, defaultValue));
			entries.insert(key, entry);
		}
	}

	/**
	 * Register a new schema entry with a dynamic default value.
	 */
	void addWithDynamicDefault(const HashedStaticString &key, Type type, unsigned int flags,
		const ValueGetter &defaultValueGetter)
	{
		if (flags & REQUIRED) {
			throw ArgumentException(
				"A key cannot be required and have a default value at the same time");
		}
		assert(!finalized);
		Entry entry(type, (Flags) (flags | _DYNAMIC_DEFAULT_VALUE), defaultValueGetter);
		entries.insert(key, entry);
	}

	void addSubSchema(const Schema &subschema) {
		addSubSchema(subschema, DummyTranslator());
	}

	template<typename Translator>
	void addSubSchema(const Schema &subschema, const Translator &translator) {
		assert(!finalized);
		assert(subschema.finalized);
		Schema::ConstIterator it = subschema.getIterator();

		while (*it != NULL) {
			const HashedStaticString &key = it.getKey();
			const Schema::Entry &entry = it.getValue();
			ValueGetter valueGetter;

			if (entry.defaultValueGetter) {
				if (entry.flags & _DYNAMIC_DEFAULT_VALUE) {
					valueGetter = boost::bind<Json::Value>(
						getValueFromSubSchema<Translator>,
						boost::placeholders::_1, &subschema, &translator,
						key);
				} else {
					valueGetter = entry.defaultValueGetter;
				}
			}

			Entry entry2(entry.type, (Flags) (entry.flags | _FROM_SUBSCHEMA),
				valueGetter);
			entries.insert(translator.reverseTranslateOne(key), entry2);
			it.next();
		}

		boost::container::vector<Schema::Validator>::const_iterator v_it, v_end
			= subschema.getValidators().end();
		for (v_it = subschema.getValidators().begin(); v_it != v_end; v_it++) {
			validators.push_back(boost::bind(validateSubSchema<Translator>,
				boost::placeholders::_1, boost::placeholders::_2,
				&subschema, &translator, *v_it));
		}
	}

	void addValidator(const Validator &validator) {
		assert(!finalized);
		validators.push_back(validator);
	}

	void finalize() {
		assert(!finalized);
		entries.compact();
		finalized = true;
		validators.shrink_to_fit();
	}

	bool get(const HashedStaticString &key, const Entry **entry) const {
		assert(finalized);
		return entries.lookup(key, entry);
	}

	/**
	 * Apply standard validation rules -- that do not depend on a particular
	 * configuration store -- to the given configuration key and value.
	 * Validators added with `addValidator()` won't be applied.
	 *
	 * Returns whether validation passed. If not, then `error` is set.
	 */
	bool validateValue(const HashedStaticString &key, const Json::Value &value,
		Error &error) const
	{
		const Entry *entry;

		assert(finalized);
		if (!entries.lookup(key, &entry)) {
			throw ArgumentException("Unknown key " + key);
		}

		if (value.isNull()) {
			if (entry->flags & REQUIRED) {
				error = Error("'{{" + key + "}}' is required");
				return false;
			} else {
				return true;
			}
		}

		switch (entry->type) {
		case STRING_TYPE:
		case PASSWORD_TYPE:
			if (value.isConvertibleTo(Json::stringValue)) {
				return true;
			} else {
				error = Error("'{{" + key + "}}' must be a string");
				return false;
			}
		case INT_TYPE:
			if (value.isConvertibleTo(Json::intValue)) {
				return true;
			} else {
				error = Error("'{{" + key + "}}' must be an integer");
				return false;
			}
		case UINT_TYPE:
			if (value.isConvertibleTo(Json::intValue)) {
				if (value.isConvertibleTo(Json::uintValue)) {
					return true;
				} else {
					error = Error("'{{" + key + "}}' must be greater than 0");
					return false;
				}
			} else {
				error = Error("'{{" + key + "}}' must be an integer");
				return false;
			}
		case FLOAT_TYPE:
			if (value.isConvertibleTo(Json::realValue)) {
				return true;
			} else {
				error = Error("'{{" + key + "}}' must be a number");
				return false;
			}
		case BOOL_TYPE:
			if (value.isConvertibleTo(Json::booleanValue)) {
				return true;
			} else {
				error = Error("'{{" + key + "}}' must be a boolean");
				return false;
			}
		default:
			P_BUG("Unknown type " + Passenger::toString((int) entry->type));
			return false;
		};
	}

	const boost::container::vector<Validator> &getValidators() const {
		assert(finalized);
		return validators;
	}

	ConstIterator getIterator() const {
		assert(finalized);
		return ConstIterator(entries);
	}

	Json::Value inspect() const {
		assert(finalized);
		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(entries);

		while (*it != NULL) {
			result[it.getKey()] = it.getValue().inspect();
			it.next();
		}

		return result;
	}
};


} // namespace ConfigKit
} // namespace Passenger

#endif /* _PASSENGER_CONFIG_KIT_SCHEMA_H_ */
