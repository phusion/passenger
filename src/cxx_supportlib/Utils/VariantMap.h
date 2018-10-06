/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_VARIANT_MAP_H_
#define _PASSENGER_VARIANT_MAP_H_

#include <oxt/backtrace.hpp>
#include <oxt/macros.hpp>
#include <sys/types.h>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <jsoncpp/json.h>
#include <modp_b64.h>
#include <Exceptions.h>
#include <StrIntTools/StrIntUtils.h>
#include <IOTools/MessageIO.h>

namespace Passenger {

using namespace std;

/**
 * A map which maps string keys to values of any type. Internally all values
 * are stored as strings, but convenience functions are provided to cast
 * to and from other types.
 *
 * <h2>get() methods</h2>
 *
 * There are many get() versions but they all behave the same way, just returning
 * different types.
 * <tt>get(name)</tt> returns the value associated with the key <em>name</em>.
 * If the key doesn't exist then the behavior depends on the <em>required</em> argument:
 * - If <em>required</em> is true, then a MissingKeyException will be thrown.
 * - If <em>required</em> is false, then <em>defaultValue</em> will be returned.
 *   (In case of the string version, <em>defaultValue</em> defaults to the empty string.)
 */
class VariantMap {
private:
	map<string, string> store;
	string empty;

	/**
	 * Looks up the string value associated with <em>name</em>.
	 * If found, then <tt>true</tt> is returned and a pointer to
	 * the string value is stored in <tt>*strValue</tt>.
	 *
	 * If not found, and <em>required</em> is true, then a
	 * MissingKeyException will be thrown.
	 *
	 * If not found, and <em>required</em> is false, then
	 * <tt>false</tt> is returned.
	 */
	bool lookup(const string &name, bool required, const string **strValue) const {
		map<string, string>::const_iterator it = store.find(name);
		if (it == store.end()) {
			if (required) {
				throw MissingKeyException(name);
			} else {
				return false;
			}
		} else {
			*strValue = &it->second;
			return true;
		}
	}

public:
	/** Thrown when a required key is not found by one of the get() methods. */
	class MissingKeyException: public oxt::tracable_exception {
	private:
		string message;
		string key;

	public:
		MissingKeyException(const string &key) {
			this->key = key;
			message = string("Required key '") + key + "' is missing";
		}

		virtual ~MissingKeyException() throw() { }

		virtual const char *what() const throw() {
			return message.c_str();
		}

		/** The key that wasn't found. */
		const string &getKey() const {
			return key;
		}
	};

	typedef map<string, string>::iterator Iterator;
	typedef map<string, string>::const_iterator ConstIterator;

	/**
	 * Populates a VariantMap from the data in <em>argv</em>, which
	 * consists of <em>argc</em> elements.
	 * <em>argv</em> must be an array containing keys followed by
	 * values, like this:
	 * <tt>[key1, value1, key2, value2, ...]</tt>
	 *
	 * @throws ArgumentException The <em>argv</em> array does not
	 *                           contain valid key-value pairs.
	 */
	void readFrom(const char **argv, unsigned int argc) {
		if (OXT_UNLIKELY(argc % 2 != 0)) {
			throw ArgumentException("argc must be a multiple of 2");
		}
		unsigned int i = 0;
		while (i < argc) {
			string name = argv[i];
			if (startsWith(name, "--")) {
				name.erase(0, 2);
			}
			name = replaceAll(name, "-", "_");

			store[name] = replaceAll(argv[i + 1], "-", "_");
			i += 2;
		}
	}

	/**
	 * Populates a VariantMap from the data in `fd`. MessageIO
	 * is used to read from the file descriptor.
	 *
	 * @throws SystemException
	 * @throws IOException
	 */
	void readFrom(int fd, const StaticString &messageName = "VariantMap") {
		TRACE_POINT();
		vector<string> args;

		if (!readArrayMessage(fd, args)) {
			throw IOException("Unexpected end-of-file encountered");
		}
		if (args.size() == 0) {
			throw IOException("Unexpected empty message received from channel");
		}
		if (args[0] != messageName) {
			throw IOException("Unexpected message '" + args[0] + "' received from channel");
		}
		if (args.size() % 2 != 1) {
			throw IOException("Message from channel has an unexpected number of arguments");
		}

		vector<string>::const_iterator it = args.begin();
		it++;
		while (it != args.end()) {
			const string &key = *it;
			it++;
			const string &value = *it;
			it++;
			store[key] = value;
		}
	}

