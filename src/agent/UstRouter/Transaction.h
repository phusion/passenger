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

#include <boost/noncopyable.hpp>
#include <boost/move/move.hpp>
#include <boost/container/string.hpp>
#include <boost/cstdint.hpp>
#include <oxt/macros.hpp>
#include <ostream>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <string>

#include <psg_sysqueue.h>
#include <ev++.h>
#include <StaticString.h>
#include <MemoryKit/palloc.h>
#include <DataStructures/LString.h>
#include <Integrations/LibevJsonUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/JsonUtils.h>
#include <Utils/FastStringStream.h>

namespace Passenger {
namespace UstRouter {

using namespace std;


class Transaction {
private:
	BOOST_MOVABLE_BUT_NOT_COPYABLE(Transaction);

	size_t bodySize;
	unsigned short nodeNameSize, filtersSize;
	boost::uint8_t txnIdSize, unionStationKeySize, categorySize;

	ev_tstamp createdAt, closedAt;
	unsigned int refCount;
	bool crashProtect: 1;
	bool discarded: 1;
	bool endOfBatch: 1;

	boost::container::string storage;

	template<typename IntegerType>
	void internString(const StaticString &str,  IntegerType *size) {
		storage.append(str.data(), str.size());
		storage.append(1, '\0');
		*size = str.size();
	}

	void appendStaticString(const StaticString &data) {
		storage.append(data.data(), data.size());
	}

public:
	STAILQ_ENTRY(Transaction) next, nextInBatch;

	Transaction()
		: bodySize(0),
		  nodeNameSize(0),
		  filtersSize(0),
		  txnIdSize(0),
		  unionStationKeySize(0),
		  categorySize(0),
		  createdAt(0),
		  closedAt(0),
		  refCount(0),
		  crashProtect(false),
		  discarded(true),
		  endOfBatch(false)
		{ }

	Transaction(const StaticString &txnId,
		const StaticString &nodeName, const StaticString &category,
		const StaticString &unionStationKey, ev_tstamp _createdAt,
		const StaticString &filters = StaticString(),
		unsigned int initialCapacity = 1024 * 8)
		: bodySize(0),
		  createdAt(_createdAt),
		  closedAt(0),
		  refCount(0),
		  crashProtect(false),
		  discarded(false),
		  endOfBatch(false)
	{
		internString(txnId, &txnIdSize);
		internString(nodeName, &nodeNameSize);
		internString(category, &categorySize);
		internString(unionStationKey, &unionStationKeySize);
		internString(filters, &filtersSize);
		STAILQ_NEXT(this, next) = NULL;
		STAILQ_NEXT(this, nextInBatch) = NULL;
	}

	Transaction(BOOST_RV_REF(Transaction) other)
		: bodySize(other.bodySize),
		  nodeNameSize(other.nodeNameSize),
		  filtersSize(other.filtersSize),
		  txnIdSize(other.txnIdSize),
		  unionStationKeySize(other.unionStationKeySize),
		  categorySize(other.categorySize),
		  createdAt(other.createdAt),
		  closedAt(other.closedAt),
		  refCount(other.refCount),
		  crashProtect(other.crashProtect),
		  discarded(other.discarded),
		  endOfBatch(other.endOfBatch),
		  storage(boost::move(other.storage))
	{
		other.bodySize = 0;
		other.nodeNameSize = 0;
		other.filtersSize = 0;
		other.txnIdSize = 0;
		other.unionStationKeySize = 0;
		other.categorySize = 0;
		other.createdAt = 0;
		other.closedAt = 0;
		other.refCount = 0;
		other.crashProtect = false;
		other.discarded = true;
		other.endOfBatch = false;
	}

	Transaction &operator=(BOOST_RV_REF(Transaction) other) {
		if (this != &other) {
			bodySize = other.bodySize;
			nodeNameSize = other.nodeNameSize;
			filtersSize = other.filtersSize;
			txnIdSize = other.txnIdSize;
			unionStationKeySize = other.unionStationKeySize;
			categorySize = other.categorySize;
			createdAt = other.createdAt;
			closedAt = other.closedAt;
			refCount = other.refCount;
			crashProtect = other.crashProtect;
			discarded = other.discarded;
			endOfBatch = other.endOfBatch;
			storage = boost::move(other.storage);

			other.bodySize = 0;
			other.nodeNameSize = 0;
			other.filtersSize = 0;
			other.txnIdSize = 0;
			other.unionStationKeySize = 0;
			other.categorySize = 0;
			other.createdAt = 0;
			other.closedAt = 0;
			other.refCount = 0;
			other.crashProtect = false;
			other.discarded = true;
			other.endOfBatch = false;
		}
		return *this;
	}

