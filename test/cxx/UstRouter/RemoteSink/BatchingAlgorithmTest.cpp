#include "TestSupport.h"
#include <zlib.h>
#include <boost/container/vector.hpp>
#include <UstRouter/RemoteSink/BatchingAlgorithm.h>

using namespace Passenger;
using namespace Passenger::UstRouter;
using namespace Passenger::UstRouter::RemoteSink;
using namespace std;

namespace tut {
	struct UstRouter_RemoteSink_BatchingAlgorithmTest {
		StaticString smallBody, mediumBody, largeBody;

		// 4 bytes
		Transaction *smallTxn, *smallTxn2, *smallTxn3;
		// 6 bytes
		Transaction *mediumTxn, *mediumTxn2, *mediumTxn3;
		// 8 bytes
		Transaction *largeTxn, *largeTxn2, *largeTxn3;

		UstRouter_RemoteSink_BatchingAlgorithmTest() {
			smallBody = P_STATIC_STRING("234");
			mediumBody = P_STATIC_STRING("23456");
			largeBody = P_STATIC_STRING("2345678");

			smallTxn = new Transaction("txnId1", "nodeName1", "category1",
				"unionStationKey1", 1, "filters1");
			smallTxn->append(smallBody);

			smallTxn2 = new Transaction("txnId2", "nodeName2", "category2",
				"unionStationKey2", 2, "filters2");
			smallTxn2->append(smallBody);

			smallTxn3 = new Transaction("txnId3", "nodeName3", "category3",
				"unionStationKey3", 3, "filters3");
			smallTxn3->append(smallBody);

			mediumTxn = new Transaction("txnId1", "nodeName1", "category1",
				"unionStationKey1", 1, "filters1");
			mediumTxn->append(mediumBody);

			mediumTxn2 = new Transaction("txnId2", "nodeName2", "category2",
				"unionStationKey2", 2, "filters2");
			mediumTxn2->append(mediumBody);

			mediumTxn3 = new Transaction("txnId3", "nodeName3", "category3",
				"unionStationKey3", 3, "filters3");
			mediumTxn3->append(mediumBody);

			largeTxn = new Transaction("txnId1", "nodeName1", "category1",
				"unionStationKey1", 1, "filters1");
			largeTxn->append(largeBody);

			largeTxn2 = new Transaction("txnId2", "nodeName2", "category2",
				"unionStationKey2", 2, "filters2");
			largeTxn2->append(largeBody);

			largeTxn3 = new Transaction("txnId3", "nodeName3", "category3",
				"unionStationKey3", 3, "filters3");
			largeTxn3->append(largeBody);
		}

		~UstRouter_RemoteSink_BatchingAlgorithmTest() {
			delete smallTxn;
			delete smallTxn2;
			delete smallTxn3;
			delete mediumTxn;
			delete mediumTxn2;
			delete mediumTxn3;
			delete largeTxn;
			delete largeTxn2;
			delete largeTxn3;
		}
	};

	DEFINE_TEST_GROUP(UstRouter_RemoteSink_BatchingAlgorithmTest);

	/***** Test organizeTransactionsBySize() *****/

	TEST_METHOD(1) {
		set_test_name("organizeTransactionsBySize");
		TransactionList transactions, undersized, oversized;
		Transaction *txn;

		STAILQ_INIT(&transactions);
		STAILQ_INIT(&undersized);
		STAILQ_INIT(&oversized);

		STAILQ_INSERT_TAIL(&transactions, smallTxn, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, smallTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, largeTxn, next);
		STAILQ_INSERT_TAIL(&transactions, smallTxn3, next);
		STAILQ_INSERT_TAIL(&transactions, largeTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, largeTxn3, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn3, next);

		BatchingAlgorithm::organizeTransactionsBySize(transactions,
			undersized, oversized, mediumBody.size());
		ensure("(1)", STAILQ_EMPTY(&transactions));

		txn = STAILQ_FIRST(&undersized);
		ensure_equals("(2)", txn, smallTxn);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(3)", txn, smallTxn2);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(4)", txn, smallTxn3);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(5)", txn, (Transaction *) NULL);

