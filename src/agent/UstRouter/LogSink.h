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
#ifndef _PASSENGER_UST_ROUTER_LOG_SINK_H_
#define _PASSENGER_UST_ROUTER_LOG_SINK_H_

#include <boost/shared_ptr.hpp>
#include <cstddef>
#include <cassert>
#include <ev++.h>
#include <jsoncpp/json.h>
#include <Utils/JsonUtils.h>

namespace Passenger {
namespace UstRouter {

using namespace std;
using namespace boost;


class Controller;
inline struct ev_loop *Controller_getLoop(Controller *controller);


class LogSink {
public:
	Controller *controller;

	/**
	 * Marks how many times this LogSink is currently opened, i.e. the
	 * number of Transaction objects currently referencing this LogSink.
	 * Only when this value is 0 is this LogSink eligible for garbage
	 * collection.
	 */
	int opened;

	/**
	 * Last time append() was called. This may be 0, meaning that
	 * append() has never been called before.
	 */
	ev_tstamp lastWrittenTo;

	/**
	 * Last time the reference count on this log sink was decremented.
	 * A value of 0 means that this LogSink is new and the reference
	 * count has never been decremented before. Such LogSinks are not
	 * eligible for garbage collection.
	 */
	ev_tstamp lastClosed;

	/**
	 * Last time data was actually written to the underlying storage device.
	 * This may be 0, meaning that the data has never been flushed before.
	 */
	ev_tstamp lastFlushed;

	/**
	 * The amount of data that has been written to this sink so far.
	 */
	size_t totalBytesWritten;

	LogSink(Controller *_controller)
		: controller(_controller),
		  opened(0),
		  lastWrittenTo(0),
		  lastClosed(0),
		  lastFlushed(0),
		  totalBytesWritten(0)
		{ }

	virtual ~LogSink() {
		// Subclasses should flush any data in their destructors.
		// We cannot call flush() here because it is not allowed
		// to call virtual functions in a destructor.
	}

	virtual bool isRemote() const {
		return false;
	}

	virtual void append(const TransactionPtr &transaction) {
		assert(!transaction->isDiscarded());
		lastWrittenTo = ev_now(Controller_getLoop(controller));
		totalBytesWritten += transaction->getBody().size();
	}

	virtual bool flush() {
		lastFlushed = ev_now(Controller_getLoop(controller));
		return true;
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc;
		doc["opened"] = opened;
		if (lastWrittenTo == 0) {
			doc["last_written_to"] = Json::Value(Json::nullValue);
		} else {
			doc["last_written_to"] = timeToJson(lastWrittenTo * 1000000.0);
		}
		if (lastClosed == 0) {
			doc["last_closed"] = Json::Value(Json::nullValue);
		} else {
			doc["last_closed"] = timeToJson(lastClosed * 1000000.0);
		}
		if (lastFlushed == 0) {
			doc["last_flushed"] = Json::Value(Json::nullValue);
		} else {
			doc["last_flushed"] = timeToJson(lastFlushed * 1000000.0);
		}
		doc["total_bytes_written"] = byteSizeToJson(totalBytesWritten);
		return doc;
	}

	virtual string inspect() const = 0;
};

typedef boost::shared_ptr<LogSink> LogSinkPtr;


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_LOG_SINK_H_ */
