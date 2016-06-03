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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCHER_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCHER_H_

#include <boost/container/small_vector.hpp>
#include <boost/bind.hpp>
#include <oxt/thread.hpp>
#include <iomanip>
#include <utility>
#include <cassert>
#include <cstddef>

#include <ev.h>

#include <Logging.h>
#include <DataStructures/StringKeyTable.h>
#include <Algorithms/MovingAverage.h>
#include <Integrations/LibevJsonUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/JsonUtils.h>
#include <Utils/SystemTime.h>
#include <Utils/VariantMap.h>
#include <UstRouter/Transaction.h>
#include <UstRouter/RemoteSink/Common.h>
#include <UstRouter/RemoteSink/Segment.h>
#include <UstRouter/RemoteSink/Batch.h>
#include <UstRouter/RemoteSink/BatchingAlgorithm.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace std;
using namespace boost;
using namespace oxt;


class Batcher: public SegmentProcessor {
private:
	typedef StringKeyTable<SegmentPtr, SKT_EnableMoveSupport> SegmentsTable;

	struct BatchResult {
		Segment::BatchList batches;
		size_t totalBatchSize;
		MonotonicTimeUsec processingTime;
	};

	Context * const context;
	SegmentProcessor * const sender;
	ev_async processingDoneSignal;
	SegmentsTable segments;

	mutable boost::mutex syncher;
	size_t threshold, limit, peakSize;
	size_t bytesAccepted, bytesQueued, bytesProcessing, bytesForwarding, bytesForwarded, bytesDropped;
	unsigned int nAccepted, nQueued, nProcessing, nForwarding, nForwarded, nDropped;
	unsigned int nThreads;
	int compressionLevel;
	SegmentList segmentsToUnref;
	ev_tstamp lastQueueAddTime;
	MonotonicTimeUsec lastProcessingBeginTime;
	MonotonicTimeUsec lastProcessingEndTime;
	ev_tstamp lastDropTime;
	bool started;
	bool quit;
	bool terminated;


	struct ev_loop *getLoop() const {
		return context->loop;
	}

	void threadMain(Segment *segment) {
		char segmentNumberStr[32];
		snprintf(segmentNumberStr, sizeof(segmentNumberStr), "Segment %u", segment->number);
		TRACE_POINT_WITH_DATA(segmentNumberStr);

		try {
			waitForThreadInitializationSignal(segment);
			realThreadMain(segment);
		} catch (const thread_interrupted &) {
			// Do nothing
		} catch (const tracable_exception &e) {
			P_WARN("ERROR: " << e.what() << "\n  Backtrace:\n" << e.backtrace());
		}

		boost::unique_lock<boost::mutex> l(syncher);
		nThreads--;
		delete segment->processorThread;
		segment->processorThread = NULL;

		// Let event loop unreference the segment and possibly terminate
		// the Batcher.
		STAILQ_INSERT_TAIL(&segmentsToUnref, segment, nextScheduledForUnreferencing);
		ev_async_send(getLoop(), &processingDoneSignal);
	}

	void realThreadMain(Segment *segment) {
		TRACE_POINT();
		boost::unique_lock<boost::mutex> l(syncher);

		while (true) {
			UPDATE_TRACE_POINT();
			while (!quit && segment->nQueued == 0) {
				segment->processorCond.wait(l);
			}

			if (segment->nQueued > 0) {
				UPDATE_TRACE_POINT();
				TransactionList transactions = consumeQueues(segment);
				size_t bytesProcessing = segment->bytesProcessing;
				size_t nProcessing = segment->nProcessing;
				int compressionLevel = this->compressionLevel;
				l.unlock();

				UPDATE_TRACE_POINT();
				BatchResult batchResult;
				performBatching(segment, transactions, bytesProcessing, nProcessing,
					compressionLevel, batchResult);

				UPDATE_TRACE_POINT();
				l.lock();
				commitBatchResult(segment, batchResult);
			} else {
				break;
			}
		}
	}

