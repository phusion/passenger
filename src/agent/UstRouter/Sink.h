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
#ifndef _PASSENGER_UST_ROUTER_SINK_H_
#define _PASSENGER_UST_ROUTER_SINK_H_

#include <ev++.h>
#include <cassert>
#include <Utils/JsonUtils.h>
#include <UstRouter/Transaction.h>

namespace Passenger {
namespace UstRouter {


class Sink {
protected:
	struct ev_loop *loop;
	unsigned int transactionsScheduled;
	unsigned int flushCount;
	size_t bytesScheduled;
	ev_tstamp lastScheduleTime;
	ev_tstamp lastFlushTime;

public:
	Sink(struct ev_loop *_loop)
		: loop(_loop),
		  transactionsScheduled(0),
		  flushCount(0),
		  bytesScheduled(0),
		  lastScheduleTime(0),
		  lastFlushTime(0)
		{ }

	virtual ~Sink() { }

	/**
	 * Schedules a transaction for writing into this sink. This method
	 * takes over ownership of the transaction object.
	 */
	virtual void schedule(Transaction *transaction) {
		assert(transaction->isClosed());
		transactionsScheduled++;
		bytesScheduled += transaction->getBody().size();
		lastScheduleTime = ev_now(loop);
	}

	virtual void flush() {
		flushCount++;
		lastFlushTime = ev_now(loop);
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc, subdoc;

		subdoc["count"] = transactionsScheduled;
		subdoc["size"] = byteSizeToJson(bytesScheduled);
		subdoc["last_activity"] = timeToJson(lastScheduleTime * 1000000.0);
		doc["transactions_scheduled"] = subdoc;

		subdoc = Json::Value();
		subdoc["count"] = flushCount;
		subdoc["last_activity"] = timeToJson(lastFlushTime * 1000000.0);
		doc["flush"] = subdoc;

		return doc;
	}
};


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_SINK_H_ */