	StaticString getTxnId() const {
		if (storage.empty()) {
			return StaticString();
		} else {
			return StaticString(storage.data(), txnIdSize);
		}
	}

	StaticString getNodeName() const {
		if (storage.empty()) {
			return StaticString();
		} else {
			return StaticString(storage.data()
				+ txnIdSize + 1,
				nodeNameSize);
		}
	}

	StaticString getCategory() const {
		if (storage.empty()) {
			return StaticString();
		} else {
			return StaticString(storage.data()
				+ txnIdSize + 1
				+ nodeNameSize + 1,
				categorySize);
		}
	}

	StaticString getUnionStationKey() const {
		if (storage.empty()) {
			return StaticString();
		} else {
			return StaticString(storage.data()
				+ txnIdSize + 1
				+ nodeNameSize + 1
				+ categorySize + 1,
				unionStationKeySize);
		}
	}

	StaticString getFilters() const {
		if (storage.empty()) {
			return StaticString();
		} else {
			return StaticString(storage.data()
				+ txnIdSize + 1
				+ nodeNameSize + 1
				+ categorySize + 1
				+ unionStationKeySize + 1,
				filtersSize);
		}
	}

	StaticString getBody() const {
		if (storage.empty()) {
			return StaticString();
		} else {
			return StaticString(storage.data()
				+ txnIdSize + 1
				+ nodeNameSize + 1
				+ categorySize + 1
				+ unionStationKeySize + 1
				+ filtersSize + 1,
				bodySize);
		}
	}

	StaticString getBatchArchiveMetadata() const {
		if (storage.empty()) {
			return StaticString();
		} else {
			const char *p = getBody().data() + bodySize;
			return StaticString(p, storage.data() + storage.size() - p);
		}
	}

	void createBatchArchiveMetadata() {
		if (!getBatchArchiveMetadata().empty()) {
			return;
		}

		string tmp;

		appendStaticString(P_STATIC_STRING("{\"txn_id\": \""));
		cEscapeString(getTxnId(), tmp);
		storage.append(tmp.data(), tmp.size());

		appendStaticString(P_STATIC_STRING("\", \"node_name\": \""));
		tmp.clear();
		cEscapeString(getNodeName(), tmp);
		storage.append(tmp.data(), tmp.size());

		appendStaticString(P_STATIC_STRING("\", \"category\": \""));
		tmp.clear();
		cEscapeString(getCategory(), tmp);
		storage.append(tmp.data(), tmp.size());

		appendStaticString(P_STATIC_STRING("\", \"key\": \""));
		tmp.clear();
		cEscapeString(getUnionStationKey(), tmp);
		storage.append(tmp.data(), tmp.size());

		appendStaticString(P_STATIC_STRING("\"}\n"));
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

	bool isEndOfBatch() const {
		return endOfBatch;
	}

	void setEndOfBatch(bool v) {
		endOfBatch = v;
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

	ev_tstamp getLifeTime() const {
		if (OXT_UNLIKELY(closedAt == 0)) {
			return 0;
		} else {
			return closedAt - createdAt;
		}
	}

	void append(const StaticString &data) {
		storage.append(data.data(), data.size());
		storage.append(1, '\n');
		bodySize += data.size() + 1;
	}

	void close(ev_tstamp now) {
		closedAt = now;
	}

	bool isClosed() const {
		return closedAt != 0;
	}

	Json::Value inspectStateAsJson(ev_tstamp evNow, unsigned long long now) const {
		Json::Value doc;
		doc["txn_id"] = getTxnId().toString();
		doc["created_at"] = evTimeToJson(createdAt, evNow, now);
		if (closedAt != 0) {
			doc["closed_at"] = evTimeToJson(closedAt, evNow, now);
		}
		doc["node"] = getNodeName().toString();
		doc["category"] = getCategory().toString();
		doc["key"] = getUnionStationKey().toString();
		doc["refcount"] = refCount;
		doc["body_size"] = byteSizeToJson(getBody().size());
		return doc;
	}

	string inspect() const {
		FastStringStream<> stream;
		inspect(stream);
		return string(stream.data(), stream.size());
	}

	void inspect(ostream &stream) const {
		stream << "txnId=" << getTxnId() <<
			", category=" << getCategory() <<
			", key=" << getUnionStationKey();
	}
};


STAILQ_HEAD(TransactionList, Transaction);


inline ostream &
operator<<(ostream &os, const Transaction &transaction) {
	transaction.inspect(os);
	return os;
}

inline size_t
TransactionList_countTotalBodySize(TransactionList &transactions) {
	Transaction *transaction;
	size_t result = 0;

	STAILQ_FOREACH(transaction, &transactions, next) {
		result += transaction->getBody().size();
	}

	return result;
}


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_TRANSACTION_H_ */
