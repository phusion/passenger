#include <TestSupport.h>
#include <Core/TelemetryCollector.h>

using namespace std;
using namespace boost;
using namespace Passenger;
using namespace Passenger::Core;

namespace tut {
	struct Core_TelemetryCollectorTest: public TestBase {
		class MyTelemetryCollector: public TelemetryCollector {
		private:
			virtual TelemetryData collectTelemetryData(bool isFinalRun) const {
				return mockTelemetryData;
			}

			virtual CURLcode performCurlAction(CURL *curl, const char *lastErrorMessage,
				const string &requestBody, string &responseData, long &responseCode)
			{
				Json::Reader reader;
				if (!reader.parse(requestBody, lastRequestBody)) {
					throw RuntimeException("Request body parse error: "
						+ reader.getFormattedErrorMessages());
				}
				responseData = mockResponse.toStyledString();
				responseCode = mockResponseCode;
				return mockCurlResult;
			}

		public:
			TelemetryData mockTelemetryData;
			long mockResponseCode;
			Json::Value mockResponse;
			CURLcode mockCurlResult;
			Json::Value lastRequestBody;

			MyTelemetryCollector(const Schema &schema,
				const Json::Value &initialConfig = Json::Value(),
				const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
				: TelemetryCollector(schema, initialConfig, translator),
				  mockResponseCode(200),
				  mockCurlResult(CURLE_OK)
			{
				mockResponse["data_processed"] = true;
			}
		};

		TelemetryCollector::Schema schema;
		Json::Value config;
		MyTelemetryCollector *col;

		Core_TelemetryCollectorTest()
			: col(NULL)
			{ }

		~Core_TelemetryCollectorTest() {
			delete col;
			SystemTime::releaseAll();
		}

		void init() {
			col = new MyTelemetryCollector(schema, config);
			col->controllers.resize(2, NULL);
			col->mockTelemetryData.requestsHandled.resize(2, 0);
			col->initialize();
		}
	};

	DEFINE_TEST_GROUP(Core_TelemetryCollectorTest);


	/***** Passing request information to the app *****/

	TEST_METHOD(1) {
		set_test_name("On first run, it sends the number of requests handled so far");

		init();

		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();

		col->runOneCycle();
		ensure_equals(col->lastRequestBody["requests_handled"].asUInt(), 90u + 150u);
	}

	TEST_METHOD(2) {
		set_test_name("On first run, it sends begin_time = object creation time, end_time = now");

		SystemTime::forceAll(1000000);
		init();

		SystemTime::forceAll(2000000);
		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();

		col->runOneCycle();
		ensure_equals(col->lastRequestBody["begin_time"].asUInt(), 1u);
		ensure_equals(col->lastRequestBody["end_time"].asUInt(), 2u);
	}

	TEST_METHOD(5) {
		set_test_name("On subsequent runs, it sends the number of requests handled since last run");

		init();

		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		col->mockTelemetryData.requestsHandled[0] = 120;
		col->mockTelemetryData.requestsHandled[1] = 180;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		ensure_equals(col->lastRequestBody["requests_handled"].asUInt(),
			(120u - 90u) + (180u - 150u));
	}

	TEST_METHOD(6) {
		set_test_name("On subsequent runs, it sends begin_time = last send time, end_time = now");

		SystemTime::forceAll(1000000);
		init();

		SystemTime::forceAll(2000000);
		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		SystemTime::forceAll(3000000);
		col->mockTelemetryData.requestsHandled[0] = 120;
		col->mockTelemetryData.requestsHandled[1] = 180;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		ensure_equals(col->lastRequestBody["begin_time"].asUInt(), 2u);
		ensure_equals(col->lastRequestBody["end_time"].asUInt(), 3u);
	}

