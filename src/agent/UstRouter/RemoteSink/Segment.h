/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_SEGMENT_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_SEGMENT_H_

#include <boost/container/small_vector.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <cstddef>
#include <cassert>
#include <psg_sysqueue.h>
#include <ev.h>
#include <Utils/SystemTime.h>
#include <UstRouter/Transaction.h>
#include <UstRouter/RemoteSink/Batch.h>
#include <UstRouter/RemoteSink/Server.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace std;


struct Segment: private boost::noncopyable {
	typedef boost::container::small_vector<ServerPtr, 4> SmallServerList;
	typedef boost::container::small_vector<Batch, 16> BatchList;

	/****** General fields ******/

	mutable unsigned int refcount;
	const unsigned int number;


	/****** Fields used by Segmenter ******/

	STAILQ_ENTRY(Segment) nextInSegmenterList;
	string segmentKey;
	SmallServerList servers;
	bool scheduledForBatching;


	/****** Fields used by Segmenter and Batcher ******/

	// Linked list of all segments that a Batcher::schedule() call
	// should process.
	STAILQ_ENTRY(Segment) nextScheduledForBatching;

	// List of transactions, provided by the Segmenter, to
	// be batched by the Batcher.
	TransactionList incomingTransactions;
	size_t bytesIncomingTransactions;
	unsigned int nIncomingTransactions;


	/****** Fields used by Batcher ******/

	STAILQ_ENTRY(Segment) nextScheduledForUnreferencing;

	TransactionList queued;
	BatchList forwarding;
	size_t bytesQueued;
	size_t bytesProcessing;
	size_t bytesForwarding;
	size_t bytesDroppedByBatcher;
	unsigned int nQueued;
	unsigned int nProcessing;
	unsigned int nForwarding;
	unsigned int nDroppedByBatcher;
	ev_tstamp lastQueueAddTime;
	MonotonicTimeUsec lastProcessingBeginTime;
	MonotonicTimeUsec lastProcessingEndTime;
	ev_tstamp lastDroppedByBatcherTime;
	double avgBatchingSpeed, avgCompressionFactor;

	oxt::thread *processorThread;
	boost::condition_variable processorCond;


	/****** Fields used by Batcher and Sender ******/

	// Linked list of all segments that a Sender::schedule() call
	// should process.
	STAILQ_ENTRY(Segment) nextScheduledForSending;
	BatchList incomingBatches;


	/****** Fields used by Segmenter and Sender *******/

	SmallServerList balancingList;
	unsigned int nextBalancingIndex;


	/****** Fields used by Sender *******/

	size_t bytesAccepted;
	size_t bytesRejected;
	size_t bytesDroppedBySender;
	unsigned int nAccepted;
	unsigned int nRejected;
	unsigned int nDroppedBySender;
	ev_tstamp lastInitiateTime;
	ev_tstamp lastAcceptTime;
	ev_tstamp lastRejectTime;
	ev_tstamp lastDroppedBySenderTime;
	SmallServerList downServers;


	/****** Methods ******/

	Segment(unsigned int _number, const string &_segmentKey)
		: refcount(1),
		  number(_number),
		  segmentKey(_segmentKey),
		  scheduledForBatching(false),
		  bytesIncomingTransactions(0),
		  nIncomingTransactions(0),
		  bytesQueued(0),
		  bytesProcessing(0),
		  bytesForwarding(0),
		  bytesDroppedByBatcher(0),
		  nQueued(0),
		  nProcessing(0),
		  nForwarding(0),
		  nDroppedByBatcher(0),
		  lastQueueAddTime(0),
		  lastProcessingBeginTime(0),
		  lastProcessingEndTime(0),
		  lastDroppedByBatcherTime(0),
		  avgBatchingSpeed(-1),
		  avgCompressionFactor(-1),
		  processorThread(NULL),
		  nextBalancingIndex(0),
		  bytesAccepted(0),
		  bytesRejected(0),
		  bytesDroppedBySender(0),
		  nAccepted(0),
		  nRejected(0),
		  nDroppedBySender(0),
		  lastInitiateTime(0),
		  lastAcceptTime(0),
		  lastRejectTime(0),
		  lastDroppedBySenderTime(0)
	{
		STAILQ_NEXT(this, nextInSegmenterList) = NULL;
		STAILQ_NEXT(this, nextScheduledForBatching) = NULL;
		STAILQ_NEXT(this, nextScheduledForSending) = NULL;
		STAILQ_INIT(&incomingTransactions);
		STAILQ_INIT(&queued);
	}

	~Segment() {
		Transaction *transaction, *nextTransaction;

		STAILQ_FOREACH_SAFE(transaction, &incomingTransactions, next, nextTransaction) {
			delete transaction;
		}
		STAILQ_FOREACH_SAFE(transaction, &queued, next, nextTransaction) {
			delete transaction;
		}
	}

	void ref() const {
		refcount++;
	}

	void unref() const {
		assert(refcount > 0);
		refcount--;
		if (refcount == 0) {
			delete this;
		}
	}
};

STAILQ_HEAD(SegmentList, Segment);
typedef boost::intrusive_ptr<Segment> SegmentPtr;


inline void
intrusive_ptr_add_ref(const Segment *segment) {
	segment->ref();
}

inline void
intrusive_ptr_release(const Segment *segment) {
	segment->unref();
}


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_SEGMENT_H_ */
