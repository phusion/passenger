#include <TestSupport.h>
#include <UnionStation/Core.h>
#include <UnionStation/Transaction.h>
#include <MessageClient.h>
#include <agents/LoggingAgent/LoggingServer.h>
#include <Utils/MessageIO.h>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>
#include <set>

using namespace Passenger;
using namespace Passenger::UnionStation;
using namespace std;
using namespace oxt;

namespace tut {
	struct UnionStationTest {
		static const unsigned long long YESTERDAY = 1263299017000000ull;  // January 12, 2009, 12:23:37 UTC
		static const unsigned long long TODAY     = 1263385422000000ull;  // January 13, 2009, 12:23:42 UTC
		static const unsigned long long TOMORROW  = 1263471822000000ull;  // January 14, 2009, 12:23:42 UTC
		#define TODAY_TXN_ID "cjb8n-abcd"
		#define TODAY_TIMESTAMP_STR "cftz90m3k0"
		
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		string socketFilename;
		string socketAddress;
		string dumpFile;
		AccountsDatabasePtr accountsDatabase;
		ev::dynamic_loop eventLoop;
		FileDescriptor serverFd;
		LoggingServerPtr server;
		boost::shared_ptr<oxt::thread> serverThread;
		CorePtr core, core2, core3, core4;
		
		UnionStationTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			socketFilename = generation->getPath() + "/logging.socket";
			socketAddress = "unix:" + socketFilename;
			dumpFile = generation->getPath() + "/log.txt";
			accountsDatabase = ptr(new AccountsDatabase());
			accountsDatabase->add("test", "1234", false);
			setLogLevel(-1);
			
			startLoggingServer();
			core = make_shared<Core>(socketAddress, "test", "1234",
				"localhost");
			core2 = make_shared<Core>(socketAddress, "test", "1234",
				"localhost");
			core3 = make_shared<Core>(socketAddress, "test", "1234",
				"localhost");
			core4 = make_shared<Core>(socketAddress, "test", "1234",
				"localhost");
		}
		
		~UnionStationTest() {
			stopLoggingServer();
			SystemTime::releaseAll();
			setLogLevel(0);
		}
		
		void startLoggingServer(const boost::function<void ()> &initFunc = boost::function<void ()>()) {
			VariantMap options;
			options.set("analytics_dump_file", dumpFile);
			serverFd = createUnixServer(socketFilename.c_str());
			server = ptr(new LoggingServer(eventLoop,
				serverFd, accountsDatabase, options));
			if (initFunc) {
				initFunc();
			}
			serverThread = ptr(new oxt::thread(
				boost::bind(&UnionStationTest::runLoop, this)
			));
		}
		
		void stopLoggingServer(bool destroy = true) {
			if (serverThread != NULL) {
				MessageClient client;
				client.connect(socketAddress, "test", "1234");
				client.write("exit", "immediately", NULL);
				joinLoggingServer(destroy);
			}
		}
		
		void joinLoggingServer(bool destroy = true) {
			serverThread->join();
			serverThread.reset();
			if (destroy) {
				server.reset();
			}
			unlink(socketFilename.c_str());
		}
		
		void runLoop() {
			eventLoop.loop();
			serverFd.close();
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

		string readDumpFile() {
			return readAll(dumpFile);
		}
	};
	
	DEFINE_TEST_GROUP(UnionStationTest);
	
	
	/*********** Logging interface tests ***********/
	
	TEST_METHOD(1) {
		// Test logging of new transaction.
		SystemTime::forceAll(YESTERDAY);
		
		TransactionPtr log = core->newTransaction("foobar");
		log->message("hello");
		log->message("world");
		log->flushToDiskAfterClose(true);
		
		ensure(!core->isNull());
		ensure(!log->isNull());
		
		log.reset();
		
		string data = readDumpFile();
		ensure(data.find("hello\n") != string::npos);
		ensure(data.find("world\n") != string::npos);
	}
	
	TEST_METHOD(2) {
		// Test logging of existing transaction.
		SystemTime::forceAll(YESTERDAY);
		
		TransactionPtr log = core->newTransaction("foobar");
		log->message("message 1");
		log->flushToDiskAfterClose(true);
		
		TransactionPtr log2 = core2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 2");
		log2->flushToDiskAfterClose(true);
		
		log.reset();
		log2.reset();
		
		string data = readDumpFile();
		ensure("(1)", data.find("message 1\n") != string::npos);
		ensure("(2)", data.find("message 2\n") != string::npos);
	}
	
