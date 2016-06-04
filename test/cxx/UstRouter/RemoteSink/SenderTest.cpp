#include "TestSupport.h"
#include <SafeLibev.h>
#include <BackgroundEventLoop.h>
#include <UstRouter/RemoteSink/Sender.h>

using namespace Passenger;
using namespace Passenger::UstRouter;
using namespace Passenger::UstRouter::RemoteSink;
using namespace std;

namespace tut {
	struct UstRouter_RemoteSink_SenderTest {
		class TestSender: public Sender {
		protected:
			virtual CURLMcode curlMultiAddHandle(CURL *curl) {
				if (failAddHandle) {
					return CURLM_INTERNAL_ERROR;
				} else {
					return Sender::curlMultiAddHandle(curl);
				}
			}

		public:
			bool failAddHandle;

			TestSender(Context *context, const VariantMap &options)
				: Sender(context, options),
				  failAddHandle(false)
				{ }

			void transferFinished(unsigned int transferNumber, CURLcode code, long httpCode,
				const string &body, const char *errorBuf)
			{
				Sender::transferFinished(transferNumber, code, httpCode, body, errorBuf);
			}
		};

		BackgroundEventLoop bg;
		Context context;
		SegmentPtr segment;
		SegmentList segments;
		VariantMap options;
		TestSender *sender;

		UstRouter_RemoteSink_SenderTest()
			: bg(false, true),
			  context(bg.safe->getLoop())
		{
			sender = NULL;
			segment.reset(new Segment(1, "segment1"));
			STAILQ_INIT(&segments);
			STAILQ_INSERT_TAIL(&segments, segment.get(), nextScheduledForSending);

			options.setULL("union_station_sender_memory_limit", 1024);
			options.setInt("union_station_upload_timeout", 60);
			options.setInt("union_station_response_timeout", 60);
		}

		~UstRouter_RemoteSink_SenderTest() {
			delete sender;
			SystemTime::releaseAll();
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		void init() {
			sender = new TestSender(&context, options);
		}

		struct ev_loop *getLoop() {
			return bg.safe->getLoop();
		}

		void mockTime(ev_tstamp t) {
			mockTime(t);
			SystemTime::forceAll(t * 1000000ull);
		}

		void createServerObject(unsigned int number, unsigned int weight = 1) {
			ServerPtr server = boost::make_shared<Server>(number,
				"http://server" + toString(number), weight);
			segment->servers.push_back(server);
			for (unsigned int i = 0; i < weight; i++) {
				segment->balancingList.push_back(server);
			}
		}

		void createBatch() {
			Transaction *txn = new Transaction("", "", "", "key1", 0);
			txn->append("body");
			segment->incomingBatches.emplace_back(txn);
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(UstRouter_RemoteSink_SenderTest, 70);


	/***** Basic sanity tests *****/

	TEST_METHOD(1) {
		set_test_name("It sends the given batches to available servers");

		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);
		ensure_equals("(2)", doc["transfers"]["items"]["1"]["server_number"].asInt(), 1);
		ensure("(3)",
			doc["transfers"]["items"]["1"]["state"].asString() == "CONNECTING"
			|| doc["transfers"]["items"]["1"]["state"].asString() == "RECEIVING_RESPONSE");
		ensure_equals("(4)", doc["transfers"]["items"]["2"]["server_number"].asInt(), 2);
		ensure("(5)",
			doc["transfers"]["items"]["2"]["state"].asString() == "CONNECTING"
			|| doc["transfers"]["items"]["2"]["state"].asString() == "RECEIVING_RESPONSE");

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(6)", doc["accepted"]["count"].asUInt(), 0u);
		ensure_equals("(7)", doc["active_requests"].asUInt(), 1u);

		doc = segment->servers[1]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(8)", doc["accepted"]["count"].asUInt(), 0u);
		ensure_equals("(9)", doc["active_requests"].asUInt(), 1u);

		sender->transferFinished(1, CURLE_OK, 200,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(10)", doc["transfers"]["count"].asUInt(), 1u);
		ensure("(11)", !doc["transfers"]["items"].isMember("1"));
		ensure_equals("(12)", doc["transfers"]["items"]["2"]["server_number"].asInt(), 2);
		ensure("(13)",
			doc["transfers"]["items"]["2"]["state"].asString() == "CONNECTING"
			|| doc["transfers"]["items"]["2"]["state"].asString() == "RECEIVING_RESPONSE");

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(14)", doc["accepted"]["count"].asUInt(), 1u);
		ensure_equals("(15)", doc["active_requests"].asUInt(), 0u);

		doc = segment->servers[1]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(16)", doc["accepted"]["count"].asUInt(), 0u);
		ensure_equals("(17)", doc["active_requests"].asUInt(), 1u);
	}


	/***** Error handling *****/

	TEST_METHOD(10) {
		set_test_name("It drops batches if there are no up servers available");

		init();
		createBatch();
		createBatch();
		setLogLevel(LVL_CRIT);
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 0u);
		ensure_equals("(2)", doc["dropped"]["count"].asUInt(), 2u);
		ensure("(3)",
			containsSubstring(doc["last_dropped"]["message"].asString(),
				"all gateways are down"));
	}

	TEST_METHOD(11) {
		set_test_name("It drops batches if the memory limit has been reached");

		options.setULL("union_station_sender_memory_limit", 1);
		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		setLogLevel(LVL_CRIT);
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(2)", doc["dropped"]["count"].asUInt(), 1u);
		ensure("(3)",
			containsSubstring(doc["last_dropped"]["message"].asString(),
				"Unable to send data to the Union Station "
				"gateway servers quickly enough"));
	}

	TEST_METHOD(12) {
		set_test_name("It drops batches if transfers cannot be initiated");

		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		setLogLevel(LVL_CRIT);
		sender->failAddHandle = true;
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 0u);
		ensure_equals("(2)", doc["dropped"]["count"].asUInt(), 2u);
		ensure("(3)",
			containsSubstring(doc["last_dropped"]["message"].asString(),
				"Error initiating transfer to gateway"));
	}