		txn = STAILQ_FIRST(&oversized);
		ensure_equals("(10)", txn, mediumTxn);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(11)", txn, mediumTxn2);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(12)", txn, largeTxn);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(13)", txn, largeTxn2);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(14)", txn, largeTxn3);

		txn = STAILQ_NEXT(txn, next);
		ensure_equals("(15)", txn, mediumTxn3);
	}


	/***** Test organizeUndersizedTransactionsIntoBatches() *****/

	TEST_METHOD(2) {
		set_test_name("organizeUndersizedTransactionsIntoBatches");
		TransactionList transactions;

		STAILQ_INIT(&transactions);
		STAILQ_INSERT_TAIL(&transactions, smallTxn, next);
		STAILQ_INSERT_TAIL(&transactions, smallTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, smallTxn3, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn3, next);

		BatchingAlgorithm::organizeUndersizedTransactionsIntoBatches(
			transactions, 6);
		ensure_equals("(1)", STAILQ_NEXT(smallTxn, nextInBatch), smallTxn2);
		ensure_equals("(2)", STAILQ_NEXT(smallTxn2, nextInBatch), (Transaction *) NULL);
		ensure_equals("(3)", STAILQ_NEXT(smallTxn3, nextInBatch), mediumTxn);
		ensure_equals("(4)", STAILQ_NEXT(mediumTxn, nextInBatch), (Transaction *) NULL);
		ensure_equals("(5)", STAILQ_NEXT(mediumTxn2, nextInBatch), (Transaction *) NULL);
		ensure_equals("(6)", STAILQ_NEXT(mediumTxn3, nextInBatch), (Transaction *) NULL);

		BatchingAlgorithm::organizeUndersizedTransactionsIntoBatches(
			transactions, 9);
		ensure_equals("(11)", STAILQ_NEXT(smallTxn, nextInBatch), smallTxn2);
		ensure_equals("(12)", STAILQ_NEXT(smallTxn2, nextInBatch), smallTxn3);
		ensure_equals("(13)", STAILQ_NEXT(smallTxn3, nextInBatch), (Transaction *) NULL);
		ensure_equals("(14)", STAILQ_NEXT(mediumTxn, nextInBatch), mediumTxn2);
		ensure_equals("(15)", STAILQ_NEXT(mediumTxn2, nextInBatch), (Transaction *) NULL);
		ensure_equals("(16)", STAILQ_NEXT(mediumTxn3, nextInBatch), (Transaction *) NULL);

		BatchingAlgorithm::organizeUndersizedTransactionsIntoBatches(
			transactions, 10);
		ensure_equals("(21)", STAILQ_NEXT(smallTxn, nextInBatch), smallTxn2);
		ensure_equals("(22)", STAILQ_NEXT(smallTxn2, nextInBatch), smallTxn3);
		ensure_equals("(23)", STAILQ_NEXT(smallTxn3, nextInBatch), (Transaction *) NULL);
		ensure_equals("(24)", STAILQ_NEXT(mediumTxn, nextInBatch), mediumTxn2);
		ensure_equals("(25)", STAILQ_NEXT(mediumTxn2, nextInBatch), (Transaction *) NULL);
		ensure_equals("(26)", STAILQ_NEXT(mediumTxn3, nextInBatch), (Transaction *) NULL);

		BatchingAlgorithm::organizeUndersizedTransactionsIntoBatches(
			transactions, 15);
		ensure_equals("(31)", STAILQ_NEXT(smallTxn, nextInBatch), smallTxn2);
		ensure_equals("(32)", STAILQ_NEXT(smallTxn2, nextInBatch), smallTxn3);
		ensure_equals("(33)", STAILQ_NEXT(smallTxn3, nextInBatch), mediumTxn);
		ensure_equals("(34)", STAILQ_NEXT(mediumTxn, nextInBatch), (Transaction *) NULL);
		ensure_equals("(35)", STAILQ_NEXT(mediumTxn2, nextInBatch), mediumTxn3);
		ensure_equals("(36)", STAILQ_NEXT(mediumTxn3, nextInBatch), (Transaction *) NULL);
	}

	/***** Test createBatchObjectsForUndersizedTransactions() *****/

	TEST_METHOD(3) {
		set_test_name("createBatchObjectsForUndersizedTransactions");
		TransactionList transactions;

		STAILQ_INIT(&transactions);
		STAILQ_INSERT_TAIL(&transactions, smallTxn, next);
		STAILQ_INSERT_TAIL(&transactions, smallTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, smallTxn3, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, mediumTxn3, next);
		STAILQ_INSERT_TAIL(&transactions, largeTxn, next);

		STAILQ_NEXT(smallTxn, nextInBatch) = smallTxn2;
		STAILQ_NEXT(smallTxn2, nextInBatch) = NULL;
		STAILQ_NEXT(smallTxn3, nextInBatch) = mediumTxn;
		STAILQ_NEXT(mediumTxn, nextInBatch) = NULL;
		STAILQ_NEXT(mediumTxn2, nextInBatch) = NULL;
		STAILQ_NEXT(mediumTxn3, nextInBatch) = largeTxn;
		STAILQ_NEXT(largeTxn, nextInBatch) = NULL;

		boost::container::vector<Batch> batches;
		BatchingAlgorithm::createBatchObjectsForUndersizedTransactions(transactions,
			batches);
		smallTxn = NULL;
		smallTxn2 = NULL;
		smallTxn3 = NULL;
		mediumTxn = NULL;
		mediumTxn2 = NULL;
		mediumTxn3 = NULL;
		largeTxn = NULL;
		ensure(STAILQ_EMPTY(&transactions));
		ensure_equals(batches.size(), 4u);
	}

	/***** Test createBatchObjectsForOversizedTransactions() *****/

	TEST_METHOD(4) {
		set_test_name("createBatchObjectsForOversizedTransactions");
		TransactionList transactions;

		STAILQ_INIT(&transactions);
		STAILQ_INSERT_TAIL(&transactions, largeTxn, next);
		STAILQ_INSERT_TAIL(&transactions, largeTxn2, next);
		STAILQ_INSERT_TAIL(&transactions, largeTxn3, next);

		boost::container::vector<Batch> batches;
		BatchingAlgorithm::createBatchObjectsForOversizedTransactions(transactions,
			batches);
		largeTxn = NULL;
		largeTxn2 = NULL;
		largeTxn3 = NULL;
		ensure(STAILQ_EMPTY(&transactions));
		ensure_equals(batches.size(), 3u);
	}
}