	TEST_METHOD(3) {
		// Test logging with different points in time.
		SystemTime::forceAll(YESTERDAY);
		TransactionPtr log = core->newTransaction("foobar");
		log->message("message 1");
		SystemTime::forceAll(TODAY);
		log->message("message 2");
		log->flushToDiskAfterClose(true);
		
		SystemTime::forceAll(TOMORROW);
		TransactionPtr log2 = core2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 3");
		log2->flushToDiskAfterClose(true);
		
		TransactionPtr log3 = core3->newTransaction("foobar");
		log3->message("message 4");
		log3->flushToDiskAfterClose(true);
		
		log.reset();
		log2.reset();
		log3.reset();
		
		string data = readDumpFile();
		ensure("(1)", data.find(timestampString(YESTERDAY) + " 1 message 1\n") != string::npos);
		ensure("(2)", data.find(timestampString(TODAY) + " 2 message 2\n") != string::npos);
		ensure("(3)", data.find(timestampString(TOMORROW) + " 4 message 3\n") != string::npos);
		ensure("(4)", data.find(timestampString(TOMORROW) + " 1 message 4\n") != string::npos);
	}
	
	TEST_METHOD(4) {
		// newTransaction() and continueTransaction() write an ATTACH message
		// to the log file, while UnionStation::Transaction writes a DETACH message upon
		// destruction.
		SystemTime::forceAll(YESTERDAY);
		TransactionPtr log = core->newTransaction("foobar");
		
		SystemTime::forceAll(TODAY);
		TransactionPtr log2 = core2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->flushToDiskAfterClose(true);
		log2.reset();
		
		SystemTime::forceAll(TOMORROW);
		log->flushToDiskAfterClose(true);
		log.reset();
		
		string data = readDumpFile();
		ensure("(1)", data.find(timestampString(YESTERDAY) + " 0 ATTACH\n") != string::npos);
		ensure("(2)", data.find(timestampString(TODAY) + " 1 ATTACH\n") != string::npos);
		ensure("(3)", data.find(timestampString(TODAY) + " 2 DETACH\n") != string::npos);
		ensure("(4)", data.find(timestampString(TOMORROW) + " 3 DETACH\n") != string::npos);
	}
	
	TEST_METHOD(5) {
		// newTransaction() generates a new ID, while continueTransaction()
		// reuses the ID.
		TransactionPtr log = core->newTransaction("foobar");
		TransactionPtr log2 = core2->newTransaction("foobar");
		TransactionPtr log3 = core3->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		TransactionPtr log4 = core4->continueTransaction(log2->getTxnId(),
			log2->getGroupName(), log2->getCategory());
		
		ensure_equals(log->getTxnId(), log3->getTxnId());
		ensure_equals(log2->getTxnId(), log4->getTxnId());
		ensure(log->getTxnId() != log2->getTxnId());
	}
	
	TEST_METHOD(6) {
		// An empty UnionStation::Transaction doesn't do anything.
		UnionStation::Transaction log;
		ensure(log.isNull());
		log.message("hello world");
		ensure_equals(getFileType(dumpFile), FT_NONEXISTANT);
	}
	
	TEST_METHOD(7) {
		// An empty UnionStation::Core doesn't do anything.
		UnionStation::Core core;
		ensure(core.isNull());
		
		TransactionPtr log = core.newTransaction("foo");
		ensure(log->isNull());
		log->message("hello world");
		ensure_equals(getFileType(dumpFile), FT_NONEXISTANT);
	}
	
