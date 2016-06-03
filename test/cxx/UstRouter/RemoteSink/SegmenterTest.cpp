#include "TestSupport.h"
#include <SafeLibev.h>
#include <BackgroundEventLoop.h>
#include <Utils/StrIntUtils.h>
#include <Utils/SystemTime.h>
#include <UstRouter/RemoteSink/Segmenter.h>

using namespace Passenger;
using namespace Passenger::UstRouter;
using namespace Passenger::UstRouter::RemoteSink;
using namespace std;

namespace tut {
	struct UstRouter_RemoteSink_SegmenterTest {
		class TestBatcher: public SegmentProcessor {
		public:
			vector<SegmentPtr> segments;

			virtual void schedule(SegmentList &segments) {
				Segment *segment, *nextSegment;

				STAILQ_FOREACH_SAFE(segment, &segments, nextScheduledForBatching, nextSegment) {
					segment->scheduledForBatching = false;
					this->segments.push_back(SegmentPtr(segment));
				}

				STAILQ_INIT(&segments);
			}
		};

		class TestServerLivelinessChecker: public AbstractServerLivelinessChecker {
		public:
			unsigned int nRegistered;

			TestServerLivelinessChecker()
				: nRegistered(0)
				{ }

			virtual void registerServers(const Segment::SmallServerList &servers) {
				nRegistered++;
			}
		};

		class TestSegmenter: public Segmenter {
		protected:
			virtual bool initiateApiLookup(const KeyInfoPtr &keyInfo) {
				keyInfo->lookingUp = apiLookupResult;
				if (!apiLookupResult) {
					keyInfo->lastLookupErrorTime = ev_now(getLoop());
					keyInfo->lastErrorMessage = "Artificial error";
				}
				apiLookupsInitiated.push_back(keyInfo->key);
				return apiLookupResult;
			}

		public:
			vector<string> apiLookupsInitiated;
			bool apiLookupResult;

			TestSegmenter(Context *context, SegmentProcessor *batcher,
				AbstractServerLivelinessChecker *checker, const VariantMap &options)
				: Segmenter(context, batcher, checker, options),
				  apiLookupResult(true)
				{ }

			void triggerTimeout() {
				Segmenter::triggerTimeout();
			}

			Segment *getSegment(unsigned int number) {
				return Segmenter::getSegment(number);
			}
		};

		BackgroundEventLoop bg;
		Context context;
		TransactionList transactions;
		size_t totalBodySize;
		size_t bytesAdded;
		unsigned int nAdded;
		unsigned int nTransactions;
		VariantMap options;
		TestBatcher batcher;
		TestServerLivelinessChecker checker;
		TestSegmenter *segmenter;

		UstRouter_RemoteSink_SegmenterTest()
			: bg(false, true),
			  context(bg.safe->getLoop())
		{
			STAILQ_INIT(&transactions);
			totalBodySize = 0;
			bytesAdded = 0;
			nAdded = 0;
			nTransactions = 0;
			segmenter = NULL;
			options.setULL("union_station_segmenter_memory_limit", 1024);
		}

