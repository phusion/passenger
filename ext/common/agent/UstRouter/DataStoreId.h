/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
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
#ifndef _PASSENGER_DATA_STORE_ID_H_
#define _PASSENGER_DATA_STORE_ID_H_

#include <cstring>
#include <climits>
#include <cassert>
#include <StaticString.h>

namespace Passenger {


/**
 * Efficient representation for a (groupName, nodeName, category) tuple.
 */
class DataStoreId {
private:
	char *id;
	unsigned short groupNameSize;
	unsigned short nodeNameSize;
	unsigned short categorySize;

	size_t totalSize() const {
		return groupNameSize + nodeNameSize + categorySize + 3;
	}

	StaticString toStaticString() const {
		if (id == NULL) {
			return StaticString();
		} else {
			return StaticString(id, totalSize());
		}
	}

public:
	DataStoreId(const StaticString &groupName, const StaticString &nodeName,
		const StaticString &category)
	{
		assert(groupName.size() <= USHRT_MAX);
		assert(nodeName.size() <= USHRT_MAX);
		assert(category.size() <= USHRT_MAX);

		char *end;

		id = new char[groupName.size() + nodeName.size() +
			category.size() + 3];
		end = id;

		memcpy(end, groupName.c_str(), groupName.size());
		groupNameSize = groupName.size();
		end += groupName.size();
		*end = '\0';
		end++;

		memcpy(end, nodeName.c_str(), nodeName.size());
		nodeNameSize = nodeName.size();
		end += nodeName.size();
		*end = '\0';
		end++;

		memcpy(end, category.c_str(), category.size());
		categorySize = category.size();
		end += category.size();
		*end = '\0';
	}

	DataStoreId() {
		id = NULL;
	}

	DataStoreId(const DataStoreId &other) {
		if (other.id == NULL) {
			id = NULL;
		} else {
			id = new char[other.totalSize()];
			memcpy(id, other.id, other.totalSize());
			groupNameSize = other.groupNameSize;
			nodeNameSize = other.nodeNameSize;
			categorySize = other.categorySize;
		}
	}

	~DataStoreId() {
		delete id;
	}

	DataStoreId &operator=(const DataStoreId &other) {
		if (other.id == NULL) {
			delete id;
			id = NULL;
			return *this;
		} else {
			if (totalSize() != other.totalSize()) {
				delete id;
				id = NULL;
			}
			if (id == NULL) {
				id = new char[other.totalSize()];
			}
			memcpy(id, other.id, other.totalSize());
			groupNameSize = other.groupNameSize;
			nodeNameSize = other.nodeNameSize;
			categorySize = other.categorySize;
			return *this;
		}
	}

	bool operator<(const DataStoreId &other) const {
		return toStaticString() < other.toStaticString();
	}

	bool operator==(const DataStoreId &other) const {
		if (id == NULL) {
			return other.id == NULL;
		} else {
			if (other.id == NULL) {
				return false;
			} else {
				return toStaticString() == other.toStaticString();
			}
		}
	}

	StaticString getGroupName() const {
		if (id != NULL) {
			return StaticString(id, groupNameSize);
		} else {
			return StaticString();
		}
	}

	StaticString getNodeName() const {
		if (id != NULL) {
			return StaticString(id + groupNameSize + 1,
				nodeNameSize);
		} else {
			return StaticString();
		}
	}

	StaticString getCategory() const {
		if (id != NULL) {
			return StaticString(id + groupNameSize + 1 + nodeNameSize + 1,
				categorySize);
		} else {
			return StaticString();
		}
	}
};


} // namespace Passenger

#endif /* _PASSENGER_DATA_STORE_ID_H_ */