	TEST_METHOD(11) {
		// newTransaction() does not reconnect to the server for a short
		// period of time if connecting failed
		core->setReconnectTimeout(60 * 1000000);
		
		SystemTime::forceAll(TODAY);
		stopLoggingServer();
		ensure(core->newTransaction("foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 30 * 1000000);
		startLoggingServer();
		ensure(core->newTransaction("foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 61 * 1000000);
		ensure(!core->newTransaction("foobar")->isNull());
	}
	
	TEST_METHOD(12) {
		// If the logging server crashed and was restarted then
		// newTransaction() and continueTransaction() print a warning and return
		// a null log object. One of the next newTransaction()/continueTransaction()
		// calls will reestablish the connection when the connection timeout
		// has passed.
		SystemTime::forceAll(TODAY);
		TransactionPtr log, log2;
		
		log = core->newTransaction("foobar");
		core2->continueTransaction(log->getTxnId(), "foobar");
		log.reset(); // Check connection back into the pool.
		stopLoggingServer();
		startLoggingServer();

		log = core->newTransaction("foobar");
		ensure("(1)", log->isNull());
		log2 = core2->continueTransaction("some-id", "foobar");
		ensure("(2)", log2->isNull());
		
		SystemTime::forceAll(TODAY + 60000000);
		log = core->newTransaction("foobar");
		ensure("(3)", !log->isNull());
		log2 = core2->continueTransaction(log->getTxnId(), "foobar");
		ensure("(4)", !log2->isNull());
		log2->message("hello");
		log2->flushToDiskAfterClose(true);
		log.reset();
		log2.reset();
		
		EVENTUALLY(3,
			result = readDumpFile().find("hello\n") != string::npos;
		);
	}
	
	TEST_METHOD(13) {
		// continueTransaction() does not reconnect to the server for a short
		// period of time if connecting failed
		core->setReconnectTimeout(60 * 1000000);
		core2->setReconnectTimeout(60 * 1000000);
		
		SystemTime::forceAll(TODAY);
		TransactionPtr log = core->newTransaction("foobar");
		core2->continueTransaction(log->getTxnId(), "foobar");
		stopLoggingServer();
		ensure(core2->continueTransaction(log->getTxnId(), "foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 30 * 1000000);
		startLoggingServer();
		ensure(core2->continueTransaction(log->getTxnId(), "foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 61 * 1000000);
		ensure(!core2->continueTransaction(log->getTxnId(), "foobar")->isNull());
	}
	
	TEST_METHOD(14) {
		// If a client disconnects from the logging server then all its
		// transactions that are no longer referenced and have crash protection enabled
		// will be closed and written to the sink.
		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		MessageClient client3 = createConnection();
		vector<string> args;
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", "true", NULL);
		client1.read(args);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", NULL);
		client2.write("log", TODAY_TXN_ID, "1000", NULL);
		client2.writeScalar("hello world");
		client2.write("flush", NULL);
		client2.read(args);
		client2.disconnect();
		SHOULD_NEVER_HAPPEN(100,
			// Transaction still has references open, so should not yet be written to sink.
			result = readDumpFile().find("hello world") != string::npos;
		);

		client1.disconnect();
		client3.write("flush", NULL);
		client3.read(args);
		EVENTUALLY(5,
			result = readDumpFile().find("hello world") != string::npos;
		);
	}
	
	TEST_METHOD(15) {
		// If a client disconnects from the logging server then all its
		// transactions that are no longer referenced and don't have crash
		// protection enabled will be closed and discarded.
		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		MessageClient client3 = createConnection();
		vector<string> args;
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", "true", NULL);
		client1.read(args);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", NULL);
		client2.write("flush", NULL);
		client2.read(args);
		client2.disconnect();
		client1.disconnect();
		client3.write("flush", NULL);
		client3.read(args);
		SHOULD_NEVER_HAPPEN(500,
			result = fileExists(dumpFile) && !readDumpFile().empty();
		);
	}
	
	TEST_METHOD(16) {
		// Upon server shutdown, all transaction that have crash protection enabled
		// will be closed and written to to the sink.
		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", "true", NULL);
		client1.read(args);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", NULL);
		client2.write("flush", NULL);
		client2.read(args);
		
		stopLoggingServer();
		EVENTUALLY(5,
			result = fileExists(dumpFile) && !readDumpFile().empty();
		);
	}
	
	TEST_METHOD(17) {
		// Upon server shutdown, all transaction that don't have crash protection
		// enabled will be discarded.
		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", "true", NULL);
		client1.read(args);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"-", "false", NULL);
		client2.write("flush", NULL);
		client2.read(args);
		
		stopLoggingServer();
		SHOULD_NEVER_HAPPEN(200,
			result = fileExists(dumpFile) && !readDumpFile().empty();
		);
	}
	
	TEST_METHOD(18) {
		// Test DataStoreId
		{
			// Empty construction.
			DataStoreId id;
			ensure_equals(id.getGroupName(), "");
			ensure_equals(id.getNodeName(), "");
			ensure_equals(id.getCategory(), "");
		}
		{
			// Normal construction.
			DataStoreId id("ab", "cd", "ef");
			ensure_equals(id.getGroupName(), "ab");
			ensure_equals(id.getNodeName(), "cd");
			ensure_equals(id.getCategory(), "ef");
		}
		{
			// Copy constructor.
			DataStoreId id("ab", "cd", "ef");
			DataStoreId id2(id);
			ensure_equals(id2.getGroupName(), "ab");
			ensure_equals(id2.getNodeName(), "cd");
			ensure_equals(id2.getCategory(), "ef");
		}
		{
			// Assignment operator.
			DataStoreId id("ab", "cd", "ef");
			DataStoreId id2;
			id2 = id;
			ensure_equals(id2.getGroupName(), "ab");
			ensure_equals(id2.getNodeName(), "cd");
			ensure_equals(id2.getCategory(), "ef");
			
			DataStoreId id3("gh", "ij", "kl");
			id3 = id;
			ensure_equals(id3.getGroupName(), "ab");
			ensure_equals(id3.getNodeName(), "cd");
			ensure_equals(id3.getCategory(), "ef");
		}
		{
			// < operator
			DataStoreId id, id2;
			ensure(!(id < id2));
			
			id = DataStoreId("ab", "cd", "ef");
			id2 = DataStoreId("ab", "cd", "ef");
			ensure(!(id < id2));
			
			id = DataStoreId("ab", "cd", "ef");
			id2 = DataStoreId("bb", "cd", "ef");
			ensure(id < id2);
			
			id = DataStoreId("ab", "cd", "ef");
			id2 = DataStoreId();
			ensure(id2 < id);
			
			id = DataStoreId();
			id2 = DataStoreId("ab", "cd", "ef");
			ensure(id < id2);
		}
		{
			// == operator
			ensure(DataStoreId() == DataStoreId());
			ensure(DataStoreId("ab", "cd", "ef") == DataStoreId("ab", "cd", "ef"));
			ensure(!(DataStoreId("ab", "cd", "ef") == DataStoreId()));
			ensure(!(DataStoreId("ab", "cd", "ef") == DataStoreId("ab", "cd", "e")));
			ensure(!(DataStoreId("ab", "cd", "ef") == DataStoreId("ab", "c", "ef")));
			ensure(!(DataStoreId("ab", "cd", "ef") == DataStoreId("a", "cd", "ef")));
		}
	}
	
	TEST_METHOD(22) {
		// The destructor flushes all data.
		TransactionPtr log = core->newTransaction("foobar");
		log->message("hello world");
		log.reset();
		stopLoggingServer();
		
		struct stat buf;
		ensure_equals(stat(dumpFile.c_str(), &buf), 0);
		ensure(buf.st_size > 0);
	}
	
	TEST_METHOD(23) {
		// The 'flush' command flushes all data.
		SystemTime::forceAll(YESTERDAY);
		
		TransactionPtr log = core->newTransaction("foobar");
		log->message("hello world");
		log.reset();
		
		ConnectionPtr connection = core->checkoutConnection();
		vector<string> args;
		writeArrayMessage(connection->fd, "flush", NULL);
		ensure(readArrayMessage(connection->fd, args));
		ensure_equals(args.size(), 1u);
		ensure_equals(args[0], "ok");
		
		struct stat buf;
		ensure_equals(stat(dumpFile.c_str(), &buf), 0);
		ensure(buf.st_size > 0);
	}
	
	TEST_METHOD(24) {
		// A transaction's data is not written out by the server
		// until the transaction is fully closed.
		SystemTime::forceAll(YESTERDAY);
		vector<string> args;
		
		TransactionPtr log = core->newTransaction("foobar");
		log->message("hello world");
		
		TransactionPtr log2 = core2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 2");
		log2.reset();
		
		ConnectionPtr connection = core->checkoutConnection();
		writeArrayMessage(connection->fd, "flush", NULL);
		ensure(readArrayMessage(connection->fd, args));
		
		connection = core2->checkoutConnection();
		writeArrayMessage(connection->fd, "flush", NULL);
		ensure(readArrayMessage(connection->fd, args));
		
		struct stat buf;
		ensure_equals(stat(dumpFile.c_str(), &buf), 0);
		ensure_equals(buf.st_size, (off_t) 0);
	}
	
	TEST_METHOD(25) {
		// The 'exit' command causes the logging server to exit some time after
		// the last client has disconnected. New clients are still accepted
		// as long as the server hasn't exited.
		SystemTime::forceAll(YESTERDAY);
		vector<string> args;
		
		MessageClient client = createConnection();
		
		MessageClient client2 = createConnection();
		client2.write("exit", NULL);
		ensure("(1)", client2.read(args));
		ensure_equals(args.size(), 1u);
		ensure_equals(args[0], "Passed security");
		ensure("(2)", client2.read(args));
		ensure_equals(args.size(), 1u);
		ensure_equals(args[0], "exit command received");
		client2.disconnect();
		
		// Not exited yet: there is still a client.
		client2 = createConnection();
		client2.write("ping", NULL);
		ensure("(3)", client2.read(args));
		client2.disconnect();
		
		client.disconnect();
		setLogLevel(-2);
		usleep(25000); // Give server some time to process the connection closes.
		
		// No clients now, but we can still connect because the timeout
		// hasn't passed yet.
		SystemTime::forceAll(YESTERDAY + 1000000);
		SHOULD_NEVER_HAPPEN(250,
			try {
				close(connectToUnixServer(socketFilename));
				result = false;
			} catch (const SystemException &) {
				result = true;
			}
		);
		
		usleep(50000); // Give server some time to process the connection closes.
		
		// It'll be gone in a few seconds.
		SystemTime::forceAll(YESTERDAY + 1000000 + 5000000);
		usleep(100000); // Give server some time to run the timer.
		try {
			close(connectToUnixServer(socketFilename));
			fail("(4)");
		} catch (const SystemException &) {
			// Success
		}
		
		joinLoggingServer();
	}
	
	TEST_METHOD(26) {
		// The 'exit semi-gracefully' command causes the logging server to
		// refuse new clients while exiting some time after the last client has
		// disconnected.
		SystemTime::forceAll(YESTERDAY);
		vector<string> args;
		
		MessageClient client = createConnection();
		
		MessageClient client2 = createConnection();
		client2.write("exit", "semi-gracefully", NULL);
		client2.disconnect();
		
		// New connections are refused.
		client2 = createConnection();
		ensure("(1)", !client2.read(args));
		
		client.disconnect();
		setLogLevel(-2);
		usleep(50000); // Give server some time to process the connection closes.
		
		// It'll be gone in a few seconds.
		SystemTime::forceAll(YESTERDAY + 1000000 + 5000000);
		usleep(100000); // Give server some time to run the timer.
		try {
			close(connectToUnixServer(socketFilename));
			fail("(2)");
		} catch (const SystemException &) {
			// Success
		}
		
		joinLoggingServer();
	}
	
	TEST_METHOD(27) {
		// The 'exit immediately' command causes the logging server to
		// immediately exit. Open transactions are not automatically
		// closed and written out, even those with crash protection
		// turned on.
		SystemTime::forceAll(YESTERDAY);
		
		TransactionPtr log = core->newTransaction("foobar");
		log->message("hello world");
		log.reset();
		
		MessageClient client = createConnection();
		client.write("exit", "immediately", NULL);
		client.disconnect();
		
		// Assertion: the following doesn't block.
		joinLoggingServer();
	}
	
	TEST_METHOD(28) {
		// UnionStation::Core treats a server that's semi-gracefully exiting as
		// one that's refusing connections.
		SystemTime::forceAll(YESTERDAY);
		
		MessageClient client = createConnection();
		client.write("exit", "semi-gracefully", NULL);
		client.disconnect();
		
		TransactionPtr log = core->newTransaction("foobar");
		ensure(log->isNull());
	}
	
	TEST_METHOD(29) {
		// One can supply a custom node name per openTransaction command.
		MessageClient client1 = createConnection();
		vector<string> args;
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "remote", "requests", TODAY_TIMESTAMP_STR,
			"-", "true", NULL);
		client1.write("closeTransaction", TODAY_TXN_ID, TODAY_TIMESTAMP_STR, NULL);
		client1.write("flush", NULL);
		client1.read(args);
		client1.disconnect();
		
		ensure(fileExists(dumpFile));
	}
	
	TEST_METHOD(30) {
		// A transaction is only written to the sink if it passes all given filters.
		// Test logging of new transaction.
		SystemTime::forceAll(YESTERDAY);
		
		TransactionPtr log = core->newTransaction("foobar", "requests", "-",
			"uri == \"/foo\""
			"\1"
			"uri != \"/bar\"");
		log->message("URI: /foo");
		log->message("transaction 1");
		log->flushToDiskAfterClose(true);
		log.reset();
		
		log = core->newTransaction("foobar", "requests", "-",
			"uri == \"/foo\""
			"\1"
			"uri == \"/bar\"");
		log->message("URI: /foo");
		log->message("transaction 2");
		log->flushToDiskAfterClose(true);
		log.reset();
		
		string data = readDumpFile();
		ensure("(1)", data.find("transaction 1\n") != string::npos);
		ensure("(2)", data.find("transaction 2\n") == string::npos);
	}
	
	/************************************/
}