	VariantMap &set(const string &name, const string &value) {
		if (value.empty()) {
			map<string, string>::iterator it = store.find(name);
			if (it != store.end()) {
				store.erase(it);
			}
		} else {
			store[name] = value;
		}
		return *this;
	}

	VariantMap &setDefault(const string &name, const string &value) {
		if (store.find(name) == store.end()) {
			set(name, value);
		}
		return *this;
	}

	VariantMap &setInt(const string &name, int value) {
		set(name, toString(value));
		return *this;
	}

	VariantMap &setUint(const string &name, unsigned int value) {
		set(name, toString(value));
		return *this;
	}

	VariantMap &setDefaultInt(const string &name, int value) {
		if (store.find(name) == store.end()) {
			store[name] = toString(value);
		}
		return *this;
	}

	VariantMap &setDefaultUint(const string &name, unsigned int value) {
		if (store.find(name) == store.end()) {
			store[name] = toString(value);
		}
		return *this;
	}

	VariantMap &setULL(const string &name, unsigned long long value) {
		set(name, toString(value));
		return *this;
	}

	VariantMap &setDefaultULL(const string &name, unsigned long long value) {
		if (store.find(name) == store.end()) {
			store[name] = toString(value);
		}
		return *this;
	}

	VariantMap &setPid(const string &name, pid_t value) {
		set(name, toString((unsigned long long) value));
		return *this;
	}

	VariantMap &setDefaultPid(const string &name, pid_t value) {
		if (store.find(name) == store.end()) {
			store[name] = toString((unsigned long long) value);
		}
		return *this;
	}

	VariantMap &setUid(const string &name, uid_t value) {
		set(name, toString((long long) value));
		return *this;
	}

	VariantMap &setDefaultUid(const string &name, uid_t value) {
		if (store.find(name) == store.end()) {
			store[name] = toString((unsigned long long) value);
		}
		return *this;
	}

	VariantMap &setGid(const string &name, gid_t value) {
		set(name, toString((long long) value));
		return *this;
	}

	VariantMap &setDefaultGid(const string &name, gid_t value) {
		if (store.find(name) == store.end()) {
			store[name] = toString((unsigned long long) value);
		}
		return *this;
	}

	VariantMap &setBool(const string &name, bool value) {
		set(name, value ? "true" : "false");
		return *this;
	}

	VariantMap &setDefaultBool(const string &name, bool value) {
		if (store.find(name) == store.end()) {
			store[name] = value ? "true" : "false";
		}
		return *this;
	}

	template <typename StringCollection>
	VariantMap &setStrSet(const string &name, const StringCollection &value) {
		typename StringCollection::const_iterator it;
		string result;

		for (it = value.begin(); it != value.end(); it++) {
			if (it != value.begin()) {
				result.append(1, '\0');
			}
			result.append(*it);
		}
		set(name, modp::b64_encode(result));
		return *this;
	}

	template <typename StringCollection>
	VariantMap &setDefaultStrSet(const string &name, const StringCollection &value) {
		if (store.find(name) == store.end()) {
			setStrSet(name, value);
		}
		return *this;
	}

	const string &get(const string &name, bool required = true) const {
		map<string, string>::const_iterator it = store.find(name);
		if (it == store.end()) {
			if (required) {
				throw MissingKeyException(name);
			} else {
				return empty;
			}
		} else {
			return it->second;
		}
	}

	const string &get(const string &name, bool required, const string &defaultValue) const {
		map<string, string>::const_iterator it = store.find(name);
		if (it == store.end()) {
			if (required) {
				throw MissingKeyException(name);
			} else {
				return defaultValue;
			}
		} else {
			return it->second;
		}
	}

	int getInt(const string &name, bool required = true, int defaultValue = 0) const {
		int result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result = (int) stringToLL(*str);
		}
		return result;
	}