	TransactionList consumeQueues(Segment *segment) {
		TRACE_POINT();
		TransactionList processing;

		STAILQ_INIT(&processing);

		assert(bytesQueued >= segment->bytesQueued);
		assert(nQueued >= segment->nQueued);
		P_ASSERT_EQ(segment->bytesProcessing, 0);
		P_ASSERT_EQ(segment->nProcessing, 0);

		STAILQ_SWAP(&segment->queued, &processing, Transaction);
		segment->bytesProcessing = segment->bytesQueued;
		segment->nProcessing = segment->nQueued;
		bytesQueued -= segment->bytesQueued;
		nQueued -= segment->nQueued;
		bytesProcessing += segment->bytesQueued;
		nProcessing += segment->nQueued;
		segment->bytesQueued = 0;
		segment->nQueued = 0;
		segment->lastProcessingBeginTime = lastProcessingBeginTime = SystemTime::getMonotonicUsec();

		return processing;
	}

	void performBatching(Segment *segment, TransactionList &transactions,
		size_t bytesProcessing, size_t nProcessing, int compressionLevel,
		BatchResult &result)
	{
		TRACE_POINT();
		TransactionList undersizedTransactions, oversizedTransactions;

		P_DEBUG("[RemoteSink batcher (segment " << segment->number
			<< ")] Compressing and creating batches for "
			<< nProcessing << " transactions ("
			<< (bytesProcessing / 1024) << " KB total)");

		STAILQ_INIT(&undersizedTransactions);
		STAILQ_INIT(&oversizedTransactions);

		BatchingAlgorithm::organizeTransactionsBySize(transactions,
			undersizedTransactions, oversizedTransactions, threshold);
		assert(STAILQ_EMPTY(&transactions));
		BatchingAlgorithm::organizeUndersizedTransactionsIntoBatches(
			undersizedTransactions, threshold);

		Segment::BatchList::const_iterator it;
		unsigned long long startTime, endTime;
		size_t totalBatchSize = 0;

		UPDATE_TRACE_POINT();
		startTime = SystemTime::getMonotonicUsec();
		BatchingAlgorithm::createBatchObjectsForUndersizedTransactions(
			undersizedTransactions, result.batches, compressionLevel);

		UPDATE_TRACE_POINT();
		BatchingAlgorithm::createBatchObjectsForOversizedTransactions(
			oversizedTransactions, result.batches, compressionLevel);
		endTime = SystemTime::getMonotonicUsec();

		UPDATE_TRACE_POINT();
		result.processingTime = endTime - startTime;
		result.totalBatchSize = countTotalCompressedSize(result.batches);
		P_DEBUG("[RemoteSink batcher (segment " << segment->number
			<< ")] Compressed " << (bytesProcessing / 1024) << " KB to "
			<< (result.totalBatchSize / 1024) << " KB in "
			<< std::fixed << std::setprecision(2) << ((endTime - startTime) / 1000000.0)
			<< " sec, created " << result.batches.size() << " batches totalling "
			<< (totalBatchSize / 1024) << " KB");
	}

	size_t countTotalCompressedSize(const Segment::BatchList &batches) const {
		Segment::BatchList::const_iterator it, end = batches.end();
		size_t result = 0;

		for (it = batches.begin(); it != end; it++) {
			result += it->getDataSize();
		}

		return result;
	}

