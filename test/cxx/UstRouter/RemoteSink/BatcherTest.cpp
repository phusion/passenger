#include "TestSupport.h"
#include <boost/bind.hpp>
#include <SafeLibev.h>
#include <BackgroundEventLoop.h>
#include <Logging.h>
#include <Utils/MessagePassing.h>
#include <UstRouter/RemoteSink/Batcher.h>

using namespace Passenger;
using namespace Passenger::UstRouter;
using namespace Passenger::UstRouter::RemoteSink;
using namespace std;

namespace tut {
	struct UstRouter_RemoteSink_BatcherTest {
		class TestSender: public SegmentProcessor {
		public:
			unsigned int nScheduledSegments;
			unsigned int nScheduledBatches;

			TestSender()
				: nScheduledSegments(0),
				  nScheduledBatches(0)
				{ }

			virtual void schedule(SegmentList &segments) {
				Segment *segment;

				STAILQ_FOREACH(segment, &segments, nextScheduledForSending) {
					nScheduledSegments++;
					nScheduledBatches += segment->incomingBatches.size();
					segment->incomingBatches.clear();
				}
				STAILQ_INIT(&segments);
			}
		};

		class TestBatcher: public Batcher {
		protected:
			virtual void waitForThreadInitializationSignal(Segment *segment) {
				vector<string> messages;
				messages.push_back("Go ahead");
				messages.push_back("Go ahead segment " + toString(segment->number));
				inbox->recvAny(messages);
				outbox->send("Proceeding with thread for segment " + toString(segment->number));
			}

		public:
			MessageBoxPtr inbox, outbox;

			TestBatcher(Context *context, SegmentProcessor *sender, const VariantMap &options)
				: Batcher(context, sender, options)
			{
				inbox = boost::make_shared<MessageBox>();
				outbox = boost::make_shared<MessageBox>();
			}
		};

		BackgroundEventLoop bg;
		Context context;
		TestBatcher *batcher;
		TestSender sender;
		SegmentList segments;
		Segment *segment;
		VariantMap options;

		StaticString smallBody, mediumBody, largeBody;

		#define SMALL_TXN_SIZE 4u
		Transaction *smallTxn, *smallTxn2, *smallTxn3;
		#define MEDIUM_TXN_SIZE 6u
		Transaction *mediumTxn, *mediumTxn2;

		UstRouter_RemoteSink_BatcherTest()
			: bg(false, true),
			  context(bg.safe->getLoop())
		{
			batcher = NULL;
			segment = new Segment(1, "segment1");

			STAILQ_INIT(&segments);

			options.setInt("union_station_batcher_threshold", 512);
			options.setInt("union_station_batcher_memory_limit", 512);

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
		}

