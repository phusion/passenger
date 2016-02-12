#include <TestSupport.h>
#include <Core/UnionStation/Context.h>
#include <Core/UnionStation/Transaction.h>
#include <MessageClient.h>
#include <UstRouter/Controller.h>
#include <Utils/MessageIO.h>
#include <Utils/ScopeGuard.h>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>
#include <set>

using namespace Passenger;
using namespace Passenger::UnionStation;
using namespace std;
using namespace oxt;

namespace tut {
	struct Core_UnionStationTest {
		static const unsigned long long YESTERDAY = 1263299017000000ull;  // January 12, 2009, 12:23:37 UTC
		static const unsigned long long TODAY     = 1263385422000000ull;  // January 13, 2009, 12:23:42 UTC
		static const unsigned long long TOMORROW  = 1263471822000000ull;  // January 14, 2009, 12:23:42 UTC
		#define TODAY_TXN_ID "cjb8n-abcd"
		#define TODAY_TIMESTAMP_STR "cftz90m3k0"

		boost::shared_ptr<BackgroundEventLoop> bg;
		boost::shared_ptr<ServerKit::Context> skContext;
		TempDir tmpdir;
		string socketFilename;
		string socketAddress;
		FileDescriptor serverFd;
		VariantMap controllerOptions;
		boost::shared_ptr<UstRouter::Controller> controller;
		ContextPtr context, context2, context3, context4;

		Core_UnionStationTest()
			: tmpdir("tmp.union_station")
		{
			socketFilename = tmpdir.getPath() + "/socket";
			socketAddress = "unix:" + socketFilename;
			setLogLevel(LVL_ERROR);

			controllerOptions.set("ust_router_username", "test");
			controllerOptions.set("ust_router_password", "1234");
			controllerOptions.setBool("ust_router_dev_mode", true);
			controllerOptions.set("ust_router_dump_dir", tmpdir.getPath());

			context = boost::make_shared<Context>(socketAddress, "test", "1234",
				"localhost");
			context2 = boost::make_shared<Context>(socketAddress, "test", "1234",
				"localhost");
			context3 = boost::make_shared<Context>(socketAddress, "test", "1234",
				"localhost");
			context4 = boost::make_shared<Context>(socketAddress, "test", "1234",
				"localhost");
		}

		~Core_UnionStationTest() {
			// Silence error disconnection messages during shutdown.
			setLogLevel(LVL_CRIT);
			shutdown();
			SystemTime::releaseAll();
			setLogLevel(DEFAULT_LOG_LEVEL);
		}

		void init() {
			bg = boost::make_shared<BackgroundEventLoop>(false, true);
			skContext = boost::make_shared<ServerKit::Context>(bg->safe, bg->libuv_loop);
			serverFd.assign(createUnixServer(socketFilename.c_str(), 0, true, __FILE__, __LINE__), NULL, 0);
			controller = make_shared<UstRouter::Controller>(skContext.get(), controllerOptions);
			controller->listen(serverFd);
			bg->start();
		}

		void shutdown() {
			if (bg != NULL) {
				bg->safe->runSync(boost::bind(&UstRouter::Controller::shutdown, controller.get(), true));
				while (getControllerState() != UstRouter::Controller::FINISHED_SHUTDOWN) {
					syscalls::usleep(1000000);
				}
				controller.reset();
				bg->stop();
				bg.reset();
				skContext.reset();
				serverFd.close();
			}
		}

		UstRouter::Controller::State getControllerState() {
			UstRouter::Controller::State result;
			bg->safe->runSync(boost::bind(&Core_UnionStationTest::_getControllerState,
				this, &result));
			return result;
		}

		void _getControllerState(UstRouter::Controller::State *state) {
			*state = controller->serverState;
		}

		string timestampString(unsigned long long timestamp) {
			char str[2 * sizeof(unsigned long long) + 1];
			integerToHexatri<unsigned long long>(timestamp, str);
			return str;
		}

		MessageClient createConnection(bool sendInitCommand = true) {
			MessageClient client;
			vector<string> args;
			client.connect(socketAddress, "test", "1234");
			if (sendInitCommand) {
				client.write("init", "localhost", NULL);
				client.read(args);
			}
			return client;
		}