	void commitBatchResult(Segment *segment, BatchResult &batchResult) {
		TRACE_POINT();
		segment->avgBatchingSpeed = expMovingAverage(segment->avgBatchingSpeed,
			(segment->bytesProcessing / 1024.0) / (batchResult.processingTime / 1000000.0),
			0.5);
		segment->avgCompressionFactor = expMovingAverage(segment->avgCompressionFactor,
			(double) batchResult.totalBatchSize / segment->bytesProcessing,
			0.5);
		segment->bytesForwarding += batchResult.totalBatchSize;
		segment->nForwarding += batchResult.batches.size();
		bytesProcessing -= segment->bytesProcessing;
		nProcessing -= segment->nProcessing;
		bytesForwarding += batchResult.totalBatchSize;
		nForwarding += batchResult.batches.size();
		segment->bytesProcessing = 0;
		segment->nProcessing = 0;
		segment->lastProcessingEndTime = lastProcessingEndTime = SystemTime::getMonotonicUsec();

		UPDATE_TRACE_POINT();
		Segment::BatchList::iterator it, end = batchResult.batches.end();
		for (it = batchResult.batches.begin(); it != end; it++) {
			Batch &batch = *it;
			segment->forwarding.push_back(boost::move(batch));
		}

		ev_async_send(getLoop(), &processingDoneSignal);
	}

	void dropQueue(Segment *segment) {
		Transaction *transaction, *nextTransaction;

		assert(bytesQueued >= segment->bytesQueued);
		assert(nQueued >= segment->nQueued);

		bytesQueued -= segment->bytesQueued;
		nQueued -= segment->nQueued;
		bytesDropped += segment->bytesQueued;
		nDropped += segment->nQueued;
		segment->bytesDroppedByBatcher += segment->bytesQueued;
		segment->nDroppedByBatcher += segment->nQueued;
		segment->bytesQueued = 0;
		segment->nQueued = 0;

		STAILQ_FOREACH_SAFE(transaction, &segment->queued, next, nextTransaction) {
			delete transaction;
		}
		STAILQ_INIT(&segment->queued);
	}

	static void onProcessingDone(EV_P_ ev_async *async, int revents) {
		Batcher *batcher = static_cast<Batcher *>(async->data);
		batcher->processingDone();
	}

	bool isTerminatable() const {
		return quit && nThreads == 0 && !terminated;
	}

	void terminate() {
		TRACE_POINT();
		SegmentsTable::ConstIterator it(segments);

		while (*it != NULL) {
			const SegmentPtr &segment = it.getValue();

			assert(STAILQ_EMPTY(&segment->queued));
			P_ASSERT_EQ(segment->processorThread, NULL);

			segment->forwarding.clear();

			it.next();
		}

		segments.clear();

		if (ev_is_active(&processingDoneSignal)) {
			ev_async_stop(getLoop(), &processingDoneSignal);
		}

		terminated = true;
	}

	size_t calculateSegmentListTotalIncomingTransactionsSize(const SegmentList &segments) const {
		const Segment *segment;
		size_t result = 0;

		STAILQ_FOREACH(segment, &segments, nextScheduledForBatching) {
			result += segment->bytesIncomingTransactions;
		}

		return result;
	}

	int getEffectiveCompressionLevel() const {
		if (compressionLevel == Z_DEFAULT_COMPRESSION) {
			return 6;
		} else {
			return compressionLevel;
		}
	}

	string getRecommendedBufferLimit() const {
		return toString(peakSize * 2 / 1024) + " KB";
	}

	size_t totalMemoryBuffered() const {
		return bytesQueued + bytesProcessing + bytesForwarding;
	}

