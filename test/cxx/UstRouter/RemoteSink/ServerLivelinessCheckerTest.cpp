#include "TestSupport.h"
#include <SafeLibev.h>
#include <BackgroundEventLoop.h>
#include <UstRouter/RemoteSink/ServerLivelinessChecker.h>

using namespace Passenger;
using namespace Passenger::UstRouter;
using namespace Passenger::UstRouter::RemoteSink;
using namespace std;

namespace tut {
	struct UstRouter_RemoteSink_ServerLivelinessCheckerTest {
		class TestServerLivelinessChecker: public ServerLivelinessChecker {
		protected:
			virtual bool shouldFailCheckInitiation(const ServerPtr &server) const {
				return m_shouldFailCheckInitiation;
			}

		public:
			bool m_shouldFailCheckInitiation;

			TestServerLivelinessChecker(Context *context)
				: ServerLivelinessChecker(context),
				  m_shouldFailCheckInitiation(false)
				{ }

			void checkFinished(const ServerPtr &server, CURLcode code,
				long httpCode, const string &body, const char *errorBuf)
			{
				ServerLivelinessChecker::checkFinished(server,
					code, httpCode, body, errorBuf);
			}
		};

		BackgroundEventLoop bg;
		Context context;
		Segment::SmallServerList servers;
		TestServerLivelinessChecker *checker;

		UstRouter_RemoteSink_ServerLivelinessCheckerTest()
			: bg(false, true),
			  context(bg.safe->getLoop())
		{
			checker = NULL;
		}

		~UstRouter_RemoteSink_ServerLivelinessCheckerTest() {
			delete checker;
			SystemTime::releaseAll();
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		void init() {
			checker = new TestServerLivelinessChecker(&context);
		}

		struct ev_loop *getLoop() {
			return bg.safe->getLoop();
		}

		ServerPtr createServer(unsigned int number, bool up = true) {
			ServerPtr server = boost::make_shared<Server>(number,
				"http://" + toString(number), 1);
			if (!up) {
				server->reportRequestBegin(ev_now(getLoop()));
				server->reportRequestDropped(1, ev_now(getLoop()), "error");
				ensure("Server " + toString(number) + " is down", !server->isUp());
			}
			servers.push_back(server);
			return server;
		}

		void registerServer(const ServerPtr &server) {
			Segment::SmallServerList servers;
			servers.push_back(server);
			checker->registerServers(servers);
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(UstRouter_RemoteSink_ServerLivelinessCheckerTest, 70);


	/***** Initial state *****/

	TEST_METHOD(1) {
		set_test_name("It is empty");

		init();
		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["servers"]["count"].asUInt(), 0u);
		ensure_equals("(2)", doc["checks_in_progress"]["count"].asUInt(), 0u);
		ensure("(3)", doc["last_ping_error"].isNull());
		ensure("(4)", doc["next_liveliness_check_time"].isNull());
	}


	/***** Registering into an empty checker *****/

	TEST_METHOD(5) {
		set_test_name("If the server is up then it doesn't schedule a liveliness check");

		init();
		registerServer(createServer(1));
		Json::Value doc = checker->inspectStateAsJson();
		ensure(doc["next_liveliness_check_time"].isNull());
	}

	TEST_METHOD(6) {
		set_test_name("If the server is down then it schedules a liveliness check");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000);
		init();

		registerServer(createServer(1, false));
		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			Server::DEFAULT_LIVELINESS_CHECK_PERIOD + 5u);
	}


	/***** Registering into a non-empty checker *****/

	TEST_METHOD(10) {
		set_test_name("If the server is up, and there was no liveliness"
			" check scheduled, then it won't schedule a new liveliness check");

		init();
		registerServer(createServer(1));
		registerServer(createServer(2));
		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", doc["next_liveliness_check_time"].isNull());
	}

