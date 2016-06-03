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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_CORE_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_CORE_H_

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

#include <boost/thread.hpp>

#include <ev.h>

#include <cstddef>
#include <Utils/JsonUtils.h>
#include <Utils/VariantMap.h>
#include <UstRouter/Sink.h>
#include <UstRouter/Transaction.h>

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * ## Lock ordering
 *
 * 1. Core::syncher
 * 2. Batcher::getMutex()
 */
class Core: public Sink {
private:
	// These fields may only be accessed from the event loop.
	size_t threshold;
	size_t bytesQueued, bytesForwarded;
	unsigned int nQueued, nForwarded;
	TransactionList queued;


	// All fields past this point are protected by the syncher.
	boost::mutex syncher;

	Json::Value inspectIncomingStateAsJson() const {
		Json::Value doc;

		doc["threshold"] = byteSizeToJson(bytesIncoming);

		doc["queued_size"] = byteSizeToJson(bytesQueued);
		doc["queued_count"] = nQueued;

		doc["forwarded_size"] = byteSizeToJson(bytesForwarded);
		doc["forwarded_count"] = nForwarded;

		return doc;
	}

public:
	Core(const VariantMap &options)
		: threshold(options.getULL("union_station_incoming_threshold")),
		  bytesQueued(0),
		  bytesForwarded(0),
		  nQueued(0),
		  nForwarded(0),
		  batcher(sender, options)
	{
		STAILQ_INIT(&queued);
	}

	virtual void schedule(Transaction *transaction) {
		Sink::schedule(transaction);
		boost::unique_lock<boost::mutex> l(syncher);

		STAILQ_INSERT_TAIL(&queued, transaction, next);
		bytesQueued += transaction->getBody().size();
		nQueued++;
		if (bytesQueued > threshold) {
			l.unlock();
			flush();
		}
	}

	virtual void flush() {
		Sink::flush();
		ev_tstamp now = ev_now(loop);
		boost::lock_guard<boost::mutex> l(syncher);

		if (nQueued > 0) {
			Transaction *transaction;
			size_t bytesAdded;
			unsigned int nAdded;

			// If not all transactions can be added to Batcher, then
			// Batcher will log an appropriate message.
			batcher.add(&queued, bytesQueued, nQueued, bytesAdded, nAdded, now);

			bytesForwarded += bytesAdded;
			bytesDropped += bytesQueued - bytesAdded;
			nForwarded += nAdded;
			nDropped += nQueued - nAdded;

			STAILQ_FOREACH(transaction, &queued, next) {
				delete transaction;
			}
			STAILQ_INIT(&queued);

			bytesQueued = 0;
			nQueued = 0;
		}
	}

	virtual Json::Value inspectStateAsJson() const {
		Json::Value doc;
		boost::lock_guard<boost::mutex> l(syncher);

		doc["incoming"] = inspectIncomingStateAsJson();
		doc["batching"] = batcher.inspectStateAsJsonUnlocked();
		doc["sending"]  = sender.inspectStateAsJson();

/*
		doc["packets_generated"] = packetsGenerated.inspectAsJson();
		doc["packets_queued"] = packetsQueued.inspectAsJson();
		doc["packets_sending"] = packetsSending.inspectAsJson();
		doc["packets_accepted"] = packetsAccepted.inspectAsJson();
		doc["packets_rejected"] = packetsRejected.inspectAsJson();
		doc["packets_dropped"] = packetsDropped.inspectAsJson(); */
		/* if (certificate.empty()) {
			doc["certificate"] = Json::nullValue;
		} else {
			doc["certificate"] = certificate;
		}
		doc["up_servers"] = serverList->inspectUpServersStateAsJson();
		doc["down_servers"] = serverList->inspectDownServersStateAsJson();
		if (lastCheckupTime == 0) {
			doc["last_server_checkup_time"] = Json::Value(Json::nullValue);
			doc["last_server_checkup_time_note"] = "not yet started";
		} else {
			doc["last_server_checkup_time"] = timeToJson(lastCheckupTime * 1000000.0);
		}
		if (nextCheckupTime == 0) {
			doc["next_server_checkup_time"] = Json::Value(Json::nullValue);
			doc["next_server_checkup_time_note"] = "not yet scheduled, waiting for first packet";
		} else {
			doc["next_server_checkup_time"] = timeToJson(nextCheckupTime * 1000000.0);
		}
		if (!lastDnsErrorMessage.empty()) {
			doc["last_dns_error_message"] = lastDnsErrorMessage;
		} */
		return doc;
	}
};


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_CORE_H_ */