		void waitForDumpFile(const string &category = "requests") {
			EVENTUALLY(5,
				result = fileExists(getDumpFilePath(category));
			);
		}

		string readDumpFile(const string &category = "requests") {
			waitForDumpFile(category);
			return readAll(getDumpFilePath(category));
		}

		string getDumpFilePath(const string &category = "requests") {
			return tmpdir.getPath() + "/" + category;
		}

		void ensureSubstringInDumpFile(const string &substr, const string &category = "requests") {
			EVENTUALLY(5,
				result = readDumpFile().find(substr) != string::npos;
			);
		}

		void ensureSubstringNotInDumpFile(const string &substr, const string &category = "requests") {
			string path = getDumpFilePath(category);
			SHOULD_NEVER_HAPPEN(100,
				result = fileExists(path) && readAll(path).find(substr) != string::npos;
			);
		}
	};

	DEFINE_TEST_GROUP(Core_UnionStationTest);


	/***** Basic logging tests *****/

	TEST_METHOD(1) {
		set_test_name("Logging to new transaction");
		init();
		SystemTime::forceAll(YESTERDAY);

		TransactionPtr log = context->newTransaction("foobar");
		log->message("hello");
		log->message("world");
		ensure(!context->isNull());
		ensure(!log->isNull());
		log.reset();

		ensureSubstringInDumpFile("hello\n");
		ensureSubstringInDumpFile("world\n");
	}

	TEST_METHOD(2) {
		set_test_name("Logging to continued transaction");
		init();
		SystemTime::forceAll(YESTERDAY);

		TransactionPtr log = context->newTransaction("foobar");
		log->message("message 1");

		TransactionPtr log2 = context2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 2");

		log.reset();
		log2.reset();

		ensureSubstringInDumpFile("message 1\n");
		ensureSubstringInDumpFile("message 2\n");
	}

	TEST_METHOD(3) {
		set_test_name("Logging with different points in time");
		init();
		SystemTime::forceAll(YESTERDAY);

		TransactionPtr log = context->newTransaction("foobar");
		log->message("message 1");
		SystemTime::forceAll(TODAY);
		log->message("message 2");

		SystemTime::forceAll(TOMORROW);
		TransactionPtr log2 = context2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 3");

		TransactionPtr log3 = context3->newTransaction("foobar");
		log3->message("message 4");

		log.reset();
		log2.reset();
		log3.reset();

		ensureSubstringInDumpFile(timestampString(YESTERDAY) + " 1 message 1\n");
		ensureSubstringInDumpFile(timestampString(TODAY) + " 2 message 2\n");
		ensureSubstringInDumpFile(timestampString(TOMORROW) + " 4 message 3\n");
		ensureSubstringInDumpFile(timestampString(TOMORROW) + " 1 message 4\n");
	}

	TEST_METHOD(4) {
		set_test_name("newTransaction() and continueTransaction() log an ATTACH message, "
			"while destroying a Transaction logs a DETACH message");
		init();
		SystemTime::forceAll(YESTERDAY);

		TransactionPtr log = context->newTransaction("foobar");

		SystemTime::forceAll(TODAY);
		TransactionPtr log2 = context2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2.reset();

		SystemTime::forceAll(TOMORROW);
		log.reset();

		ensureSubstringInDumpFile(timestampString(YESTERDAY) + " 0 ATTACH\n");
		ensureSubstringInDumpFile(timestampString(TODAY) + " 1 ATTACH\n");
		ensureSubstringInDumpFile(timestampString(TODAY) + " 2 DETACH\n");
		ensureSubstringInDumpFile(timestampString(TOMORROW) + " 3 DETACH\n");
	}

	TEST_METHOD(5) {
		set_test_name("newTransaction() generates a new ID, while "
			"continueTransaction() reuses the ID");
		init();

		TransactionPtr log = context->newTransaction("foobar");
		TransactionPtr log2 = context2->newTransaction("foobar");
		TransactionPtr log3 = context3->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		TransactionPtr log4 = context4->continueTransaction(log2->getTxnId(),
			log2->getGroupName(), log2->getCategory());

		ensure_equals(log->getTxnId(), log3->getTxnId());
		ensure_equals(log2->getTxnId(), log4->getTxnId());
		ensure(log->getTxnId() != log2->getTxnId());
	}

