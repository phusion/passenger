/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_TRANSACTION_H_
#define _PASSENGER_UST_ROUTER_TRANSACTION_H_

#include <boost/shared_ptr.hpp>
#include <boost/move/move.hpp>
#include <boost/container/string.hpp>
#include <boost/cstdint.hpp>
#include <cstdlib>
#include <cstring>

#include <ev++.h>
#include <StaticString.h>
#include <MemoryKit/palloc.h>
#include <DataStructures/LString.h>
#include <Utils/JsonUtils.h>

namespace Passenger {
namespace UstRouter {

using namespace std;


class Transaction {
private:
	BOOST_MOVABLE_BUT_NOT_COPYABLE(Transaction);

	unsigned int groupNameOffset;
	unsigned int nodeNameOffset;
	unsigned int categoryOffset;
	unsigned int unionStationKeyOffset;
	unsigned int filtersOffset;

	unsigned short groupNameSize, nodeNameSize, filtersSize;
	boost::uint8_t txnIdSize, unionStationKeySize, categorySize;

	ev_tstamp createdAt;
	unsigned int writeCount;
	unsigned int refCount;
	unsigned int bodyOffset;
	bool crashProtect, discarded;

	boost::container::string storage;

	template<typename IntegerType1, typename IntegerType2>
	void internString(const StaticString &str, IntegerType1 *offset, IntegerType2 *size) {
		if (offset != NULL) {
			*offset = bodyOffset;
		}
		storage.append(str.data(), str.size());
		storage.append(1, '\0');
		*size = str.size();
		bodyOffset += str.size() + 1;
	}

public:
	Transaction(const StaticString &txnId, const StaticString &groupName,
		const StaticString &nodeName, const StaticString &category,
		const StaticString &unionStationKey, ev_tstamp _createdAt,
		const StaticString &filters = StaticString(),
		unsigned int initialCapacity = 1024 * 8)
		: createdAt(_createdAt),
		  writeCount(0),
		  refCount(0),
		  bodyOffset(0),
		  crashProtect(false),
		  discarded(false)
	{
		internString(txnId, (boost::uint8_t *) NULL, &txnIdSize);
		internString(groupName, &groupNameOffset, &groupNameSize);
		internString(nodeName, &nodeNameOffset, &nodeNameSize);
		internString(category, &categoryOffset, &categorySize);
		internString(unionStationKey, &unionStationKeyOffset, &unionStationKeySize);
		internString(filters, &filtersOffset, &filtersSize);
	}

	Transaction(BOOST_RV_REF(Transaction) other)
		: groupNameOffset(other.groupNameOffset),
		  nodeNameOffset(other.nodeNameOffset),
		  categoryOffset(other.categoryOffset),
		  unionStationKeyOffset(other.unionStationKeyOffset),
		  filtersOffset(other.filtersOffset),
		  groupNameSize(other.groupNameSize),
		  nodeNameSize(other.nodeNameSize),
		  filtersSize(other.filtersSize),
		  txnIdSize(other.txnIdSize),
		  unionStationKeySize(other.unionStationKeySize),
		  categorySize(other.categorySize),
		  createdAt(other.createdAt),
		  writeCount(other.writeCount),
		  refCount(other.refCount),
		  bodyOffset(other.bodyOffset),
		  crashProtect(other.crashProtect),
		  discarded(other.discarded),
		  storage(boost::move(other.storage))
	{
		other.groupNameOffset = 0;
		other.nodeNameOffset = 0;
		other.categoryOffset = 0;
		other.unionStationKeyOffset = 0;
		other.filtersOffset = 0;
		other.groupNameSize = 0;
		other.nodeNameSize = 0;
		other.filtersSize = 0;
		other.txnIdSize = 0;
		other.unionStationKeySize = 0;
		other.categorySize = 0;
		other.createdAt = 0;
		other.writeCount = 0;
		other.refCount = 0;
		other.bodyOffset = 0;
		other.crashProtect = false;
		other.discarded = true;
	}