	TEST_METHOD(11) {
		set_test_name("If the server is up, and there was already a liveliness"
			" check scheduled, then it will remain scheduled at the same time");

		init();
		registerServer(createServer(1, false));
		Json::Value doc = checker->inspectStateAsJson();
		unsigned long long t = doc["next_liveliness_check_time"]["timestamp"].asUInt64();

		registerServer(createServer(2));
		doc = checker->inspectStateAsJson();
		ensure_equals("(1)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			t);
	}

	TEST_METHOD(12) {
		set_test_name("If the server is down, and the there isn't already a liveliness"
			" check scheduled, then a new liveliness check is scheduled");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000);
		init();

		registerServer(createServer(1));
		registerServer(createServer(2, false));
		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			Server::DEFAULT_LIVELINESS_CHECK_PERIOD + 5u);
	}

	TEST_METHOD(13) {
		set_test_name("If the server is down, and the next liveliness check time of that"
			" server is earlier than the previously scheduled liveliness check time,"
			" then the next liveliness check is scheduled on that time");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();

		registerServer(createServer(1, false));
		Json::Value doc = checker->inspectStateAsJson();
		unsigned long long t = doc["next_liveliness_check_time"]["timestamp"].asUInt64();

		ServerPtr server = createServer(2, false);
		server->setLivelinessCheckPeriod(Server::DEFAULT_LIVELINESS_CHECK_PERIOD / 2);
		registerServer(server);

		doc = checker->inspectStateAsJson();
		ensure(doc["next_liveliness_check_time"]["timestamp"].asUInt64() < t);
	}

	TEST_METHOD(14) {
		set_test_name("If the server is down, and the next liveliness check time of that"
			" server is later than the previously scheduled liveliness check time,"
			" then the next liveliness check time remains unchanged");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();

		registerServer(createServer(1, false));
		Json::Value doc = checker->inspectStateAsJson();
		unsigned long long t = doc["next_liveliness_check_time"]["timestamp"].asUInt64();

		ServerPtr server = createServer(2, false);
		server->setLivelinessCheckPeriod(Server::DEFAULT_LIVELINESS_CHECK_PERIOD * 2);
		registerServer(server);

		doc = checker->inspectStateAsJson();
		ensure_equals(doc["next_liveliness_check_time"]["timestamp"].asUInt64(), t);
	}


	/***** Initiating checks *****/

	static vector<unsigned int> getServerNumbers(const Json::Value &doc) {
		const Json::Value items = doc["checks_in_progress"]["items"];
		Json::Value::const_iterator it, end = items.end();
		vector<unsigned int> serverNumbers;
		for (it = items.begin(); it != end; it++) {
			const Json::Value item = *it;
			serverNumbers.push_back(item["server_number"].asUInt());
		}
		std::sort(serverNumbers.begin(), serverNumbers.end());
		return serverNumbers;
	}

	TEST_METHOD(20) {
		set_test_name("It initiates checks for all servers that are down and whose"
			" next liveliness check time has passed");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		createServer(1, false);
		createServer(2, false);
		createServer(3);
		checker->registerServers(servers);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		registerServer(createServer(4, false));

		ev_set_time(getLoop(), 1 + Server::DEFAULT_LIVELINESS_CHECK_PERIOD);
		SystemTime::forceAll((1 + Server::DEFAULT_LIVELINESS_CHECK_PERIOD) * 1000000ull);
		checker->checkEligibleServers();

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_in_progress"]["count"].asUInt(), 2u);

		vector<unsigned int> serverNumbers = getServerNumbers(doc);
		ensure_equals("(2)", serverNumbers.size(), 2u);
		ensure_equals("(3)", serverNumbers[0], 1u);
		ensure_equals("(4)", serverNumbers[1], 2u);
	}

	TEST_METHOD(21) {
		set_test_name("It does not initiate checks for all servers that are already being checked");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		createServer(1, false);
		createServer(2, false);
		checker->registerServers(servers);

		// Initiate checking for servers 1 and 2
		ev_set_time(getLoop(), 1 + Server::DEFAULT_LIVELINESS_CHECK_PERIOD);
		SystemTime::forceAll((1 + Server::DEFAULT_LIVELINESS_CHECK_PERIOD) * 1000000ull);
		checker->checkEligibleServers();
		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_in_progress"]["count"].asUInt(), 2u);

		// Register a new server, initiate checking for it and verify that we
		// don't recheck servers 1 and 2 (since they're still in progress)
		registerServer(createServer(3, false));
		ev_set_time(getLoop(), (1 + Server::DEFAULT_LIVELINESS_CHECK_PERIOD) * 2);
		SystemTime::forceAll((1 + Server::DEFAULT_LIVELINESS_CHECK_PERIOD) * 2 * 1000000ull);
		checker->checkEligibleServers();

		doc = checker->inspectStateAsJson();
		ensure_equals("(2)", doc["checks_in_progress"]["count"].asUInt(), 3u);

		vector<unsigned int> serverNumbers = getServerNumbers(doc);
		ensure_equals("(3)", serverNumbers.size(), 3u);
		ensure_equals("(4)", serverNumbers[0], 1u);
		ensure_equals("(5)", serverNumbers[1], 2u);
		ensure_equals("(6)", serverNumbers[2], 3u);
	}


	/***** Response handling *****/

	TEST_METHOD(25) {
		set_test_name("If curl failed to initiate: it retries the liveliness check later");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		checker->m_shouldFailCheckInitiation = true;
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 6);
		SystemTime::forceAll(6000000ull);
		setLogLevel(LVL_CRIT);
		checker->checkEligibleServers();

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", !server->isUp());
		ensure("(2)", !server->isBeingCheckedForLiveliness());
		ensure_equals("(3)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			10u);
	}

	TEST_METHOD(26) {
		set_test_name("If curl failed to initiate: it logs the error");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		checker->m_shouldFailCheckInitiation = true;
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 6);
		SystemTime::forceAll(6000000ull);
		setLogLevel(LVL_CRIT);
		checker->checkEligibleServers();

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", doc["last_error"]["message"].isString());
	}

	TEST_METHOD(27) {
		set_test_name("If curl failed to initiate: it updates the counters and timestamps");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		checker->m_shouldFailCheckInitiation = true;
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		setLogLevel(LVL_CRIT);
		checker->checkEligibleServers();

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_initiated"].asUInt(), 1u);
		ensure_equals("(2)", doc["checks_finished"].asUInt(), 1u);
		ensure_equals("(3)", doc["last_error"]["timestamp"].asUInt64(), 2u);
	}

	TEST_METHOD(28) {
		set_test_name("If curl perform failed: it retries the liveliness check later");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_COULDNT_CONNECT, 0, "", "oh no");

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", !server->isUp());
		ensure("(2)", !server->isBeingCheckedForLiveliness());
		ensure_equals("(3)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			5u);
	}

	TEST_METHOD(29) {
		set_test_name("If curl perform failed: it logs the error");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_COULDNT_CONNECT, 0, "", "oh no");

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["last_error"]["message"].asString(),
			"It appears to be down"));
		ensure("(2)", containsSubstring(doc["last_error"]["message"].asString(),
			"oh no"));

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure("(3)", doc["last_drop_error"].isObject());
	}

	TEST_METHOD(30) {
		set_test_name("If curl perform failed: it updates the failure counters and timestamps");

		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_COULDNT_CONNECT, 0, "", "oh no");

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_initiated"].asUInt(), 1u);
		ensure_equals("(2)", doc["checks_finished"].asUInt(), 1u);
		ensure_equals("(3)", doc["last_error"]["timestamp"].asUInt64(), 2u);

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure_equals("(4)", doc["last_liveliness_check_error"]["timestamp"].asUInt64(), 2u);
	}

	TEST_METHOD(35) {
		set_test_name("If the server responded with gibberish: it retries the"
			" liveliness check later");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", !server->isUp());
		ensure("(2)", !server->isBeingCheckedForLiveliness());
		ensure_equals("(3)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			5u);
	}

	TEST_METHOD(36) {
		set_test_name("If the server responded with gibberish: it logs the error");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["last_error"]["message"].asString(),
			"unparseable"));

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure("(2)", doc["last_drop_error"].isObject());
	}

	TEST_METHOD(37) {
		set_test_name("If the server responded with gibberish: it updates the"
			" failure counters and timestamps");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "foo", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_initiated"].asUInt(), 1u);
		ensure_equals("(2)", doc["checks_finished"].asUInt(), 1u);
		ensure_equals("(3)", doc["last_error"]["timestamp"].asUInt64(), 2u);

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure_equals("(4)", doc["last_liveliness_check_error"]["timestamp"].asUInt64(), 2u);
	}

	TEST_METHOD(40) {
		set_test_name("If the server returned a parsable but invalid response:"
			" it retries the liveliness check later");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", !server->isUp());
		ensure("(2)", !server->isBeingCheckedForLiveliness());
		ensure_equals("(3)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			5u);
	}

	TEST_METHOD(41) {
		set_test_name("If the server returned a parsable but invalid response:"
			" it logs the error");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["last_error"]["message"].asString(),
			"parseable, but does not comply"));

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure("(2)", doc["last_drop_error"].isObject());
	}

	TEST_METHOD(42) {
		set_test_name("If the server returned a parsable but invalid response:"
			" it updates the failure counters and timestamps");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{}", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_initiated"].asUInt(), 1u);
		ensure_equals("(2)", doc["checks_finished"].asUInt(), 1u);
		ensure_equals("(3)", doc["last_error"]["timestamp"].asUInt64(), 2u);

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure_equals("(4)", doc["last_liveliness_check_error"]["timestamp"].asUInt64(), 2u);
	}

	TEST_METHOD(45) {
		set_test_name("If the server returned a non-200 response: it"
			" retries the liveliness check later");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 404, "{ \"status\": \"ok\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", !server->isUp());
		ensure("(2)", !server->isBeingCheckedForLiveliness());
		ensure_equals("(3)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			5u);
	}

	TEST_METHOD(46) {
		set_test_name("If the server returned a non-200 response: it"
			" logs the error");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 404, "{ \"status\": \"ok\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["last_error"]["message"].asString(),
			"invalid HTTP code"));

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure("(2)", doc["last_drop_error"].isObject());
	}

	TEST_METHOD(47) {
		set_test_name("If the server returned a non-200 response: it"
			" updates the failure counters and timestamps");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 404, "{ \"status\": \"ok\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_initiated"].asUInt(), 1u);
		ensure_equals("(2)", doc["checks_finished"].asUInt(), 1u);
		ensure_equals("(3)", doc["last_error"]["timestamp"].asUInt64(), 2u);

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure_equals("(4)", doc["last_liveliness_check_error"]["timestamp"].asUInt64(), 2u);
	}

	TEST_METHOD(50) {
		set_test_name("If the server returned a response with non-ok status:"
			" it retries the liveliness check later");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{ \"status\": \"error\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", !server->isUp());
		ensure("(2)", !server->isBeingCheckedForLiveliness());
		ensure_equals("(3)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			5u);
	}

	TEST_METHOD(51) {
		set_test_name("If the server returned a response with non-ok status:"
			" it logs the error");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{ \"status\": \"error\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", containsSubstring(doc["last_error"]["message"].asString(),
			"is down"));
		ensure("(2)", containsSubstring(doc["last_error"]["message"].asString(),
			"\"error\""));

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure("(3)", doc["last_drop_error"].isObject());
	}

	TEST_METHOD(52) {
		set_test_name("If the server returned a response with non-ok status:"
			" it updates the failure counters and timestamps");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{ \"status\": \"error\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_initiated"].asUInt(), 1u);
		ensure_equals("(2)", doc["checks_finished"].asUInt(), 1u);
		ensure_equals("(3)", doc["last_error"]["timestamp"].asUInt64(), 2u);

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure_equals("(4)", doc["last_liveliness_check_error"]["timestamp"].asUInt64(), 2u);
	}

	TEST_METHOD(55) {
		set_test_name("If the server returned a response with ok status:"
			" it updates the counters and timestamps");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{ \"status\": \"ok\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)", doc["checks_initiated"].asUInt(), 1u);
		ensure_equals("(2)", doc["checks_finished"].asUInt(), 1u);

		doc = server->inspectStateAsJson(ev_now(getLoop()), SystemTime::getUsec());
		ensure_equals("(4)", doc["last_liveliness_ok_time"]["timestamp"].asUInt64(), 2u);
	}

	TEST_METHOD(56) {
		set_test_name("If the server returned a response with ok status:"
			" it marks the server as 'up'");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{ \"status\": \"ok\" }", NULL);

		ensure("(1)", server->isUp());
		ensure("(2)", !server->isBeingCheckedForLiveliness());
	}

	TEST_METHOD(57) {
		set_test_name("If the server returned a response with ok status:"
			" if there are still down servers then it schedules the next liveliness check");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);
		registerServer(createServer(2, false));

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{ \"status\": \"ok\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure_equals("(1)",
			doc["next_liveliness_check_time"]["timestamp"].asUInt64(),
			5u + Server::DEFAULT_LIVELINESS_CHECK_PERIOD);
	}

	TEST_METHOD(58) {
		set_test_name("If the server returned a response with ok status:"
			" if there are no more down servers then does not schedule the next liveliness check");

		init();
		ev_set_time(getLoop(), 1);
		SystemTime::forceAll(1000000ull);
		init();
		ServerPtr server = createServer(1, false);
		server->setLivelinessCheckPeriod(1);
		registerServer(server);

		ev_set_time(getLoop(), 2);
		SystemTime::forceAll(2000000ull);
		checker->checkEligibleServers();
		checker->checkFinished(server, CURLE_OK, 200, "{ \"status\": \"ok\" }", NULL);

		Json::Value doc = checker->inspectStateAsJson();
		ensure("(1)", doc["next_liveliness_check_time"].isNull());
	}


	/***** Miscellaneous *****/

	TEST_METHOD(60) {
		set_test_name("If a registered server object has no more references, then"
			" the liveliness checker eventually purges it");

		init();
		registerServer(createServer(1));
		registerServer(createServer(2));
		servers.clear();
		vector<ServerPtr> servers = checker->getServersAndCleanupStale();
		ensure(servers.empty());
	}
}
