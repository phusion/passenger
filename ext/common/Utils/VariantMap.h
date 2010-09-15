/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#include <string>
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils/StrIntUtils.h"

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
		string getKey() const {
			return key;
		}
	};
	
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
			store[argv[i]] = argv[i + 1];
			i += 2;
		}
	}
	
	/**
	 * Populates a VariantMap from the data in <em>fd</em>. MessageChannel
	 * is used to read from the file descriptor.
	 *
	 * @throws SystemException
	 * @throws IOException
	 * @see <tt>readFrom(MessageChannel &)</tt>
	 */
	void readFrom(int fd) {
		MessageChannel channel(fd);
		readFrom(channel);
	}
	
	/**
	 * Populates a VariantMap from the data in <em>channel</em>. The first
	 * message in the channel must be a message as sent by writeToChannel().
	 *
	 * @throws SystemException
	 * @throws IOException
	 */
	void readFrom(MessageChannel &channel) {
		TRACE_POINT();
		vector<string> args;
		
		if (!channel.read(args)) {
			throw IOException("Unexpected end-of-file encountered");
		}
		if (args.size() == 0) {
			throw IOException("Unexpected empty message received from channel");
		}
		if (args[0] != "VariantMap") {
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
		store[name] = value;
		return *this;
	}
	
	VariantMap &setInt(const string &name, int value) {
		store[name] = toString(value);
		return *this;
	}
	
	VariantMap &setULL(const string &name, unsigned long long value) {
		store[name] = toString(value);
		return *this;
	}
	
	VariantMap &setPid(const string &name, pid_t value) {
		store[name] = toString((unsigned long long) value);
		return *this;
	}
	
	VariantMap &setUid(const string &name, uid_t value) {
		store[name] = toString((long long) value);
		return *this;
	}
	
	VariantMap &setGid(const string &name, gid_t value) {
		store[name] = toString((long long) value);
		return *this;
	}
	
	VariantMap &setBool(const string &name, bool value) {
		store[name] = value ? "true" : "false";
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
	
	void writeToFd(int fd) const {
		MessageChannel channel(fd);
		writeToChannel(channel);
	}
	
	/**
	 * Writes a representation of the contents in this VariantMap to
	 * the given channel. The data can be unserialized with
	 * <tt>readFrom(MessageChannel &)</tt>.
	 *
	 * @throws SystemException
	 */
	void writeToChannel(MessageChannel &channel) const {
		map<string, string>::const_iterator it;
		map<string, string>::const_iterator end = store.end();
		vector<string> args;
		
		args.reserve(1 + 2 * store.size());
		args.push_back("VariantMap");
		for (it = store.begin(); it != end; it++) {
			args.push_back(it->first);
			args.push_back(it->second);
		}
		channel.write(args);
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