	Transaction &operator=(BOOST_RV_REF(Transaction) other) {
		if (this != &other) {
			groupNameOffset = other.groupNameOffset;
			nodeNameOffset = other.nodeNameOffset;
			categoryOffset = other.categoryOffset;
			unionStationKeyOffset = other.unionStationKeyOffset;
			filtersOffset = other.filtersOffset;
			groupNameSize = other.groupNameSize;
			nodeNameSize = other.nodeNameSize;
			filtersSize = other.filtersSize;
			txnIdSize = other.txnIdSize;
			unionStationKeySize = other.unionStationKeySize;
			categorySize = other.categorySize;
			createdAt = other.createdAt;
			writeCount = other.writeCount;
			refCount = other.refCount;
			bodyOffset = other.bodyOffset;
			crashProtect = other.crashProtect;
			discarded = other.discarded;
			storage = boost::move(other.storage);

			other.groupNameOffset = 0;
			other.nodeNameOffset = 0;
			other.categoryOffset = 0;
			other.unionStationKeyOffset = 0;
			other.filtersOffset = 0;
			other.groupNameSize = 0;
			other.nodeNameSize = 0;
			other.filtersSize = 0;
			other.txnIdSize = 0;
			other.unionStationKeySize = 0;
			other.categorySize = 0;
			other.createdAt = 0;
			other.writeCount = 0;
			other.refCount = 0;
			other.bodyOffset = 0;
			other.crashProtect = false;
			other.discarded = true;
		}
		return *this;
	}

	StaticString getTxnId() const {
		return StaticString(storage.data(), txnIdSize);
	}

	StaticString getGroupName() const {
		return StaticString(storage.data() + groupNameOffset, groupNameSize);
	}

	StaticString getNodeName() const {
		return StaticString(storage.data() + nodeNameOffset, nodeNameSize);
	}

	StaticString getCategory() const {
		return StaticString(storage.data() + categoryOffset, categorySize);
	}

	StaticString getUnionStationKey() const {
		return StaticString(storage.data() + unionStationKeyOffset, unionStationKeySize);
	}

	StaticString getFilters() const {
		return StaticString(storage.data() + filtersOffset, filtersSize);
	}

	StaticString getBody() const {
		return StaticString(storage.data() + bodyOffset, storage.size() - bodyOffset);
	}

	bool crashProtectEnabled() const {
		return crashProtect;
	}

	void enableCrashProtect(bool v) {
		crashProtect = v;
	}

	bool isDiscarded() const {
		return discarded;
	}

	void discard() {
		discarded = true;
	}

	void ref() {
		refCount++;
	}

	void unref() {
		assert(refCount > 0);
		refCount--;
	}

	unsigned int getRefCount() const {
		return refCount;
	}

	void append(const StaticString &timestamp, const StaticString &data) {
		char txnIdCopy[txnIdSize];
		char writeCountStr[sizeof(unsigned int) * 2 + 1];
		unsigned int writeCountStrSize = integerToHexatri(
			writeCount, writeCountStr);

		memcpy(txnIdCopy, getTxnId().data(), getTxnId().size());
		writeCount++;

		storage.append(txnIdCopy, getTxnId().size());
		storage.append(1, ' ');
		storage.append(timestamp.data(), timestamp.size());
		storage.append(1, ' ');
		storage.append(writeCountStr, writeCountStrSize);
		storage.append(1, ' ');
		storage.append(data.data(), data.size());
		storage.append(1, '\n');
	}

	Json::Value inspectStateAsJson() const {
		Json::Value doc;
		doc["txn_id"] = getTxnId().toString();
		doc["created_at"] = timeToJson(createdAt * 1000000.0);
		doc["group"] = getGroupName().toString();
		doc["node"] = getNodeName().toString();
		doc["category"] = getCategory().toString();
		doc["key"] = getUnionStationKey().toString();
		doc["refcount"] = refCount;
		doc["body_size"] = byteSizeToJson(getBody().size());
		return doc;
	}
};

typedef boost::shared_ptr<Transaction> TransactionPtr;


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_TRANSACTION_H_ */