	TEST_METHOD(6) {
		set_test_name("An empty Transaction doesn't do anything");
		init();

		{
			UnionStation::Transaction log;
			ensure(log.isNull());
			log.message("hello world");
		}
		SHOULD_NEVER_HAPPEN(100,
			result = fileExists(getDumpFilePath());
		);
	}

	TEST_METHOD(7) {
		set_test_name("An empty Context doesn't do anything");
		UnionStation::Context context;
		init();
		ensure(context.isNull());

		TransactionPtr log = context.newTransaction("foo");
		ensure(log->isNull());
		log->message("hello world");
		log.reset();
		SHOULD_NEVER_HAPPEN(100,
			result = fileExists(getDumpFilePath());
		);
	}


	/***** Connection handling *****/

	TEST_METHOD(11) {
		set_test_name("newTransaction() does not reconnect to the server for a short"
			" period of time if connecting failed");
		init();
		context->setReconnectTimeout(60 * 1000000);

		SystemTime::forceAll(TODAY);
		shutdown();
		ensure(context->newTransaction("foobar")->isNull());

		SystemTime::forceAll(TODAY + 30 * 1000000);
		init();
		ensure(context->newTransaction("foobar")->isNull());

		SystemTime::forceAll(TODAY + 61 * 1000000);
		ensure(!context->newTransaction("foobar")->isNull());
	}

	TEST_METHOD(12) {
		set_test_name("If the UstRouter crashed and was restarted then"
			" newTransaction() and continueTransaction() print a warning and return"
			" a null log object. One of the next newTransaction()/continueTransaction()"
			" calls will reestablish the connection when the connection timeout"
			" has passed");
		init();
		SystemTime::forceAll(TODAY);
		TransactionPtr log, log2;

		log = context->newTransaction("foobar");
		context2->continueTransaction(log->getTxnId(), "foobar");
		log.reset(); // Check connection back into the pool.
		shutdown();
		init();

		log = context->newTransaction("foobar");
		ensure("(1)", log->isNull());
		log2 = context2->continueTransaction("some-id", "foobar");
		ensure("(2)", log2->isNull());

		SystemTime::forceAll(TODAY + 60000000);
		log = context->newTransaction("foobar");
		ensure("(3)", !log->isNull());
		log2 = context2->continueTransaction(log->getTxnId(), "foobar");
		ensure("(4)", !log2->isNull());
		log2->message("hello");
		log.reset();
		log2.reset();

		ensureSubstringInDumpFile("hello\n");
	}

	TEST_METHOD(13) {
		set_test_name("continueTransaction() does not reconnect to the server for a short"
			" period of time if connecting failed");
		init();
		context->setReconnectTimeout(60 * 1000000);
		context2->setReconnectTimeout(60 * 1000000);

		SystemTime::forceAll(TODAY);
		TransactionPtr log = context->newTransaction("foobar");
		ensure("(1)", !log->isNull());
		ensure("(2)", !context2->continueTransaction(log->getTxnId(), "foobar")->isNull());
		shutdown();
		ensure("(3)", context2->continueTransaction(log->getTxnId(), "foobar")->isNull());

		SystemTime::forceAll(TODAY + 30 * 1000000);
		init();
		ensure("(3)", context2->continueTransaction(log->getTxnId(), "foobar")->isNull());

		SystemTime::forceAll(TODAY + 61 * 1000000);
		ensure("(4)", !context2->continueTransaction(log->getTxnId(), "foobar")->isNull());
	}

	TEST_METHOD(14) {
		set_test_name("If a client disconnects from the UstRouter then all its"
			" transactions that are no longer referenced and have crash protection enabled"
			" will be closed and written to the sink");
		init();

		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;

		SystemTime::forceAll(TODAY);

		// Create a new transaction
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", "true", NULL);
		client1.read(args);

		// Continue previous transaction, log data to it, disconnect without closing it
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", NULL);
		client2.write("log", TODAY_TXN_ID, "1000", NULL);
		client2.writeScalar("hello world");
		client2.write("ping", NULL);
		client2.read(args);
		client2.disconnect();

		// The transaction still has one reference open, so should not yet be flushed yet
		SHOULD_NEVER_HAPPEN(100,
			result = fileExists(getDumpFilePath());
		);

		client1.disconnect();
		ensureSubstringInDumpFile("hello world");
	}

