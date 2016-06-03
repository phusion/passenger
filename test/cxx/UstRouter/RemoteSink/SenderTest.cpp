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
		public:
			TestSender(Context *context, const VariantMap &options)
				: Sender(context, options)
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
		cout << doc.toStyledString() << endl;
		ensure_equals("(1)", doc["transfers"]["count"].asUInt(), 2u);

		sender->transferFinished(1, CURLE_OK, 200,
			"{ \"status\": \"ok\" }", NULL);
		doc = sender->inspectStateAsJson();
		ensure_equals("(2)", doc["transfers"]["count"].asUInt(), 1u);

		doc = segment->servers[0]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
		//ensure_equals(doc[""]);

		doc = segment->servers[1]->inspectStateAsJson(ev_now(getLoop()),
			SystemTime::getUsec());
	}


	/***** Error handling *****/

	TEST_METHOD(5) {
		set_test_name("It drops batches if there are no up servers available");
		set_test_name("It drops batches if the memory limit has been reached");
		set_test_name("It drops batches if transfers cannot be initiated");
		set_test_name("If transfers failed to perform then it drops batches and marks the server as down");
		set_test_name("If the response is gibberish then it drops batches and marks the server as down if the response is gibberish");
		set_test_name("If the response is parseable but not valid then it drops batches and does not mark the server as down");
		set_test_name("If the response has a non-ok status then it drops batches and does not mark the server as down");
		set_test_name("If the response HTTP code is not 200 then it drops batches and marks the server as down");
	}


	/***** Success response handling *****/

	TEST_METHOD(20) {
		set_test_name("If the response contains the recheck_balancer_in key then it updates the corresponding segment's balancer recheck time");
		set_test_name("If the response contains the suspend_sending key then it tells the Segmenter to stop forwarding data for the corresponding Segment for a while");
		set_test_name("If the response contains the recheck_down_gateway_in key then it updates the liveliness check period of the corresponding Server");
	}


	/***** Miscellaneous *****/

	TEST_METHOD(30) {
		set_test_name("It reuses Transfer objects up to a maximum of FREE_TRANSFER_OBJECTS");
	}
}