		~UstRouter_RemoteSink_BatcherTest() {
			if (batcher != NULL) {
				for (unsigned int i = 0; i < 10; i++) {
					batcher->inbox->send("Go ahead");
				}

				startLoop();
				bg.safe->runLater(boost::bind(&TestBatcher::shutdown, batcher, true));
				while (!isTerminated()) {
					usleep(20000);
				}
				bg.safe->runSync(boost::bind(
					&UstRouter_RemoteSink_BatcherTest::destroyBatcher, this));
			}
			bg.stop();

			segment->unref();
			delete smallTxn;
			delete smallTxn2;
			delete smallTxn3;
			delete mediumTxn;
			delete mediumTxn2;
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		void init(bool start = true) {
			batcher = new TestBatcher(&context, &sender, options);
			if (start) {
				batcher->start();
			}
		}

		void destroyBatcher() {
			delete batcher;
		}

		struct ev_loop *getLoop() {
			return bg.safe->getLoop();
		}

		void startLoop() {
			if (!bg.isStarted()) {
				bg.start();
			}
		}

		void addTxn(Transaction **transaction) {
			if (STAILQ_EMPTY(&segments)) {
				STAILQ_INSERT_TAIL(&segments, segment, nextScheduledForBatching);
			}
			STAILQ_INSERT_TAIL(&segment->incomingTransactions, *transaction, next);
			segment->bytesIncomingTransactions += (*transaction)->getBody().size();
			segment->nIncomingTransactions++;
			*transaction = NULL;
		}

		bool isTerminated() {
			bool result;
			bg.safe->runSync(boost::bind(&UstRouter_RemoteSink_BatcherTest::_isTerminated,
				this, &result));
			return result;
		}

		void _isTerminated(bool *result) {
			*result = batcher->isTerminated();
		}

		Json::Value inspectStateAsJson() {
			Json::Value result;
			bg.safe->runSync(boost::bind(&UstRouter_RemoteSink_BatcherTest::_inspectStateAsJson,
				this, &result));
			return result;
		}

		void _inspectStateAsJson(Json::Value *result) {
			*result = batcher->inspectStateAsJson();
		}

		void schedule() {
			bg.safe->runSync(boost::bind(&UstRouter_RemoteSink_BatcherTest::_schedule,
				this));
		}

		void _schedule() {
			batcher->schedule(segments);
		}

		template<typename T>
		T getVarThroughEventLoop(T *var) {
			T result;
			bg.safe->runSync(boost::bind(_getVarThroughEventLoop<T>, var, &result));
			return result;
		}

		template<typename T>
		static void _getVarThroughEventLoop(T *var, T *result) {
			*result = *var;
		}
	};

	DEFINE_TEST_GROUP(UstRouter_RemoteSink_BatcherTest);


	/***** Overall sanity check *****/

	TEST_METHOD(1) {
		set_test_name("It compresses and batches transactions in the background and forwards them to the sender");

		options.setInt("union_station_batcher_threshold", 2 * SMALL_TXN_SIZE);
		init();
		startLoop();

		addTxn(&smallTxn);
		addTxn(&smallTxn2);
		addTxn(&smallTxn3);
		schedule();

		batcher->inbox->send("Go ahead");
		batcher->outbox->recv("Proceeding with thread for segment 1");

		EVENTUALLY(5,
			result = getVarThroughEventLoop(&sender.nScheduledSegments) == 1;
		);
		Json::Value doc = inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 0u);
		ensure_equals("(2)", doc["forwarding"]["count"].asUInt(), 0u);
		ensure_equals("(3)", doc["forwarded"]["count"].asUInt(), 2u);
		ensure_equals("(4)", doc["dropped"]["count"].asUInt(), 0u);

		// smallTxn and smallTxn2 should be batched together,
		// while smallTxn3 is sent separately.
		ensure_equals("(5)", getVarThroughEventLoop(&sender.nScheduledBatches), 2u);
	}


	/***** Scheduling *****/

	TEST_METHOD(5) {
		set_test_name("It schedules all transactions that fit within the limits");

		options.setInt("union_station_batcher_memory_limit", 3 * SMALL_TXN_SIZE);
		init();

		addTxn(&smallTxn);
		addTxn(&smallTxn2);
		addTxn(&smallTxn3);
		batcher->schedule(segments);

		batcher->inbox->send("Go ahead");
		batcher->outbox->recv("Proceeding with thread for segment 1");

		Json::Value doc = batcher->inspectStateAsJson();
		ensure_equals("(1)", doc["accepted"]["count"].asUInt(), 3u);
		EVENTUALLY(5,
			result = batcher->inspectStateAsJson()["forwarding"]["count"].asUInt() == 1;
		);
	}

	TEST_METHOD(6) {
		set_test_name("It drops all transactions that don't fit within the limits");

		options.setInt("union_station_batcher_memory_limit", SMALL_TXN_SIZE + 1);
		init();

		addTxn(&smallTxn);
		addTxn(&smallTxn2);
		addTxn(&smallTxn3);
		setLogLevel(LVL_CRIT);
		batcher->schedule(segments);

		batcher->inbox->send("Go ahead");
		batcher->outbox->recv("Proceeding with thread for segment 1");

		Json::Value doc = batcher->inspectStateAsJson();
		ensure_equals("(1)", doc["dropped"]["count"].asUInt(), 1u);
	}