	TEST_METHOD(15) {
		set_test_name("If a client disconnects from the UstRouter then all its"
			" transactions that are no longer referenced and don't have crash"
			" protection enabled will be closed and discarded");
		init();

		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;

		SystemTime::forceAll(TODAY);

		// Open new transaction with crash protection disabled
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", "true", NULL);
		client1.read(args);

		// Continue previous transaction, then disconnect without closing it
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", "true", NULL);
		client2.read(args);
		client2.disconnect();

		// Disconnect client 1 too. Now all references to the transaction are gone
		client1.disconnect();

		SHOULD_NEVER_HAPPEN(100,
			result = fileExists(getDumpFilePath());
		);
	}


	/***** Shutdown behavior *****/

	TEST_METHOD(16) {
		set_test_name("Upon server shutdown, all transaction that have crash protection "
			" enabled will be closed and written to the sink");
		init();

		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;

		SystemTime::forceAll(TODAY);

		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", NULL);
		client1.write("log", TODAY_TXN_ID, "1000", NULL);
		client1.writeScalar("hello");
		client1.write("ping", NULL);
		client1.read(args);

		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true",  NULL);
		client2.write("log", TODAY_TXN_ID, "1000", NULL);
		client2.writeScalar("world");
		client2.write("ping", NULL);
		client2.read(args);

		shutdown();
		ensureSubstringInDumpFile("hello");
		ensureSubstringInDumpFile("world");
	}

	TEST_METHOD(17) {
		set_test_name("Upon server shutdown, all transaction that don't have crash"
			" protection enabled will be discarded");
		init();

		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;

		SystemTime::forceAll(TODAY);

		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", NULL);
		client1.write("log", TODAY_TXN_ID, "1000", NULL);
		client1.writeScalar("hello");
		client1.write("ping", NULL);
		client1.read(args);

		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", NULL);
		client2.write("log", TODAY_TXN_ID, "1000", NULL);
		client2.writeScalar("world");
		client2.write("ping", NULL);
		client2.read(args);

		shutdown();
		SHOULD_NEVER_HAPPEN(100,
			result = fileExists(getDumpFilePath());
		);
	}


	/***** Miscellaneous *****/

	TEST_METHOD(20) {
		set_test_name("A transaction's data is not written out by the server"
			" until the transaction is fully closed");
		init();
		SystemTime::forceAll(YESTERDAY);
		vector<string> args;

		TransactionPtr log = context->newTransaction("foobar");
		log->message("hello world");

		TransactionPtr log2 = context2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 2");
		log2.reset();

		SHOULD_NEVER_HAPPEN(100,
			result = fileExists(getDumpFilePath());
		);
	}

	TEST_METHOD(21) {
		set_test_name("One can supply a custom node name per openTransaction command");
		init();
		MessageClient client1 = createConnection();
		vector<string> args;

		SystemTime::forceAll(TODAY);

		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "remote", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", NULL);
		client1.write("closeTransaction", TODAY_TXN_ID, TODAY_TIMESTAMP_STR, NULL);
		client1.disconnect();

		waitForDumpFile();
	}

	TEST_METHOD(22) {
		set_test_name("A transaction is only written to the sink if it passes all given filters");
		init();
		SystemTime::forceAll(YESTERDAY);

		TransactionPtr log = context->newTransaction("foobar", "requests", "-",
			"uri == \"/foo\""
			"\1"
			"uri != \"/bar\"");
		log->message("URI: /foo");
		log->message("transaction 1");
		log.reset();

		log = context->newTransaction("foobar", "requests", "-",
			"uri == \"/foo\""
			"\1"
			"uri == \"/bar\"");
		log->message("URI: /foo");
		log->message("transaction 2");
		log.reset();

		ensureSubstringInDumpFile("transaction 1\n");
		ensureSubstringNotInDumpFile("transaction 2\n");
	}

	/************************************/
}