	TEST_METHOD(13) {
		set_test_name("If transfers failed to perform then it drops batches and marks the server as down");

		init();
		createServerObject(1);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		setLogLevel(LVL_CRIT);
		sender->transferFinished(1, CURLE_FAILED_INIT, 0, "", "oh no");
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(3)", doc["dropped"]["count"].asUInt(), 1u);
		ensure("(4)", containsSubstring(
			doc["last_dropped"]["message"].asString(),
			"It might be down"));

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(5)", doc["dropped"]["count"].asUInt(), 1u);
		ensure_equals("(6)", doc["active_requests"].asUInt(), 1u);
		ensure("(7)", !doc["up"].asBool());
	}

	TEST_METHOD(14) {
		set_test_name("If transfers failed to perform then it retries with a different server");

		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		sender->transferFinished(1, CURLE_FAILED_INIT, 0, "", "oh no");
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 2u);
		ensure("(3)", doc["transfers"]["items"].isMember("3"));
		ensure_equals("(4)", doc["dropped"]["count"].asUInt(), 0u);

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure("(5)", !doc["up"].asBool());

		sender->transferFinished(3, CURLE_OK, 200,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(6)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(7)", doc["accepted"]["count"].asUInt(), 1u);
	}

	TEST_METHOD(15) {
		set_test_name("If the response is gibberish then it drops batches and"
			" marks the server as down");

		init();
		createServerObject(1);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		setLogLevel(LVL_CRIT);
		sender->transferFinished(1, CURLE_OK, 200,
			"gibberish", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(3)", doc["dropped"]["count"].asUInt(), 1u);
		ensure("(4)", containsSubstring(
			doc["last_dropped"]["message"].asString(),
			"invalid response (unparseable)"));

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(5)", doc["dropped"]["count"].asUInt(), 1u);
		ensure_equals("(6)", doc["active_requests"].asUInt(), 1u);
		ensure("(7)", !doc["up"].asBool());
	}

	TEST_METHOD(16) {
		set_test_name("If the response is gibberish then it retries with"
			" a different server");

		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		sender->transferFinished(1, CURLE_OK, 200,
			"gibberish", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 2u);
		ensure("(3)", doc["transfers"]["items"].isMember("3"));
		ensure_equals("(4)", doc["dropped"]["count"].asUInt(), 0u);

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure("(5)", !doc["up"].asBool());

		sender->transferFinished(3, CURLE_OK, 200,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(6)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(7)", doc["accepted"]["count"].asUInt(), 1u);
	}

	TEST_METHOD(17) {
		set_test_name("If the response is parseable but not valid then it drops batches"
			" and marks the server as down");

		init();
		createServerObject(1);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		setLogLevel(LVL_CRIT);
		sender->transferFinished(1, CURLE_OK, 200,
			"{ \"foo\": \"bar\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(3)", doc["dropped"]["count"].asUInt(), 1u);
		ensure("(4)", containsSubstring(
			doc["last_dropped"]["message"].asString(),
			"invalid response (parseable, but does not comply"));

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(5)", doc["dropped"]["count"].asUInt(), 1u);
		ensure_equals("(6)", doc["active_requests"].asUInt(), 1u);
		ensure("(7)", !doc["up"].asBool());
	}

	TEST_METHOD(18) {
		set_test_name("If the response is parseable but not valid then it retries with"
			" a different server");

		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		sender->transferFinished(1, CURLE_OK, 200,
			"{ \"foo\": \"bar\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 2u);
		ensure("(3)", doc["transfers"]["items"].isMember("3"));
		ensure_equals("(4)", doc["dropped"]["count"].asUInt(), 0u);

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure("(5)", !doc["up"].asBool());

		sender->transferFinished(3, CURLE_OK, 200,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(6)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(7)", doc["accepted"]["count"].asUInt(), 1u);
	}

	TEST_METHOD(19) {
		set_test_name("If the response has a non-ok status then it drops batches,"
			" does not mark the server as down and does not retry on a different server");

		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		sender->transferFinished(1, CURLE_OK, 500,
			"{ \"status\": \"error\", \"message\": \"oh no\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(3)", doc["dropped"]["count"].asUInt(), 0u);
		ensure_equals("(4)", doc["rejected"]["count"].asUInt(), 1u);
		ensure("(5)", containsSubstring(
			doc["last_rejected"]["message"].asString(),
			"Could not send data to the Union Station gateway server"));
		ensure("(6)", containsSubstring(
			doc["last_rejected"]["message"].asString(),
			"oh no"));

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(7)", doc["dropped"]["count"].asUInt(), 0u);
		ensure_equals("(8)", doc["rejected"]["count"].asUInt(), 1u);
		ensure("(9)", doc["up"].asBool());
	}

	TEST_METHOD(20) {
		set_test_name("If the response HTTP code is not 200 then it drops batches"
			" and marks the server as down");

		init();
		createServerObject(1);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		setLogLevel(LVL_CRIT);
		sender->transferFinished(1, CURLE_OK, 500,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(3)", doc["dropped"]["count"].asUInt(), 1u);
		ensure("(4)", containsSubstring(
			doc["last_dropped"]["message"].asString(),
			"invalid HTTP code"));

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure_equals("(5)", doc["dropped"]["count"].asUInt(), 1u);
		ensure_equals("(6)", doc["active_requests"].asUInt(), 1u);
		ensure("(7)", !doc["up"].asBool());
	}

	TEST_METHOD(21) {
		set_test_name("If the response HTTP code is not 200 then it retries"
			" on a different server");

		init();
		createServerObject(1);
		createServerObject(2);
		createBatch();
		createBatch();
		sender->schedule(segments);

		Json::Value doc = sender->inspectStateAsJson();
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		sender->transferFinished(1, CURLE_OK, 500,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 2u);
		ensure("(3)", doc["transfers"]["items"].isMember("3"));
		ensure_equals("(4)", doc["dropped"]["count"].asUInt(), 0u);

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		ensure("(5)", !doc["up"].asBool());

		sender->transferFinished(3, CURLE_OK, 200,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(6)", doc["transfers"]["count"].asUInt(), 1u);
		ensure_equals("(7)", doc["accepted"]["count"].asUInt(), 1u);
	}


	/***** Success response handling *****/

	TEST_METHOD(30) {
		set_test_name("If the response contains the recheck_balancer_in key then it updates the corresponding segment's balancer recheck time");
		set_test_name("If the response contains the suspend_sending key then it tells the Segmenter to stop forwarding data for the corresponding Segment for a while");
		set_test_name("If the response contains the recheck_down_gateway_in key then it updates the liveliness check period of the corresponding Server");
	}


	/***** Miscellaneous *****/

	TEST_METHOD(40) {
		set_test_name("It reuses Transfer objects up to a maximum of FREE_TRANSFER_OBJECTS");
	}
}