	unsigned int getUint(const string &name, bool required = true, unsigned int defaultValue = 0) const {
		unsigned int result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			long long val = stringToLL(*str);
			if (val < 0) {
				result = 0;
			} else {
				result = (unsigned int) val;
			}
		}
		return result;
	}

	unsigned long long getULL(const string &name, bool required = true,
		unsigned long long defaultValue = 0) const
	{
		unsigned long long result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result = stringToULL(*str);
		}
		return result;
	}

	double getDouble(const string &name, bool required = true,
		double defaultValue = 0) const
	{
		double result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result = atof(str->c_str());
		}
		return result;
	}

	pid_t getPid(const string &name, bool required = true, pid_t defaultValue = 0) const {
		pid_t result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result = (pid_t) stringToLL(*str);
		}
		return result;
	}

	uid_t getUid(const string &name, bool required = true, uid_t defaultValue = 0) const {
		uid_t result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result = (uid_t) stringToLL(*str);
		}
		return result;
	}

	gid_t getGid(const string &name, bool required = true, gid_t defaultValue = 0) const {
		gid_t result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result = (gid_t) stringToLL(*str);
		}
		return result;
	}

	bool getBool(const string &name, bool required = true, bool defaultValue = false) const {
		bool result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result = *str == "true";
		}
		return result;
	}

	vector<string> getStrSet(const string &name, bool required = true,
		const vector<string> &defaultValue = vector<string>()) const
	{
		vector<string> result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result.clear();
			split(modp::b64_decode(*str), '\0', result);
		}
		return result;
	}

	Json::Value getJsonObject(const string &name, bool required = true,
		const Json::Value &defaultValue = Json::Value()) const
	{
		Json::Value result = defaultValue;
		const string *str;
		if (lookup(name, required, &str)) {
			result.clear();
			Json::Reader reader;
			if (!reader.parse(*str, result)) {
				throw RuntimeException("Cannot parse '" + name + "' key as JSON data: "
					+ reader.getFormattedErrorMessages());
			}
			if (!result.isObject()) {
				throw RuntimeException("'" + name + "' is valid JSON but is not an object");
			}
		}
		return result;
	}

	bool erase(const string &name) {
		return store.erase(name) != 0;
	}

	/** Checks whether the specified key is in this map. */
	bool has(const string &name) const {
		return store.find(name) != store.end();
	}

	/** Returns the number of elements in this map. */
	unsigned int size() const {
		return store.size();
	}

	void addTo(VariantMap &other) const {
		map<string, string>::const_iterator it;
		map<string, string>::const_iterator end = store.end();

		for (it = store.begin(); it != end; it++) {
			other.set(it->first, it->second);
		}
	}

	/**
	 * Writes a representation of the contents in this VariantMap to
	 * the given file descriptor with MessageIO. The data can be
	 * unserialized with <tt>readFrom(fd)</tt>.
	 *
	 * @throws SystemException
	 */
	void writeToFd(int fd, const StaticString &messageName = "VariantMap") const {
		map<string, string>::const_iterator it;
		map<string, string>::const_iterator end = store.end();
		vector<string> args;

		args.reserve(1 + 2 * store.size());
		args.push_back(messageName);
		for (it = store.begin(); it != end; it++) {
			args.push_back(it->first);
			args.push_back(it->second);
		}
		writeArrayMessage(fd, args);
	}

	Iterator begin() {
		return store.begin();
	}

	ConstIterator begin() const {
		return store.begin();
	}

	Iterator end() {
		return store.end();
	}

	ConstIterator end() const {
		return store.end();
	}

	string inspect() const {
		map<string, string>::const_iterator it;
		map<string, string>::const_iterator end = store.end();
		string result;
		unsigned int i = 0;

		result.append("{ ");
		for (it = store.begin(); it != end; it++, i++) {
			result.append("'");
			result.append(it->first);
			result.append("' => '");
			result.append(it->second);
			if (i == store.size() - 1) {
				result.append("'");
			} else {
				result.append("', ");
			}
		}
		result.append(" }");
		return result;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_VARIANT_MAP_H_ */
_PASSENGER_VARIANT_MAP_H_
