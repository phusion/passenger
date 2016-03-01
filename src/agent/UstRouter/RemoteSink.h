/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_H_

#include <string>
#include <cstring>
#include <ctime>
#include <ev++.h>
#include <Logging.h>
#include <UstRouter/LogSink.h>
#include <UstRouter/RemoteSender.h>

namespace Passenger {
namespace UstRouter {

using namespace std;
using namespace boost;

inline RemoteSender &Controller_getRemoteSender(Controller *controller);


class RemoteSink: public LogSink {
private:
	bool realFlush() {
		if (bufferSize > 0) {
			P_DEBUG("Flushing " << inspect() << ": " << bufferSize << " bytes");
			lastFlushed = ev_now(Controller_getLoop(controller));
			StaticString data(buffer, bufferSize);
			Controller_getRemoteSender(controller).schedule(unionStationKey,
				nodeName, category, &data, 1);
			bufferSize = 0;
			return true;
		} else {
			P_DEBUG("Flushing remote sink " << inspect() << ": 0 bytes");
			return false;
		}
	}

public:
	/* RemoteSender compresses the data with zlib before sending it
	 * to the server. Even including Base64 and URL encoding overhead,
	 * this compresses the data to about 25% of its original size.
	 * Therefore we set a buffer capacity of a little less than 4 times
	 * the TCP maximum segment size so that we can send as much
	 * data as possible to the server in a single TCP segment.
	 * With the "little less" we take into account:
	 * - HTTPS overhead. This can be as high as 2 KB.
	 * - The fact that RemoteSink.append() might try to flush the
	 *   current buffer. Observations have shown that the data
	 *   for a request transaction is often less than 5 KB.
	 */
	static const unsigned int BUFFER_CAPACITY =
		4 * 64 * 1024 -
		16 * 1024;

	string unionStationKey;
	string nodeName;
	string category;
	char buffer[BUFFER_CAPACITY];
	unsigned int bufferSize;

	RemoteSink(Controller *controller, const string &_unionStationKey,
		const string &_nodeName, const string &_category)
		: LogSink(controller),
		  unionStationKey(_unionStationKey),
		  nodeName(_nodeName),
		  category(_category),
		  bufferSize(0)
		{ }

	~RemoteSink() {
		// Calling non-virtual flush method
		realFlush();
	}

	virtual bool isRemote() const {
		return true;
	}

	virtual void append(const TransactionPtr &transaction) {
		StaticString data = transaction->getBody();
		LogSink::append(transaction);
		if (bufferSize + data.size() > BUFFER_CAPACITY) {
			StaticString data2[] = {
				StaticString(buffer, bufferSize),
				data
			};
			Controller_getRemoteSender(controller).schedule(unionStationKey,
				nodeName, category, data2, 2);
			lastFlushed = ev_now(Controller_getLoop(controller));
			bufferSize = 0;
		} else {
			memcpy(buffer + bufferSize, data.data(), data.size());
			bufferSize += data.size();
		}
	}

	virtual bool flush() {
		return realFlush();
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc = LogSink::inspectStateAsJson();
		doc["type"] = "remote";
		doc["key"] = unionStationKey;
		doc["node"] = nodeName;
		doc["category"] = category;
		doc["buffer_size"] = byteSizeToJson(bufferSize);
		return doc;
	}

	string inspect() const {
		return "RemoteSink(key=" + unionStationKey + ", node=" + nodeName + ", category=" + category + ")";
	}
};


} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_H_ */
