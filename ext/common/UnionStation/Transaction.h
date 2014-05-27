/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
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
#ifndef _PASSENGER_UNION_STATION_TRANSACTION_H_
#define _PASSENGER_UNION_STATION_TRANSACTION_H_

#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <oxt/backtrace.hpp>

#include <string>
#include <stdexcept>

#include <cstdio>
#include <cassert>

#include <Logging.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <UnionStation/Connection.h>
#include <Utils/IOUtils.h>
#include <Utils/SystemTime.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {
namespace UnionStation {

using namespace std;
using namespace boost;


enum ExceptionHandlingMode {
	PRINT,
	THROW,
	IGNORE
};


class Core;
typedef boost::shared_ptr<Core> CorePtr;

inline void _checkinConnection(const CorePtr &core, const ConnectionPtr &connection);


class Transaction: public boost::noncopyable {
private:
	static const int INT64_STR_BUFSIZE = 22; // Long enough for a 64-bit number.
	static const unsigned long long IO_TIMEOUT = 5000000; // In microseconds.

	const CorePtr core;
	const ConnectionPtr connection;
	const string txnId;
	const string groupName;
	const string category;
	const string unionStationKey;
	const ExceptionHandlingMode exceptionHandlingMode;
	bool shouldFlushToDiskAfterClose;

	/**
	 * Buffer must be at least txnId.size() + 1 + INT64_STR_BUFSIZE + 1 bytes.
	 */
	char *insertTxnIdAndTimestamp(char *buffer, const char *end) {
		assert(end - buffer >= int(txnId.size() + 1 + INT64_STR_BUFSIZE + 1));
		int size;

		// "txn-id-here"
		buffer = appendData(buffer, end, txnId);

		// "txn-id-here "
		buffer = appendData(buffer, end, " ", 1);

		// "txn-id-here 123456"
		assert(end - buffer >= INT64_STR_BUFSIZE);
		size = snprintf(buffer, INT64_STR_BUFSIZE, "%llu", SystemTime::getUsec());
		if (size >= INT64_STR_BUFSIZE) {
			// The buffer is too small.
			throw IOException("Cannot format a new transaction log message timestamp.");
		}
		buffer += size;

		// "txn-id-here 123456 "
		buffer = appendData(buffer, end, " ", 1);

		return buffer;
	}

	template<typename ExceptionType>
	void handleException(const ExceptionType &e) {
		switch (exceptionHandlingMode) {
		case THROW:
			throw e;
		case PRINT: {
				const tracable_exception *te =
					dynamic_cast<const tracable_exception *>(&e);
				if (te != NULL) {
					P_WARN(te->what() << "\n" << te->backtrace());
				} else {
					P_WARN(e.what());
				}
				break;
			}
		default:
			break;
		}
	}

public:
	Transaction()
		: exceptionHandlingMode(PRINT)
		{ }

	Transaction(const CorePtr &_core,
		const ConnectionPtr &_connection,
		const string &_txnId,
		const string &_groupName,
		const string &_category,
		const string &_unionStationKey,
		ExceptionHandlingMode _exceptionHandlingMode = PRINT)
		: core(_core),
		  connection(_connection),
		  txnId(_txnId),
		  groupName(_groupName),
		  category(_category),
		  unionStationKey(_unionStationKey),
		  exceptionHandlingMode(_exceptionHandlingMode),
		  shouldFlushToDiskAfterClose(false)
		{ }

	~Transaction() {
		TRACE_POINT();
		if (connection == NULL) {
			return;
		}
		ConnectionLock l(connection);
		if (!connection->connected()) {
			return;
		}

		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(),
			timestamp);

		UPDATE_TRACE_POINT();
		ConnectionGuard guard(connection.get());
		try {
			unsigned long long timeout = IO_TIMEOUT;
			writeArrayMessage(connection->fd, &timeout,
				"closeTransaction",
				txnId.c_str(),
				timestamp,
				NULL);

			if (shouldFlushToDiskAfterClose) {
				UPDATE_TRACE_POINT();
				timeout = IO_TIMEOUT;
				writeArrayMessage(connection->fd, &timeout,
					"flush", NULL);
				readArrayMessage(connection->fd, &timeout);
			}

			_checkinConnection(core, connection);
			guard.clear();
		} catch (const SystemException &e) {
			string errorResponse;

			UPDATE_TRACE_POINT();
			guard.clear();
			if (connection->disconnect(errorResponse)) {
				handleException(IOException(
					"Logging agent disconnected with error: " +
					errorResponse));
			} else {
				handleException(e);
			}
		}
	}

	void message(const StaticString &text) {
		TRACE_POINT();
		if (connection == NULL) {
			P_TRACE(3, "[Union Station log to null] " << text);
			return;
		}
		ConnectionLock l(connection);
		if (!connection->connected()) {
			P_TRACE(3, "[Union Station log to null] " << text);
			return;
		}

		char timestamp[2 * sizeof(unsigned long long) + 1];
		integerToHexatri<unsigned long long>(SystemTime::getUsec(), timestamp);

		UPDATE_TRACE_POINT();
		ConnectionGuard guard(connection.get());
		try {
			unsigned long long timeout = IO_TIMEOUT;
			P_TRACE(3, "[Union Station log] " << txnId << " " << timestamp << " " << text);
			writeArrayMessage(connection->fd, &timeout,
				"log",
				txnId.c_str(),
				timestamp,
				NULL);
			writeScalarMessage(connection->fd, text, &timeout);
			guard.clear();
		} catch (const std::exception &e) {
			string errorResponse;

			UPDATE_TRACE_POINT();
			guard.clear();
			if (connection->disconnect(errorResponse)) {
				handleException(IOException(
					"Logging agent disconnected with error: " +
					errorResponse));
			} else {
				handleException(e);
			}
		}
	}

	void abort(const StaticString &text) {
		message("ABORT");
	}

	void flushToDiskAfterClose(bool value) {
		shouldFlushToDiskAfterClose = value;
	}

	bool isNull() const {
		return connection == NULL;
	}

	const string &getTxnId() const {
		return txnId;
	}

	const string &getGroupName() const {
		return groupName;
	}

	const string &getCategory() const {
		return category;
	}

	const string &getUnionStationKey() const {
		return unionStationKey;
	}
};

typedef boost::shared_ptr<Transaction> TransactionPtr;


} // namespace UnionStation
} // namespace Passenger

#endif /* _PASSENGER_UNION_STATION_TRANSACTION_H_ */
