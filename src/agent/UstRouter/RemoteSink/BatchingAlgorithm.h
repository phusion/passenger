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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCH_ALGORITHM_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCH_ALGORITHM_H_

#include <boost/container/small_vector.hpp>
#include <cassert>
#include <cstddef>

#include <UstRouter/RemoteSink/Batch.h>
#include <UstRouter/Transaction.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace boost;


/**
 * Low-level support algorithms for batching transactions. Used by the
 * Batcher class.
 */
class BatchingAlgorithm {
private:
	/**
	 * Frees all transaction objects in the given batch, returning
	 * the first transaction in the next batch.
	 */
	static Transaction *freeTransactionsInBatch(Transaction *firstTxnInBatch) {
		Transaction *transaction = firstTxnInBatch;
		while (true) {
			Transaction *nextInBatch = STAILQ_NEXT(transaction, nextInBatch);
			Transaction *firstTxnInNextBatch = STAILQ_NEXT(transaction, next);
			delete transaction;
			if (nextInBatch != NULL) {
				transaction = nextInBatch;
			} else {
				return firstTxnInNextBatch;
			}
		}
		return NULL; // Never reached, shut up compiler warning
	}

public:
	/**
	 * Organizes `transactions` into those whose body is < threshold
	 * (`undersizedTransactions`), and those whose body is >= threshold
	 * (`oversizedTransactions`).
	 *
	 * `transactions` will be cleared after this function returns.
	 *
	 * `undersizedTransactions` and `oversizedTransactions` must already be
	 * initialized. When this function returns, the transactions in these lists
	 * are linked to each other through the `next` pointer.
	 */
	static void organizeTransactionsBySize(TransactionList &transactions,
		TransactionList &undersizedTransactions,
		TransactionList &oversizedTransactions,
		size_t threshold)
	{
		Transaction *transaction = STAILQ_FIRST(&transactions);

		while (transaction != NULL) {
			Transaction *next = STAILQ_NEXT(transaction, next);

			if (transaction->getBody().size() >= threshold) {
				STAILQ_INSERT_TAIL(&oversizedTransactions, transaction, next);
				// We don't really need to set this (it defaults to NULL), but do
				// it anyway to signal intent.
				STAILQ_NEXT(transaction, nextInBatch) = NULL;
			} else {
				STAILQ_INSERT_TAIL(&undersizedTransactions, transaction, next);
			}

			transaction = next;
		}

		STAILQ_INIT(&transactions);
	}

	/**
	 * Given a list of undersized transactions (as produced by `organizeTransactionsBySize`),
	 * link some of them together to signal that they should be put in the same batch. This is
	 * done by updating the `nextInBatch` pointer. Transactions are linked with each other
	 * until their total body size >= threshold.
	 */
	static void organizeUndersizedTransactionsIntoBatches(TransactionList &transactions,
		size_t threshold)
	{
		Transaction *transaction;
		size_t currentBatchSize = 0;

		STAILQ_FOREACH(transaction, &transactions, next) {
			currentBatchSize += transaction->getBody().size();
			if (currentBatchSize < threshold) {
				STAILQ_NEXT(transaction, nextInBatch) = STAILQ_NEXT(transaction, next);
			} else {
				STAILQ_NEXT(transaction, nextInBatch) = NULL;
				currentBatchSize = 0;
			}
		}
	}

	/**
	 * Creates Batch objects for the a list of undersized transactions (as produced by
	 * `organizeTransactionsBySize` followed by `organizeUndersizedTransactionsIntoBatches`)
	 * and frees the transactions. Created Batch objects are appended into the `result` list.
	 * The `transaction` list is cleared.
	 */
	template<typename BatchList>
	static void createBatchObjectsForUndersizedTransactions(TransactionList &transactions,
		BatchList &result, int compressionLevel = Z_DEFAULT_COMPRESSION)
	{
		Transaction *firstTxnInBatch = STAILQ_FIRST(&transactions);

		while (firstTxnInBatch != NULL) {
			result.emplace_back(firstTxnInBatch, compressionLevel);
			Transaction *firstTxnInNextBatch = freeTransactionsInBatch(firstTxnInBatch);
			firstTxnInBatch = firstTxnInNextBatch;
		}

		STAILQ_INIT(&transactions);
	}

	/**
	 * Creates Batch objects for the given oversized transactions (as produced by
	 * `organizeTransactionsBySize`) and frees the transactions. Created Batch objects
	 * are appended into the `result` list. The `transaction` list is cleared.
	 */
	template<typename BatchList>
	static void createBatchObjectsForOversizedTransactions(TransactionList &transactions,
		BatchList &result, int compressionLevel = Z_DEFAULT_COMPRESSION)
	{
		Transaction *transaction = STAILQ_FIRST(&transactions);

		while (transaction != NULL) {
			P_ASSERT_EQ(STAILQ_NEXT(transaction, nextInBatch), NULL);
			result.emplace_back(transaction, compressionLevel);

			Transaction *current = transaction;
			transaction = STAILQ_NEXT(transaction, next);
			delete current;
		}

		STAILQ_INIT(&transactions);
	}
};


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCH_ALGORITHM_H_ */