	Json::Value inspectSegmentsAsJson(ev_tstamp evNow, MonotonicTimeUsec monoNow,
		unsigned long long now) const
	{
		Json::Value doc(Json::objectValue);
		SegmentsTable::ConstIterator it(segments);

		while (*it != NULL) {
			const Segment *segment = it.getValue().get();
			Json::Value subdoc;

			subdoc["thread_active"] = segment->processorThread != NULL;

			subdoc["incoming"] = byteSizeAndCountToJson(segment->bytesIncomingTransactions,
				segment->nIncomingTransactions);
			subdoc["queued"] = byteSizeAndCountToJson(segment->bytesQueued,
				segment->nQueued);
			subdoc["processing"] = byteSizeAndCountToJson(segment->bytesProcessing,
				segment->nProcessing);
			subdoc["forwarding"] = byteSizeAndCountToJson(segment->bytesForwarding,
				segment->nForwarding);
			subdoc["dropped"] = byteSizeAndCountToJson(segment->bytesDroppedByBatcher,
				segment->nDroppedByBatcher);

			subdoc["last_queue_add_time"] = evTimeToJson(segment->lastQueueAddTime,
				evNow, now);
			subdoc["last_processing_begin_time"] = monoTimeToJson(segment->lastProcessingBeginTime,
				monoNow, now);
			subdoc["last_processing_end_time"] = monoTimeToJson(segment->lastProcessingEndTime,
				monoNow, now);
			subdoc["last_drop_time"] = evTimeToJson(segment->lastDroppedByBatcherTime,
				evNow, now);

			subdoc["average_batching_speed"] = byteSpeedToJson(
				segment->avgBatchingSpeed / 1000000.0, -1, "second");
			if (segment->avgCompressionFactor == -1) {
				subdoc["average_compression_factor"] = Json::Value(Json::nullValue);
			} else {
				subdoc["average_compression_factor"] = segment->avgCompressionFactor;
			}

			doc[toString(segment->number)] = subdoc;
			it.next();
		}

		return doc;
	}

protected:
	// Virtual so that it can be mocked by unit tests.
	virtual void waitForThreadInitializationSignal(Segment *segment) {
		// Do nothing by default
	}

public:
	Batcher(Context *_context, SegmentProcessor *_sender, const VariantMap &options)
		: context(_context),
		  sender(_sender),
		  threshold(options.getULL("union_station_batcher_threshold")),
		  limit(options.getULL("union_station_batcher_memory_limit")),
		  peakSize(0),
		  bytesAccepted(0),
		  bytesQueued(0),
		  bytesProcessing(0),
		  bytesForwarding(0),
		  bytesForwarded(0),
		  bytesDropped(0),
		  nAccepted(0),
		  nQueued(0),
		  nProcessing(0),
		  nForwarding(0),
		  nForwarded(0),
		  nDropped(0),
		  nThreads(0),
		  compressionLevel(options.getULL("union_station_compression_level",
		      false, Z_DEFAULT_COMPRESSION)),
		  lastQueueAddTime(0),
		  lastProcessingBeginTime(0),
		  lastProcessingEndTime(0),
		  lastDropTime(0),
		  started(false),
		  quit(false),
		  terminated(false)
	{
		memset(&processingDoneSignal, 0, sizeof(processingDoneSignal));
		ev_async_init(&processingDoneSignal, onProcessingDone);
		processingDoneSignal.data = this;
		STAILQ_INIT(&segmentsToUnref);
	}

	~Batcher() {
		assert(!started || terminated);
	}

	void start() {
		started = true;
		ev_async_start(getLoop(), &processingDoneSignal);
	}

	bool shutdown(bool dropQueuedWork = false) {
		TRACE_POINT();
		boost::lock_guard<boost::mutex> l(syncher);
		quit = true;

		SegmentsTable::Iterator it(segments);
		while (*it != NULL) {
			Segment *segment = it.getValue().get();

			if (dropQueuedWork) {
				dropQueue(segment);
			}

			segment->processorCond.notify_one();
			it.next();
		}

		if (isTerminatable()) {
			terminate();
			return true;
		} else {
			return false;
		}
	}

