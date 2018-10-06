/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_DATA_STRUCTURES_STRING_MAP_H_
#define _PASSENGER_DATA_STRUCTURES_STRING_MAP_H_

#include <string>
#include <map>
#include <utility>

#include <StaticString.h>
#include <DataStructures/HashMap.h>

namespace Passenger {

using namespace std;


/**
 * An efficient map with string keys. map<string, T> forces one to construct an
 * std::string object when looking up the map. StringMap interns all keys and
 * allows lookups without constructing an std::string key.
 *
 * StringMap requires the following properties on T:
 * - T's default constructor must be cheap, otherwise set() can be a bit slow.
 * - T must support operator=().
 */
template<typename T>
class StringMap {
private:
	struct Entry {
		string key;
		pair<StaticString, T> thePair;
	};

	typedef HashMap<StaticString, Entry, StaticString::Hash> InternalMap;
	typedef typename InternalMap::iterator InternalIterator;
	typedef typename InternalMap::const_iterator InternalConstIterator;
	typedef typename InternalMap::value_type ValueType;
	InternalMap store;

public:
	class const_iterator {
	private:
		InternalConstIterator it;

	public:
		const_iterator() { }

		const_iterator(const InternalConstIterator &_it)
			: it(_it)
			{ }

		const_iterator &operator=(const const_iterator &value) {
			it = value.it;
			return *this;
		}

		const_iterator &operator++() {
			it++;
			return *this;
		}

		const_iterator operator++(int) {
			const_iterator copy(*this);
			operator++();
			return copy;
		}

		bool operator==(const const_iterator &other) {
			return it == other.it;
		}

		bool operator!=(const const_iterator &other) {
			return it != other.it;
		}

		const pair<const StaticString, const T> &operator*() {
			return (pair<const StaticString, const T> &) it->second.thePair;
		}

		const pair<const StaticString, const T> *operator->() {
			return &(**this);
		}
	};

	class iterator {
	private:
		InternalIterator it;

	public:
		iterator() { }

		iterator(const InternalIterator &_it)
			: it(_it)
			{ }

		iterator &operator=(const iterator &value) {
			it = value.it;
			return *this;
		}

		iterator &operator++() {
			it++;
			return *this;
		}

		iterator operator++(int) {
			iterator copy(*this);
			operator++();
			return copy;
		}

		bool operator==(const iterator &other) {
			return it == other.it;
		}

		bool operator!=(const iterator &other) {
			return it != other.it;
		}

		pair<StaticString, T> &operator*() {
			return it->second.thePair;
		}

		pair<StaticString, T> *operator->() {
			return &(**this);
		}

		operator const_iterator() const {
			return const_iterator(it);
		}
	};

	T get(const StaticString &key) const {
		InternalConstIterator it = store.find(key);
		if (it == store.end()) {
			return T();
		} else {
			return it->second.thePair.second;
		}
	}

	T get(const StaticString &key, const T &defaultValue) const {
		InternalConstIterator it = store.find(key);
		if (it == store.end()) {
			return defaultValue;
		} else {
			return it->second.thePair.second;
		}
	}

	bool has(const StaticString &key) const {
		return store.find(key) != store.end();
	}

	bool set(const StaticString &key, const T &value) {
		pair<InternalIterator, bool> result = store.insert(make_pair(key, Entry()));
		if (result.second) {
			// Key has been inserted. Copy it internally and point key
			// to the copy.
			ValueType &node = *result.first;
			StaticString &originalKey = const_cast<StaticString &>(node.first);
			Entry &entry = node.second;
			entry.key = key;
			entry.thePair.first = entry.key;
			entry.thePair.second = value;
			originalKey = entry.key;
			return true;
		} else {
			// Key already exists. Update value.
			Entry &entry = result.first->second;
			entry.thePair.second = value;
			return false;
		}
	}

	bool remove(const StaticString &key) {
		return store.erase(key) > 0;
	}

	unsigned int size() const {
		return store.size();
	}

	bool empty() const {
		return store.empty();
	}

	iterator begin() {
		return iterator(store.begin());
	}

	const_iterator begin() const {
		return const_iterator(store.begin());
	}

	iterator end() {
		return iterator(store.end());
	}

	const_iterator end() const {
		return const_iterator(store.end());
	}
};


} // namespace Passenger

#endif /* _PASSENGER_DATA_STRUCTURES_STRING_MAP_H_ */