	TEST_METHOD(7) {
		set_test_name("If the processor thread hasn't woken up yet, it appends all scheduleable"
			" transactions to the processing queue");

		init();

		addTxn(&smallTxn);
		batcher->schedule(segments);

		addTxn(&smallTxn2);
		batcher->schedule(segments);

		Json::Value doc = batcher->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 2u);
	}

	TEST_METHOD(8) {
		set_test_name("Scheduling multiple times");

		init();

		addTxn(&smallTxn);
		batcher->schedule(segments);
		batcher->inbox->send("Go ahead");
		batcher->outbox->recv("Proceeding with thread for segment 1");
		EVENTUALLY(5,
			result = batcher->inspectStateAsJson()["forwarding"]["count"].asUInt() == 1;
		);

		addTxn(&smallTxn2);
		batcher->schedule(segments);

		startLoop();
		EVENTUALLY(5,
			result = inspectStateAsJson()["forwarded"]["count"].asUInt() == 2;
		);
	}


	/***** Processing *****/

	TEST_METHOD(11) {
		set_test_name("If the event loop hasn't woken up yet, it appends all generated batches"
			" to the forwarding queue");

		init();

		addTxn(&smallTxn);
		batcher->schedule(segments);
		batcher->inbox->send("Go ahead");
		batcher->outbox->recv("Proceeding with thread for segment 1");
		EVENTUALLY(5,
			result = batcher->inspectStateAsJson()["forwarding"]["count"].asUInt() == 1;
		);

		addTxn(&smallTxn2);
		batcher->schedule(segments);
		EVENTUALLY(5,
			result = batcher->inspectStateAsJson()["forwarding"]["count"].asUInt() == 2;
		);

		batcher->processingDone();
		ensure_equals("(1)", sender.nScheduledSegments, 1u);
		ensure_equals("(2)", sender.nScheduledBatches, 2u);
	}


	/***** Shutdown *****/

	TEST_METHOD(20) {
		set_test_name("It drops all newly scheduled transactions while shutting down");

		init();
		batcher->shutdown();
		addTxn(&smallTxn);
		addTxn(&smallTxn2);
		batcher->schedule(segments);

		Json::Value doc = batcher->inspectStateAsJson();
		ensure_equals("(1)", doc["dropped"]["count"].asUInt(), 2u);
	}

	TEST_METHOD(21) {
		set_test_name("If it is not started then it can be destroyed");

		init(false);
		ensure("(1)", batcher->shutdown());
		delete batcher; // Does not throw
		batcher = NULL;
	}

	TEST_METHOD(22) {
		set_test_name("Shutdown without having anything scheduled before");

		init();
		ensure("(1)", batcher->shutdown());
		delete batcher; // Does not throw
		batcher = NULL;
	}

	TEST_METHOD(23) {
		set_test_name("Shutdown while having something scheduled before, without dropping queued work");

		init();
		addTxn(&smallTxn);
		addTxn(&smallTxn2);
		batcher->schedule(segments);
		batcher->shutdown();

		batcher->inbox->send("Go ahead");
		batcher->outbox->recv("Proceeding with thread for segment 1");

		startLoop();
		EVENTUALLY(5,
			result = getVarThroughEventLoop(&sender.nScheduledSegments) == 1;
		);
	}

	TEST_METHOD(24) {
		set_test_name("Shutdown while having something scheduled before, dropping queued work");

		init();
		addTxn(&smallTxn);
		addTxn(&smallTxn2);
		batcher->schedule(segments);
		batcher->shutdown(true);

		batcher->inbox->send("Go ahead");
		batcher->outbox->recv("Proceeding with thread for segment 1");

		startLoop();
		SHOULD_NEVER_HAPPEN(100,
			result = getVarThroughEventLoop(&sender.nScheduledSegments) > 0;
		);
		EVENTUALLY(5,
			Json::Value doc = inspectStateAsJson();
			result = doc["dropped"]["count"].asUInt() == 2;
		);
	}
} //