	bool isTerminated() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return terminated;
	}

	void schedule(SegmentList &segments) {
		TRACE_POINT();
		Segment *segment;
		SegmentPtr *ourSegment;
		Transaction *transaction;
		char address[sizeof(Segment *)];
		bool droppedSome = false;
		boost::lock_guard<boost::mutex> l(syncher);
		assert(started);

		peakSize = std::max(peakSize, totalMemoryBuffered()
			+ calculateSegmentListTotalIncomingTransactionsSize(segments));

		STAILQ_FOREACH(segment, &segments, nextScheduledForBatching) {
			memcpy(address, &segment, sizeof(Segment *));
			HashedStaticString addressString(address, sizeof(address));

			// Add this segment to our segments hash table if we
			// don't already have it.
			if (this->segments.lookup(addressString, &ourSegment)) {
				P_ASSERT_EQ(segment, ourSegment->get());
			} else if (OXT_LIKELY(!quit)) {
				segment->ref(); // Reference by thread
				segment->processorThread = new oxt::thread(
					boost::bind(&Batcher::threadMain, this, segment),
					"RemoteSink batcher: segment " + toString(segment->number),
					1024 * 1024);
				this->segments.insertByMoving(addressString, SegmentPtr(segment));
				nThreads++;
			}

			STAILQ_REMOVE(&segments, segment, Segment, nextScheduledForBatching);

			// Move the transactions that we can accept within our limits
			// into the queue.
			while (!quit
				&& !STAILQ_EMPTY(&segment->incomingTransactions)
				&& totalMemoryBuffered() < limit)
			{
				transaction = STAILQ_FIRST(&segment->incomingTransactions);
				size_t bodySize = transaction->getBody().size();

				STAILQ_REMOVE_HEAD(&segment->incomingTransactions, next);
				STAILQ_INSERT_TAIL(&segment->queued, transaction, next);

				if (segment->nQueued == 0) {
					segment->processorCond.notify_one();
				}

				assert(segment->bytesIncomingTransactions >= bodySize);
				assert(segment->nIncomingTransactions > 0);
				segment->bytesIncomingTransactions -= bodySize;
				segment->nIncomingTransactions--;
				segment->bytesQueued += bodySize;
				segment->nQueued++;
				segment->lastQueueAddTime = lastQueueAddTime = ev_now(getLoop());

				bytesQueued += bodySize;
				nQueued++;

				bytesAccepted += bodySize;
				nAccepted++;
			}

			// Drop any transactions that we can't accept due to limits,
			// or that we can't accept because we're quitting.
			while (!STAILQ_EMPTY(&segment->incomingTransactions)) {
				transaction = STAILQ_FIRST(&segment->incomingTransactions);
				size_t bodySize = transaction->getBody().size();

				assert(segment->bytesIncomingTransactions >= bodySize);
				assert(segment->nIncomingTransactions > 0);
				segment->bytesIncomingTransactions -= bodySize;
				segment->nIncomingTransactions--;
				segment->bytesDroppedByBatcher += bodySize;
				segment->nDroppedByBatcher++;
				segment->lastDroppedByBatcherTime = lastDropTime = ev_now(getLoop());

				bytesDropped += bodySize;
				nDropped++;

				droppedSome = true;
				STAILQ_REMOVE_HEAD(&segment->incomingTransactions, next);
				delete transaction;
			}
		}

		STAILQ_INIT(&segments);

		if (droppedSome && !quit) {
			assert(bytesQueued + bytesProcessing > limit);
			if (getEffectiveCompressionLevel() > 3) {
				P_WARN("Unable to batch and compress Union Station data quickly enough. "
					"Please lower the compression level to speed up compression, or "
					"increase the batch buffer's limit (recommended limit: "
					+ getRecommendedBufferLimit() + ")");
			} else {
				P_WARN("Unable to batch and compress Union Station data quickly enough. "
					"The current compression level is " + toString(compressionLevel)
					+ ", which is already very fast. Please try increasing the batch "
					+ "buffer's limit (recommended limit: " + getRecommendedBufferLimit()
					+ ")");
			}
		}
	}

	void processingDone() {
		TRACE_POINT();
		SegmentsTable::Iterator it(segments);
		SegmentList segmentsToForward;
		Segment *segment;

		STAILQ_INIT(&segmentsToForward);

		boost::lock_guard<boost::mutex> l(syncher);
		assert(started);

		while (*it != NULL) {
			segment = it.getValue().get();
			if (!segment->forwarding.empty()) {
				bytesForwarding -= segment->bytesForwarding;
				nForwarding -= segment->nForwarding;
				bytesForwarded += segment->bytesForwarding;
				nForwarded += segment->nForwarding;
				segment->bytesForwarding = 0;
				segment->nForwarding = 0;
				segment->incomingBatches = boost::move(segment->forwarding);
				STAILQ_INSERT_TAIL(&segmentsToForward, segment, nextScheduledForSending);
			}
			it.next();
		}

		UPDATE_TRACE_POINT();
		if (!STAILQ_EMPTY(&segmentsToForward)) {
			sender->schedule(segmentsToForward);
			assert(STAILQ_EMPTY(&segmentsToForward));
		}

		UPDATE_TRACE_POINT();
		while (!STAILQ_EMPTY(&segmentsToUnref)) {
			segment = STAILQ_FIRST(&segmentsToUnref);
			STAILQ_REMOVE(&segmentsToUnref, segment, Segment,
				nextScheduledForUnreferencing);
			segment->unref();
		}
		STAILQ_INIT(&segmentsToUnref);

		UPDATE_TRACE_POINT();
		if (isTerminatable()) {
			terminate();
		}
	}

	void setThreshold(size_t newThreshold) {
		boost::lock_guard<boost::mutex> l(syncher);
		threshold = newThreshold;
	}

	void setLimit(size_t newLimit) {
		boost::lock_guard<boost::mutex> l(syncher);
		limit = newLimit;
	}

	void setCompressionLevel(int newLevel) {
		boost::lock_guard<boost::mutex> l(syncher);
		compressionLevel = newLevel;
	}

	Json::Value inspectStateAsJson() const {
		Json::Value doc;
		boost::lock_guard<boost::mutex> l(syncher);
		ev_tstamp evNow = ev_now(getLoop());
		MonotonicTimeUsec monoNow = SystemTime::getMonotonicUsecWithGranularity
			<SystemTime::GRAN_10MSEC>();
		unsigned long long now = SystemTime::getUsec();

		doc["total_memory"]["size"] = byteSizeToJson(totalMemoryBuffered());
		doc["total_memory"]["count"] = nQueued + nProcessing + nForwarding;
		doc["total_memory"]["peak_size"] = byteSizeToJson(peakSize);
		doc["total_memory"]["limit"] = byteSizeToJson(limit);

		doc["threshold"] = byteSizeToJson(threshold);
		doc["compression_level"] = getEffectiveCompressionLevel();
		doc["accepted"] = byteSizeAndCountToJson(bytesAccepted, nAccepted);
		doc["queued"] = byteSizeAndCountAndLastActivityEvTimeToJson(bytesQueued, nQueued,
			lastQueueAddTime, evNow, now);
		doc["processing"] = byteSizeAndCountToJson(bytesProcessing, nProcessing);
		doc["processing"]["last_begin_time"] = monoTimeToJson(lastProcessingBeginTime,
			monoNow, now);
		doc["processing"]["last_end_time"] = monoTimeToJson(lastProcessingEndTime,
			monoNow, now);
		doc["forwarding"] = byteSizeAndCountToJson(bytesForwarding, nForwarding);
		doc["forwarded"] = byteSizeAndCountToJson(bytesForwarded, nForwarded);
		doc["dropped"] = byteSizeAndCountAndLastActivityEvTimeToJson(bytesDropped,
			nDropped, lastDropTime, evNow, now);
		doc["segments"] = inspectSegmentsAsJson(evNow, monoNow, now);

		if (quit) {
			if (terminated) {
				doc["state"] = "TERMINATED";
			} else {
				doc["state"] = "SHUTTING_DOWN";
			}
		} else {
			doc["state"] = "ACTIVE";
		}

		return doc;
	}
};


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCHER_H_ */