		~UstRouter_RemoteSink_SegmenterTest() {
			Transaction *transaction, *nextTransaction;

			STAILQ_FOREACH_SAFE(transaction, &transactions, next, nextTransaction) {
				delete transaction;
			}

			delete segmenter;

			SystemTime::releaseAll();
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		void init() {
			segmenter = new TestSegmenter(&context, &batcher, &checker, options);
		}

		struct ev_loop *getLoop() {
			return bg.safe->getLoop();
		}

		void mockTime(ev_tstamp t) {
			ev_set_time(getLoop(), t);
			SystemTime::forceAll(t * 1000000ull);
		}

		Transaction *createTxn(const string &key, bool addToList = true) {
			Transaction *txn = new Transaction("", "", "", key, 0);
			txn->append("body");
			if (addToList) {
				STAILQ_INSERT_TAIL(&transactions, txn, next);
				totalBodySize += txn->getBody().size();
				nTransactions++;
			}
			return txn;
		}

		SegmentPtr createSegment(const string &segmentName, const string &key) {
			Transaction *txn = createTxn(key, false);
			TransactionList transactions;
			size_t oldTotalBodySize = totalBodySize;
			size_t bytesAdded;
			unsigned int nAdded;
			unsigned int oldSegmentsPassedToBatcher;

			STAILQ_INIT(&transactions);
			STAILQ_INSERT_TAIL(&transactions, txn, next);
			oldSegmentsPassedToBatcher = batcher.segments.size();
			segmenter->schedule(transactions, txn->getBody().size(), 1, bytesAdded, nAdded);
			totalBodySize = oldTotalBodySize;

			if (batcher.segments.size() == oldSegmentsPassedToBatcher) {
				// Segment doesn't exist yet; finish API lookup
				Json::Value manifest;
				manifest["status"] = "ok";
				manifest["targets"][0]["base_url"] = "http://" + segmentName;

				unsigned int oldNRegistered = checker.nRegistered;
				segmenter->apiLookupFinished(key, 0, CURLE_OK, 200, manifest.toStyledString(), "");
				checker.nRegistered = oldNRegistered;
			}

			ensure("createSegment: API lookup initiated",
				!segmenter->apiLookupsInitiated.empty());
			ensure_equals("createSegment: API lookup used expected key",
				segmenter->apiLookupsInitiated.back(), key);
			segmenter->apiLookupsInitiated.pop_back();

			ensure("createSegment: segment exists", !batcher.segments.empty());
			SegmentPtr segment = batcher.segments.back();
			batcher.segments.pop_back();

			ensure("createSegment: has incoming transactions",
				!STAILQ_EMPTY(&segment->incomingTransactions));

			// Remove last entry
			Transaction *prevTxn = NULL;
			STAILQ_FOREACH(txn, &segment->incomingTransactions, next) {
				if (STAILQ_NEXT(txn, next) == NULL) {
					break;
				} else {
					prevTxn = txn;
				}
			}
			if (prevTxn == NULL) {
				STAILQ_REMOVE_HEAD(&segment->incomingTransactions, next);
			} else {
				STAILQ_REMOVE_AFTER(&segment->incomingTransactions, prevTxn, next);
			}
			segment->bytesIncomingTransactions -= txn->getBody().size();
			segment->nIncomingTransactions--;
			delete txn;

			return segment;
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(UstRouter_RemoteSink_SegmenterTest, 130);

	/***** Scheduling *****/

	TEST_METHOD(1) {
		set_test_name("It queues transactions with unknown keys and initiates API lookups for those keys");

		createTxn("key1");
		createTxn("key1");
		createTxn("key2");
		createTxn("key3");
		init();

		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		ensure_equals("(1)", bytesAdded, totalBodySize);
		ensure_equals("(2)", nAdded, nTransactions);
		ensure("(3)", STAILQ_EMPTY(&transactions));

		ensure_equals("3 API lookups initiated",
			segmenter->apiLookupsInitiated.size(), 3u);
		ensure_equals("API lookup for key1 initiated",
			segmenter->apiLookupsInitiated[0], "key1");
		ensure_equals("API lookup for key2 initiated",
			segmenter->apiLookupsInitiated[1], "key2");
		ensure_equals("API lookup for key3 initiated",
			segmenter->apiLookupsInitiated[2], "key3");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("4 transactions queued", doc["queued"]["count"].asUInt(), 4u);
	}

	TEST_METHOD(2) {
		set_test_name("It segments transactions with known keys and forwards them to the batcher");

		init();
		createSegment("segment1", "key1");
		createSegment("segment2", "key2");
		Json::Value doc = segmenter->inspectStateAsJson();
		size_t bytesForwarded = doc["forwarded"]["bytes"].asUInt64();
		unsigned int nForwarded = doc["forwarded"]["count"].asUInt();

		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", batcher.segments.size(), 2u);
		ensure_equals("(2)", doc["forwarded"]["bytes"].asUInt64(), bytesForwarded + totalBodySize);
		ensure_equals("(3)", doc["forwarded"]["count"].asUInt(), nForwarded + 2u);
	}

	TEST_METHOD(3) {
		set_test_name("If multiple transactions map to the same segment, then that segment"
			" is forwarded to the batcher only once");

		init();
		createSegment("segment1", "key1");
		createSegment("segment2", "key2");
		Json::Value doc = segmenter->inspectStateAsJson();
		size_t bytesForwarded = doc["forwarded"]["bytes"].asUInt64();
		unsigned int nForwarded = doc["forwarded"]["count"].asUInt();

		createTxn("key1");
		createTxn("key1");
		createTxn("key2");
		createTxn("key2");

		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", batcher.segments.size(), 2u);
		ensure_equals("(2)", doc["forwarded"]["bytes"].asUInt64(), bytesForwarded + totalBodySize);
		ensure_equals("(3)", doc["forwarded"]["count"].asUInt(), nForwarded + 4u);
	}

	TEST_METHOD(4) {
		set_test_name("If the memory limit has been passed then it drops the transactions");

		createTxn("key1");
		createTxn("key1");
		createTxn("key2");
		options.setULL("union_station_segmenter_memory_limit", totalBodySize + 1);
		createTxn("key2");
		createTxn("key2");
		init();
		setLogLevel(LVL_ERROR);

		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 4u);
		ensure_equals("(2)", doc["dropped"]["count"].asUInt(), 1u);
	}

	TEST_METHOD(5) {
		set_test_name("If API lookup fails to initiate then it tries again later");

		createTxn("key1");
		init();
		mockTime(1);
		segmenter->apiLookupResult = false;
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure("(2)", doc["keys"].isMember("key1"));
		ensure("(3)", !doc["keys"]["key1"]["looking_up"].asBool());
		ensure_equals("(4)",
			doc["keys"]["key1"]["last_lookup_error_time"]["timestamp"].asDouble(),
			1);
		ensure_equals("(5)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asDouble(),
			1 + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure("(6)",
			doc["next_key_refresh_time"]["timestamp"].asDouble() >=
			1 + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}


	/***** Next key info refresh time scheduling *****/

	TEST_METHOD(10) {
		set_test_name("It is initially not scheduled");

		init();
		Json::Value doc = segmenter->inspectStateAsJson();
		ensure(doc["next_key_refresh_time"].isNull());
	}

	TEST_METHOD(11) {
		set_test_name("It picks the earliest time and restarts the timer");

		init();

		mockTime(1);
		createSegment("segment1", "key1");

		mockTime(11);
		createSegment("segment2", "key2");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			1ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
		ensure_equals("(2)", doc["keys"]["key2"]["next_refresh_time"]["timestamp"].asUInt64(),
			11ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
		ensure_equals("(3)", doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
	}

	TEST_METHOD(12) {
		set_test_name("It ignores key infos for which an API lookup is active");

		init();

		mockTime(1);
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);

		mockTime(11);
		createSegment("segment1", "key2");

		mockTime(21);
		createSegment("segment2", "key3");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", doc["keys"]["key1"]["looking_up"].asBool());
		ensure("(2)", doc["keys"]["key1"]["next_refresh_time"].isNull());
		ensure_equals("(1)", doc["keys"]["key2"]["next_refresh_time"]["timestamp"].asUInt64(),
			11ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
		ensure_equals("(2)", doc["keys"]["key3"]["next_refresh_time"]["timestamp"].asUInt64(),
			21ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
		ensure_equals("(5)", doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			15ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
	}

	TEST_METHOD(13) {
		set_test_name("It stops the timer if all key infos are busy with API lookups");

		mockTime(0);
		init();
		createSegment("segment1", "key1");
		createSegment("segment2", "key2");
		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", !doc["next_key_refresh_time"].isNull());

		mockTime(9999);
		segmenter->triggerTimeout();
		doc = segmenter->inspectStateAsJson();
		ensure("(2)", doc["next_key_refresh_time"].isNull());
	}


	/***** Key info refresh handling *****/

	TEST_METHOD(20) {
		set_test_name("It performs API lookups for all keys whose next refresh time has passed");

		init();

		mockTime(1);
		createSegment("segment1", "key1");

		mockTime(11);
		createSegment("segment2", "key2");

		mockTime(21);
		createSegment("segment3", "key3");

		mockTime(11 + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
		segmenter->triggerTimeout();
		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", doc["keys"]["key1"]["looking_up"].asBool());
		ensure("(2)", doc["keys"]["key2"]["looking_up"].asBool());
		ensure("(3)", !doc["keys"]["key3"]["looking_up"].asBool());
	}

	TEST_METHOD(21) {
		set_test_name("It reschedules an API lookup for a later time if initiating the lookup failed");

		init();
		mockTime(1);
		createSegment("segment1", "key1");

		mockTime(11 + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);

		segmenter->apiLookupResult = false;
		segmenter->triggerTimeout();

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", !doc["keys"]["key1"]["looking_up"].asBool());
		ensure("(1)", doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64()
			> ev_now(getLoop()));
	}


	/***** Handling API lookup results for unknown keys *****/

	TEST_METHOD(30) {
		set_test_name("If CURL returned with an error then it drops all queued transactions with the same key");

		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_COULDNT_CONNECT,
			200, "", "my error");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure_equals("(2)", doc["queued"]["items"][0]["key"].asString(), "key2");
	}

	TEST_METHOD(31) {
		set_test_name("If CURL returned with an error then it logs the error");

		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_COULDNT_CONNECT,
			200, "", "my error");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"appears to be down"));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"appears to be down"));
	}

	TEST_METHOD(32) {
		set_test_name("If CURL returned with an error then it schedules a refresh"
			" in the near future according to a default 'has_errors' timeout");

		mockTime(1);
		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_COULDNT_CONNECT,
			200, "", "my error");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			1ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(35) {
		set_test_name("If the response is gibberish then it drops all queued transactions with the same key");

		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure_equals("(2)", doc["queued"]["items"][0]["key"].asString(), "key2");
	}

	TEST_METHOD(36) {
		set_test_name("If the response is gibberish then it logs the error");

		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"unparseable"));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"unparseable"));
	}

	TEST_METHOD(37) {
		set_test_name("If the response is gibberish then it schedules a refresh in the"
			" near future according to a default 'has_errors' timeout");

		mockTime(1);
		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			1ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(40) {
		set_test_name("If the response is parseable but not valid then it drops all"
			" queued transactions with the same key");

		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure_equals("(2)", doc["queued"]["items"][0]["key"].asString(), "key2");
	}

	TEST_METHOD(41) {
		set_test_name("If the response is parseable but not valid then it logs the error");

		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"parseable, but does not comply"));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"parseable, but does not comply"));
	}

	TEST_METHOD(42) {
		set_test_name("If the response is parseable but not valid then it schedules a refresh"
			" in the near future according to a default 'has_errors' timeout");

		mockTime(1);
		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			1ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(45) {
		set_test_name("If the response is valid but has a non-ok status then it drops"
			" all queued transactions with the same key");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";

		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure_equals("(2)", doc["queued"]["items"][0]["key"].asString(), "key2");
	}

	TEST_METHOD(46) {
		set_test_name("If the response is valid but has a non-ok status then it logs the error");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";

		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure("(2)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"Message from server: oh no"));
		ensure("(3)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(4)", doc["last_error"]["time"].isObject());
		ensure("(5)", containsSubstring(doc["last_error"]["message"].asString(),
			"Message from server: oh no"));
	}

	TEST_METHOD(47) {
		set_test_name("If the response is valid but has a non-ok status then it schedules"
			" a refresh according to the timeout provided by the recheck_balancer_in key and updates"
			" the 'has_error' timeout");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";
		doc["recheck_balancer_in"] = 122;

		mockTime(1);
		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			123ull);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY * 1000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			122 * 1000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			125ull);
	}

	TEST_METHOD(48) {
		set_test_name("If the response is valid but has a non-ok status, and there is"
			" no recheck_balancer_in key, then it schedules a refresh in the near future according"
			" to the default 'has_errors' timeout");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";

		mockTime(1);
		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			1ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY * 1000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS * 1000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(49) {
		set_test_name("If the response is valid but has a non-ok status, and there is a suspend_sending key,"
			" then any newly scheduled transactions are dropped until the timeout has passed");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";
		doc["suspend_sending"] = 123;

		mockTime(1);
		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["suspend_sending_until"]["timestamp"].asUInt64(),
			124u);

		nTransactions = 0;
		totalBodySize = 0;
		createTxn("key1");
		mockTime(2);
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		ensure_equals("(2)", bytesAdded, 0u);
		ensure_equals("(3)", nAdded, 0u);

		nTransactions = 0;
		totalBodySize = 0;
		createTxn("key1");
		mockTime(125);
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		ensure_equals("(4)", bytesAdded, totalBodySize);
		ensure_equals("(5)", nAdded, 1u);
	}

	TEST_METHOD(55) {
		set_test_name("If the response is valid but has a non-200 HTTP code then"
			" it drops all queued transactions with the same key");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 500, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure_equals("(2)", doc["queued"]["items"][0]["key"].asString(), "key2");
	}

	TEST_METHOD(56) {
		set_test_name("If the response is valid but has a non-200 HTTP code then it logs the error");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 500, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"invalid HTTP response code."));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"invalid HTTP response code."));
	}

	TEST_METHOD(57) {
		set_test_name("If the response is valid but has a non-200 HTTP code then it"
			" schedules a refresh in the near future according to the 'has_errors'"
			" timeout regardless of whether there is a 'recheck_balancer_in' key");

		Json::Value doc;
		doc["status"] = "ok";
		doc["recheck_balancer_in"]["all_healthy"] = 122;
		doc["recheck_balancer_in"]["has_errors"] = 456;
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createTxn("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 500, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			1ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY * 1000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS * 1000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(60) {
		set_test_name("If the response is valid then it forwards all queued"
			" transactions with the same key to the batcher");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createTxn("key1");
		createTxn("key2");
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", batcher.segments.size(), 1u);
		ensure_equals("(2)", batcher.segments[0]->nIncomingTransactions, 2u);

		Transaction *txn = STAILQ_FIRST(&batcher.segments[0]->incomingTransactions);
		ensure_equals("(3)", txn->getUnionStationKey(), "key1");
		ensure_equals("(4)", STAILQ_NEXT(txn, next)->getUnionStationKey(), "key1");

		ensure_equals("(5)",
			doc["forwarded"]["bytes"].asUInt64(),
			totalBodySize / 3 * 2);
		ensure_equals("(6)",
			doc["forwarded"]["count"].asUInt64(),
			2u);
	}

	TEST_METHOD(61) {
		set_test_name("If the response is valid then it updates the segment's"
			" server list and balancing list");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createTxn("key1");
		createTxn("key2");
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["segments"]["1"]["servers"].size(), 2u);
		ensure_equals("(2)",
			doc["segments"]["1"]["servers"][0].asUInt(),
			1u);
		ensure_equals("(3)",
			doc["segments"]["1"]["servers"][1].asUInt(),
			2u);
		ensure_equals("(4)",
			doc["servers"]["1"]["base_url"].asString(),
			"http://server1");
		ensure_equals("(5)",
			doc["servers"]["1"]["weight"].asUInt(),
			1u);
		ensure_equals("(6)",
			doc["servers"]["2"]["base_url"].asString(),
			"http://server2");
		ensure_equals("(7)",
			doc["servers"]["2"]["weight"].asUInt(),
			2u);

		Segment *segment = segmenter->getSegment(1);
		ensure_equals("(8)", segment->balancingList.size(), 3u);
		ensure_equals("(9)", segment->nextBalancingIndex, 0u);
	}

	TEST_METHOD(62) {
		set_test_name("If the response is valid then it registers the servers"
			" with the liveliness checker");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createTxn("key1");
		createTxn("key2");
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);

		ensure_equals("(1)", checker.nRegistered, 0u);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);
		ensure_equals("(2)", checker.nRegistered, 1u);
	}

	TEST_METHOD(63) {
		set_test_name("If the response is valid then it schedules a refresh according to the"
			" the timeout provided by the recheck_balancer_in key and updates the stored timeouts");

		Json::Value doc;
		doc["status"] = "ok";
		doc["recheck_balancer_in"]["all_healthy"] = 122;
		doc["recheck_balancer_in"]["has_errors"] = 456;
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			123ull);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			122000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			456000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			125ull);
	}

	TEST_METHOD(64) {
		set_test_name("If the response is valid, and there is no recheck_balancer_in key,"
			" then it schedules a refresh in the near future according to"
			" the default 'all_healthy' timeout");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createTxn("key1");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			1ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
	}


	/***** Handling API lookup results for known keys *****/

	TEST_METHOD(70) {
		set_test_name("If CURL returned with an error then it does not drop any queued transactions");

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_COULDNT_CONNECT,
			200, "", "my error");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure("(2)", doc["keys"]["key2"]["looking_up"].asBool());
	}

	TEST_METHOD(71) {
		set_test_name("If CURL returned with an error then it logs the error");

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_COULDNT_CONNECT,
			200, "", "my error");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"appears to be down"));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"appears to be down"));
	}

	TEST_METHOD(72) {
		set_test_name("If CURL returned with an error then it schedules a refresh in"
			" the near future according to the last 'has_errors' timeout");

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_COULDNT_CONNECT,
			200, "", "my error");

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			2ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(75) {
		set_test_name("If the response is gibberish then it does not drop any queued transactions");

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure("(2)", doc["keys"]["key2"]["looking_up"].asBool());
	}

	TEST_METHOD(76) {
		set_test_name("If the response is gibberish then it logs the error");

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"unparseable"));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"unparseable"));
	}

	TEST_METHOD(77) {
		set_test_name("If the response is gibberish then it schedules a refresh in the near future"
			" according to the last 'has_errors' timeout");

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			2ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(80) {
		set_test_name("If the response is parseable but not valid then it does not drop any queued transactions");

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure("(2)", doc["keys"]["key2"]["looking_up"].asBool());
	}

	TEST_METHOD(81) {
		set_test_name("If the response is parseable but not valid then it logs the error");

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"parseable, but does not comply"));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"parseable, but does not comply"));
	}

	TEST_METHOD(82) {
		set_test_name("If the response is parseable but not valid then it schedules a refresh"
			" in the near future according to the last 'has_errors' timeout");

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			2ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(85) {
		set_test_name("If the response is valid but has a non-ok status then it does not drop any queued transactions");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure("(2)", doc["keys"]["key2"]["looking_up"].asBool());
	}

	TEST_METHOD(86) {
		set_test_name("If the response is valid but has a non-ok status then it logs the error");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure("(2)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"Message from server: oh no"));
		ensure("(3)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(4)", doc["last_error"]["time"].isObject());
		ensure("(5)", containsSubstring(doc["last_error"]["message"].asString(),
			"Message from server: oh no"));
	}

	TEST_METHOD(87) {
		set_test_name("If the response is valid but has a non-ok status then it schedules a refresh"
			" according to the timeout provided by the recheck_balancer_in key and updates the 'has_error' timeout");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";
		doc["recheck_balancer_in"] = 121;

		mockTime(1);
		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");

		mockTime(2);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			123u);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY * 1000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			121 * 1000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			125u);
	}

	TEST_METHOD(88) {
		set_test_name("If the response is valid but has a non-ok status, and there is no recheck_balancer_in key,"
			" then it schedules a refresh in the near future according to the last 'has_errors' timeout");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";

		mockTime(1);
		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");

		mockTime(2);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			2ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY * 1000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS * 1000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(89) {
		set_test_name("If the response is valid but has a non-ok status, and there is a suspend_sending key,"
			" then any newly scheduled transactions are dropped until the timeout has passed");

		Json::Value doc;
		doc["status"] = "error";
		doc["message"] = "oh no";
		doc["suspend_sending"] = 123;

		mockTime(1);
		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");

		mockTime(2);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["suspend_sending_until"]["timestamp"].asUInt64(),
			125u);

		nTransactions = 0;
		totalBodySize = 0;
		createTxn("key1");
		mockTime(3);
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		ensure_equals("(2)", bytesAdded, 0u);
		ensure_equals("(3)", nAdded, 0u);

		nTransactions = 0;
		totalBodySize = 0;
		createTxn("key1");
		mockTime(126);
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		ensure_equals("(4)", bytesAdded, totalBodySize);
		ensure_equals("(5)", nAdded, 1u);
	}

	TEST_METHOD(95) {
		set_test_name("If the response is valid but has a non-200 HTTP code then it does"
			" not drop any queued transactions");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		createTxn("key2");
		segmenter->schedule(transactions, totalBodySize, nTransactions, bytesAdded, nAdded);
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 500, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["queued"]["count"].asUInt(), 1u);
		ensure("(2)", doc["keys"]["key2"]["looking_up"].asBool());
	}

	TEST_METHOD(96) {
		set_test_name("If the response is valid but has a non-200 HTTP code then it logs the error");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 500, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["keys"]["key1"]["last_error"]["message"].asString(),
			"invalid HTTP response code."));
		ensure("(2)", doc["keys"]["key1"]["last_error"]["time"].isObject());
		ensure("(3)", doc["last_error"]["time"].isObject());
		ensure("(4)", containsSubstring(doc["last_error"]["message"].asString(),
			"invalid HTTP response code."));
	}

	TEST_METHOD(98) {
		set_test_name("If the response is valid but has a non-200 HTTP code then it"
			" schedules a refresh according to the timeout provided by the recheck_balancer_in"
			" key and updates the 'has_error' timeout regardless of whether there is"
			" a recheck_balancer_in key");

		Json::Value doc;
		doc["status"] = "ok";
		doc["recheck_balancer_in"]["all_healthy"] = 122;
		doc["recheck_balancer_in"]["has_errors"] = 456;
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		setLogLevel(LVL_CRIT);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 500, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			2ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY * 1000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS * 1000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5ull + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_HAVE_ERRORS);
	}

	TEST_METHOD(101) {
		set_test_name("If the response is valid and indicates that the key now belongs to a different"
			" segment, and that segment already exists, then it moves the key to that segment");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://segment2";

		init();
		createSegment("segment1", "key1");
		createSegment("segment2", "key2");
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["keys"]["key1"]["segment_number"].asUInt(), 2u);
	}

	TEST_METHOD(102) {
		set_test_name("If the response is valid and indicates that the key now belongs to a different"
			" segment, and that segment already exists, then it does not touch that segment's"
			" server list and balancing list");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://segment2";

		init();
		SegmentPtr segment1 = createSegment("segment1", "key1");
		SegmentPtr segment2 = createSegment("segment2", "key2");
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(2)", doc["segments"]["2"]["servers"].size(), 1u);
		ensure_equals("(3)",
			doc["segments"]["2"]["servers"][0].asUInt(),
			2u);
		ensure_equals("(5)",
			doc["servers"]["2"]["base_url"].asString(),
			"http://segment2");


		ensure_equals("Old segment's balancing list remains unchanged",
			segment1->balancingList.size(), 1u);
		ensure_equals("New segment's balancing list is updated",
			segment2->balancingList.size(), 1u);
		ensure_equals("(6)", segment2->nextBalancingIndex, 0u);
	}

	TEST_METHOD(103) {
		set_test_name("If the response is valid and indicates that the key now belongs to a different"
			" segment, and that segment does not already exist, then it creates a new segment and"
			" moves the key there");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)", doc["keys"]["key1"]["segment_number"].asUInt(), 2u);
	}

	TEST_METHOD(104) {
		set_test_name("If the response is valid and indicates that the key now belongs to a different"
			" segment, and that segment does not already exist, then it creates a new segment and"
			" populates its server list and balancing list");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		init();
		createSegment("segment1", "key1");
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(2)", doc["segments"]["2"]["servers"].size(), 2u);
		ensure_equals("(3)",
			doc["segments"]["2"]["servers"][0].asUInt(),
			2u);
		ensure_equals("(4)",
			doc["segments"]["2"]["servers"][1].asUInt(),
			3u);
		ensure_equals("(5)",
			doc["servers"]["2"]["base_url"].asString(),
			"http://server1");
		ensure_equals("(6)",
			doc["servers"]["2"]["weight"].asUInt(),
			1u);
		ensure_equals("(7)",
			doc["servers"]["3"]["base_url"].asString(),
			"http://server2");
		ensure_equals("(8)",
			doc["servers"]["3"]["weight"].asUInt(),
			2u);

		Segment *segment = segmenter->getSegment(2);
		ensure_equals("(9)", segment->balancingList.size(), 3u);
		ensure_equals("(10)", segment->nextBalancingIndex, 0u);
	}

	TEST_METHOD(105) {
		set_test_name("If the response is valid then it updates the segment's"
			" server list and balancing list");

		Json::Value doc;
		doc["status"] = "ok";
		doc["recheck_balancer_in"]["all_healthy"] = 121;
		doc["recheck_balancer_in"]["has_errors"] = 456;
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		Segment *segment = segmenter->getSegment(2);
		ensure_equals("(1)", segment->balancingList.size(), 3u);
		ensure_equals("(2)", segment->nextBalancingIndex, 0u);
	}

	TEST_METHOD(106) {
		set_test_name("If the response is valid then it registers the servers"
			" with the liveliness checker");

		Json::Value doc;
		doc["status"] = "ok";
		doc["recheck_balancer_in"]["all_healthy"] = 121;
		doc["recheck_balancer_in"]["has_errors"] = 456;
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");

		ensure_equals("(1)", checker.nRegistered, 0u);
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);
		ensure_equals("(2)", checker.nRegistered, 1u);
	}

	TEST_METHOD(107) {
		set_test_name("If the response is valid then it schedules a refresh according to the the"
			" timeout provided by the recheck_balancer_in key and updates the stored timeouts");

		Json::Value doc;
		doc["status"] = "ok";
		doc["recheck_balancer_in"]["all_healthy"] = 121;
		doc["recheck_balancer_in"]["has_errors"] = 456;
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			123u);
		ensure_equals("(2)",
			doc["keys"]["key1"]["refresh_timeout_when_all_healthy"]["microseconds"].asUInt64(),
			121000000ull);
		ensure_equals("(3)",
			doc["keys"]["key1"]["refresh_timeout_when_have_errors"]["microseconds"].asUInt64(),
			456000000ull);
		ensure_equals("(4)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			125u);
	}

	TEST_METHOD(108) {
		set_test_name("If the response is valid, and there is no recheck_balancer_in key, then it schedules"
			" a refresh in the near future according to the default 'all_healthy' timeout");

		Json::Value doc;
		doc["status"] = "ok";
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["keys"]["key1"]["next_refresh_time"]["timestamp"].asUInt64(),
			2u + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
		ensure_equals("(2)",
			doc["next_key_refresh_time"]["timestamp"].asUInt64(),
			5u + Segmenter::DEFAULT_KEY_INFO_REFRESH_TIME_WHEN_ALL_HEALTHY);
	}

	TEST_METHOD(109) {
		set_test_name("If the response is valid then it sets all the segment's servers'"
			" liveliness check period to what is provided in the recheck_down_gateway_in");

		Json::Value doc;
		doc["status"] = "ok";
		doc["recheck_down_gateway_in"] = 121;
		doc["targets"][0]["base_url"] = "http://server1";
		doc["targets"][0]["weight"] = 1;
		doc["targets"][1]["base_url"] = "http://server2";
		doc["targets"][1]["weight"] = 2;

		mockTime(1);
		init();
		createSegment("segment1", "key1");

		mockTime(2);
		segmenter->refreshKey("key1");
		segmenter->apiLookupFinished("key1", 0, CURLE_OK, 200, doc.toStyledString(), NULL);

		doc = segmenter->inspectStateAsJson();
		ensure_equals("(1)",
			doc["servers"]["2"]["ping_url"].asString(),
			"http://server1/ping");
		ensure_equals("(2)",
			doc["servers"]["2"]["liveliness_check_period"]["microseconds"].asUInt64(),
			121000000ull);
		ensure_equals("(3)",
			doc["servers"]["3"]["ping_url"].asString(),
			"http://server2/ping");
		ensure_equals("(4)",
			doc["servers"]["3"]["liveliness_check_period"]["microseconds"].asUInt64(),
			121000000ull);
	}


	/***** Miscellaneous *****/

	TEST_METHOD(120) {
		set_test_name("If stopSending() is called, then schedule() drops new transactions"
			" for that key until the timeout has passed");
		fail("TODO");
	}
} //