	TEST_METHOD(7) {
		set_test_name("On subsequent runs, it handles request counter overflows");

		init();

		col->mockTelemetryData.requestsHandled[0] = std::numeric_limits<boost::uint64_t>::max();
		col->mockTelemetryData.requestsHandled[1] = std::numeric_limits<boost::uint64_t>::max() - 1;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		col->mockTelemetryData.requestsHandled[0] = 0;
		col->mockTelemetryData.requestsHandled[1] = 2;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		ensure_equals(col->lastRequestBody["requests_handled"].asUInt(), 1u + 4u);
	}

	TEST_METHOD(10) {
		set_test_name("If the server responds with data_processed = false,"
			" then the next run sends telemetry relative to the last time"
			" the server responded with data_processed = true");

		SystemTime::forceAll(1000000);
		init();

		SystemTime::forceAll(2000000);
		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		SystemTime::forceAll(3000000);
		col->mockTelemetryData.requestsHandled[0] = 120;
		col->mockTelemetryData.requestsHandled[1] = 180;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->mockResponse["data_processed"] = false;
		col->runOneCycle();

		SystemTime::forceAll(4000000);
		col->mockTelemetryData.requestsHandled[0] = 160;
		col->mockTelemetryData.requestsHandled[1] = 200;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->mockResponse["data_processed"] = true;
		col->runOneCycle();

		ensure_equals(col->lastRequestBody["requests_handled"].asUInt(),
			(160u - 90u) + (200u - 150u));
		ensure_equals(col->lastRequestBody["begin_time"].asUInt(), 2u);
		ensure_equals(col->lastRequestBody["end_time"].asUInt(), 4u);
	}

	TEST_METHOD(11) {
		set_test_name("If the server responds with an error,"
			" then the next run sends telemetry relative to the last time"
			" the server responded with data_processed = true");

		SystemTime::forceAll(1000000);
		init();

		SystemTime::forceAll(2000000);
		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->runOneCycle();

		SystemTime::forceAll(3000000);
		col->mockTelemetryData.requestsHandled[0] = 120;
		col->mockTelemetryData.requestsHandled[1] = 180;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->mockResponseCode = 502;
		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			// If the user did not customize the test's log level,
			// then we'll want to tone down the noise.
			LoggingKit::setLevel(LoggingKit::CRIT);
		}
		col->runOneCycle();
		if (defaultLogLevel == (LoggingKit::Level) DEFAULT_LOG_LEVEL) {
			LoggingKit::setLevel((LoggingKit::Level) DEFAULT_LOG_LEVEL);
		}

		SystemTime::forceAll(4000000);
		col->mockTelemetryData.requestsHandled[0] = 160;
		col->mockTelemetryData.requestsHandled[1] = 200;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->mockResponseCode = 200;
		col->runOneCycle();

		ensure_equals(col->lastRequestBody["requests_handled"].asUInt(),
			(160u - 90u) + (200u - 150u));
		ensure_equals(col->lastRequestBody["begin_time"].asUInt(), 2u);
		ensure_equals(col->lastRequestBody["end_time"].asUInt(), 4u);
	}

	TEST_METHOD(12) {
		set_test_name("If the server responds with 'backoff',"
			"then the next run is scheduled according to the server-provided backoff");

		init();

		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();
		col->mockResponse["backoff"] = 555;

		ensure_equals(col->runOneCycle(), 555u);
	}

	TEST_METHOD(13) {
		set_test_name("If the server responds with no 'backoff',"
			"then the next run is scheduled according to the interval config");

		init();

		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();

		ensure_equals(col->runOneCycle(), 0u);
	}

	TEST_METHOD(15) {
		set_test_name("It sends no data when disabled");

		config["disabled"] = true;
		init();

		col->mockTelemetryData.requestsHandled[0] = 90;
		col->mockTelemetryData.requestsHandled[1] = 150;
		col->mockTelemetryData.timestamp = SystemTime::getMonotonicUsec();

		ensure_equals(col->runOneCycle(), 0u);
		ensure(col->lastRequestBody.isNull());
	}
}
