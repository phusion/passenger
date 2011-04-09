#include "TestSupport.h"
#include "Logging.h"
#include "MessageClient.h"
#include "LoggingAgent/LoggingServer.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>
#include <set>

using namespace Passenger;
using namespace std;
using namespace oxt;

namespace tut {
	struct LoggingTest {
		static const unsigned long long YESTERDAY = 1263299017000000ull;  // January 12, 2009, 12:23:37 UTC
		static const unsigned long long TODAY     = 1263385422000000ull;  // January 13, 2009, 12:23:42 UTC
		static const unsigned long long TOMORROW  = 1263471822000000ull;  // January 14, 2009, 12:23:42 UTC
		#define FOOBAR_MD5 "3858f62230ac3c915f300c664312c63f"
		#define LOCALHOST_MD5 "421aa90e079fa326b6494f812ad13e79"
		#define REMOTEHOST_MD5 "2c18e486683a3db1e645ad8523223b72"
		#define FOOBAR_LOCALHOST_PREFIX FOOBAR_MD5 "/" LOCALHOST_MD5
		#define FOOBAR_REMOTEHOST_PREFIX FOOBAR_MD5 "/" REMOTEHOST_MD5
		#define TODAY_TXN_ID "cjb8n-abcd"
		#define TODAY_TIMESTAMP_STR "cftz90m3k0"
		
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		string socketFilename;
		string socketAddress;
		string loggingDir;
		AccountsDatabasePtr accountsDatabase;
		ev::dynamic_loop eventLoop;
		FileDescriptor serverFd;
		LoggingServerPtr server;
		shared_ptr<oxt::thread> serverThread;
		AnalyticsLoggerPtr logger, logger2, logger3, logger4;
		
		LoggingTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			socketFilename = generation->getPath() + "/logging.socket";
			socketAddress = "unix:" + socketFilename;
			loggingDir = generation->getPath() + "/logs";
			accountsDatabase = ptr(new AccountsDatabase());
			accountsDatabase->add("test", "1234", false);
			setLogLevel(-1);
			
			startLoggingServer();
			logger = ptr(new AnalyticsLogger(socketAddress, "test", "1234",
				"localhost"));
			logger2 = ptr(new AnalyticsLogger(socketAddress, "test", "1234",
				"localhost"));
			logger3 = ptr(new AnalyticsLogger(socketAddress, "test", "1234",
				"localhost"));
			logger4 = ptr(new AnalyticsLogger(socketAddress, "test", "1234",
				"localhost"));
		}
		
		~LoggingTest() {
			stopLoggingServer();
			SystemTime::releaseAll();
			setLogLevel(0);
		}
		
		void startLoggingServer(const function<void ()> &initFunc = function<void ()>()) {
			serverFd = createUnixServer(socketFilename.c_str());
			server = ptr(new LoggingServer(eventLoop,
				serverFd, accountsDatabase, loggingDir));
			if (initFunc) {
				initFunc();
			}
			serverThread = ptr(new oxt::thread(
				boost::bind(&LoggingTest::runLoop, this)
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
	};
	
	DEFINE_TEST_GROUP(LoggingTest);
	
	
	/*********** Logging interface tests ***********/
	
	TEST_METHOD(1) {
		// Test logging of new transaction.
		SystemTime::forceAll(YESTERDAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello");
		log->message("world");
		log->flushToDiskAfterClose(true);
		
		ensure(!logger->isNull());
		ensure(!log->isNull());
		
		log.reset();
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		ensure(data.find("hello\n") != string::npos);
		ensure(data.find("world\n") != string::npos);
	}
	
	TEST_METHOD(2) {
		// Test logging of existing transaction.
		SystemTime::forceAll(YESTERDAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("message 1");
		log->flushToDiskAfterClose(true);
		
		AnalyticsLogPtr log2 = logger2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 2");
		log2->flushToDiskAfterClose(true);

		log.reset();
		log2.reset();
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		ensure("(1)", data.find("message 1\n") != string::npos);
		ensure("(2)", data.find("message 2\n") != string::npos);
	}
	
	TEST_METHOD(3) {
		// Test logging with different points in time.
		SystemTime::forceAll(YESTERDAY);
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("message 1");
		SystemTime::forceAll(TODAY);
		log->message("message 2");
		log->flushToDiskAfterClose(true);
		
		SystemTime::forceAll(TOMORROW);
		AnalyticsLogPtr log2 = logger2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 3");
		log2->flushToDiskAfterClose(true);
		
		AnalyticsLogPtr log3 = logger3->newTransaction("foobar");
		log3->message("message 4");
		log3->flushToDiskAfterClose(true);
		
		log.reset();
		log2.reset();
		log3.reset();
		
		string yesterdayData = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		string tomorrowData = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/14/12/log.txt");
		ensure("(1)", yesterdayData.find(timestampString(YESTERDAY) + " 1 message 1\n") != string::npos);
		ensure("(2)", yesterdayData.find(timestampString(TODAY) + " 2 message 2\n") != string::npos);
		ensure("(3)", yesterdayData.find(timestampString(TOMORROW) + " 4 message 3\n") != string::npos);
		ensure("(4)", tomorrowData.find(timestampString(TOMORROW) + " 1 message 4\n") != string::npos);
	}
	
	TEST_METHOD(4) {
		// newTransaction() and continueTransaction() write an ATTACH message
		// to the log file, while AnalyticsLogPtr writes a DETACH message upon
		// destruction.
		SystemTime::forceAll(YESTERDAY);
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		
		SystemTime::forceAll(TODAY);
		AnalyticsLogPtr log2 = logger2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->flushToDiskAfterClose(true);
		log2.reset();
		
		SystemTime::forceAll(TOMORROW);
		log->flushToDiskAfterClose(true);
		log.reset();
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		ensure("(1)", data.find(timestampString(YESTERDAY) + " 0 ATTACH\n") != string::npos);
		ensure("(2)", data.find(timestampString(TODAY) + " 1 ATTACH\n") != string::npos);
		ensure("(3)", data.find(timestampString(TODAY) + " 2 DETACH\n") != string::npos);
		ensure("(4)", data.find(timestampString(TOMORROW) + " 3 DETACH\n") != string::npos);
	}
	
	TEST_METHOD(5) {
		// newTransaction() generates a new ID, while continueTransaction()
		// reuses the ID.
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		AnalyticsLogPtr log2 = logger2->newTransaction("foobar");
		AnalyticsLogPtr log3 = logger3->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		AnalyticsLogPtr log4 = logger4->continueTransaction(log2->getTxnId(),
			log2->getGroupName(), log2->getCategory());
		
		ensure_equals(log->getTxnId(), log3->getTxnId());
		ensure_equals(log2->getTxnId(), log4->getTxnId());
		ensure(log->getTxnId() != log2->getTxnId());
	}
	
	TEST_METHOD(6) {
		// An empty AnalyticsLog doesn't do anything.
		AnalyticsLog log;
		ensure(log.isNull());
		log.message("hello world");
		ensure_equals(getFileType(loggingDir), FT_NONEXISTANT);
	}
	
	TEST_METHOD(7) {
		// An empty AnalyticsLogger doesn't do anything.
		AnalyticsLogger logger;
		ensure(logger.isNull());
		
		AnalyticsLogPtr log = logger.newTransaction("foo");
		ensure(log->isNull());
		log->message("hello world");
		ensure_equals(getFileType(loggingDir), FT_NONEXISTANT);
	}
	
	TEST_METHOD(8) {
		// It creates a file group_name.txt under the group directory.
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->flushToDiskAfterClose(true);
		log.reset();
		string data = readAll(loggingDir + "/1/" FOOBAR_MD5 "/group_name.txt");
		ensure_equals(data, "foobar");
	}
	
	TEST_METHOD(9) {
		// It creates a file node_name.txt under the node directory.
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->flushToDiskAfterClose(true);
		log.reset();
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/node_name.txt");
		ensure_equals(data, "localhost");
	}
	
	TEST_METHOD(10) {
		// newTransaction() reestablishes the connection to the logging
		// server if the logging server crashed and was restarted
		SystemTime::forceAll(TODAY);
		
		logger->newTransaction("foobar");
		stopLoggingServer();
		startLoggingServer();
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello");
		log->flushToDiskAfterClose(true);
		log.reset();
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/13/12/log.txt");
		ensure("(1)", data.find("hello\n") != string::npos);
	}
	
	TEST_METHOD(11) {
		// newTransaction() does not reconnect to the server for a short
		// period of time if connecting failed
		logger->setReconnectTimeout(60 * 1000000);
		logger->setMaxConnectTries(1);
		
		SystemTime::forceAll(TODAY);
		stopLoggingServer();
		ensure(logger->newTransaction("foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 30 * 1000000);
		startLoggingServer();
		ensure(logger->newTransaction("foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 61 * 1000000);
		ensure(!logger->newTransaction("foobar")->isNull());
	}
	
	TEST_METHOD(12) {
		// continueTransaction() reestablishes the connection to the logging
		// server if the logging server crashed and was restarted
		SystemTime::forceAll(TODAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		logger2->continueTransaction(log->getTxnId(), "foobar");
		stopLoggingServer();
		startLoggingServer();
		
		AnalyticsLogPtr log2 = logger2->continueTransaction(log->getTxnId(), "foobar");
		log2->message("hello");
		log2->flushToDiskAfterClose(true);
		log2.reset();
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/13/12/log.txt");
		ensure("(1)", data.find("hello\n") != string::npos);
	}
	
	TEST_METHOD(13) {
		// continueTransaction() does not reconnect to the server for a short
		// period of time if connecting failed
		logger->setReconnectTimeout(60 * 1000000);
		logger->setMaxConnectTries(1);
		logger2->setReconnectTimeout(60 * 1000000);
		logger2->setMaxConnectTries(1);
		
		SystemTime::forceAll(TODAY);
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		logger2->continueTransaction(log->getTxnId(), "foobar");
		stopLoggingServer();
		ensure(logger2->continueTransaction(log->getTxnId(), "foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 30 * 1000000);
		startLoggingServer();
		ensure(logger2->continueTransaction(log->getTxnId(), "foobar")->isNull());
		
		SystemTime::forceAll(TODAY + 61 * 1000000);
		ensure(!logger2->continueTransaction(log->getTxnId(), "foobar")->isNull());
	}
	
	TEST_METHOD(14) {
		// If a client disconnects from the logging server then all its
		// transactions that are no longer referenced and have crash protection enabled
		// will be closed and written to to the sink.
		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		MessageClient client3 = createConnection();
		vector<string> args;
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/13/12/log.txt";
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "true", NULL);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "true", NULL);
		client2.write("flush", NULL);
		client2.read(args);
		client2.disconnect();
		
		SHOULD_NEVER_HAPPEN(100,
			result = fileExists(filename) && !readAll(filename).empty();
		);
		client1.disconnect();
		client3.write("flush", NULL);
		client3.read(args);
		EVENTUALLY(5,
			result = fileExists(filename) && !readAll(filename).empty();
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
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/13/12/log.txt";
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "false", NULL);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "false", NULL);
		client2.write("flush", NULL);
		client2.read(args);
		client2.disconnect();
		client1.disconnect();
		client3.write("flush", NULL);
		client3.read(args);
		SHOULD_NEVER_HAPPEN(500,
			result = fileExists(filename) && !readAll(filename).empty();
		);
	}
	
	TEST_METHOD(16) {
		// Upon server shutdown, all transaction that have crash protection enabled
		// will be closed and written to to the sink.
		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/13/12/log.txt";
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "true", NULL);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "true", NULL);
		client2.write("flush", NULL);
		client2.read(args);
		
		stopLoggingServer();
		EVENTUALLY(5,
			result = fileExists(filename) && !readAll(filename).empty();
		);
	}
	
	TEST_METHOD(17) {
		// Upon server shutdown, all transaction that don't have crash protection
		// enabled will be discarded.
		MessageClient client1 = createConnection();
		MessageClient client2 = createConnection();
		vector<string> args;
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/13/12/log.txt";
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "false", NULL);
		client2.write("openTransaction",
			TODAY_TXN_ID, "foobar", "", "requests", TODAY_TIMESTAMP_STR,
			"", "false", NULL);
		client2.write("flush", NULL);
		client2.read(args);
		
		stopLoggingServer();
		SHOULD_NEVER_HAPPEN(200,
			result = fileExists(filename) && !readAll(filename).empty();
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
	
	TEST_METHOD(21) {
		// The server temporarily buffers data in memory.
		SystemTime::forceAll(YESTERDAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello world");
		log.reset();
		
		// Give server some time to process these commands.
		usleep(20000);
		
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt";
		struct stat buf;
		ensure_equals(stat(filename.c_str(), &buf), 0);
		ensure_equals(buf.st_size, (off_t) 0);
	}
	
	TEST_METHOD(22) {
		// The destructor flushes all data.
		SystemTime::forceAll(YESTERDAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello world");
		log.reset();
		stopLoggingServer();
		
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt";
		struct stat buf;
		ensure_equals(stat(filename.c_str(), &buf), 0);
		ensure(buf.st_size > 0);
	}
	
	TEST_METHOD(23) {
		// The 'flush' command flushes all data.
		SystemTime::forceAll(YESTERDAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello world");
		log.reset();
		
		vector<string> args;
		MessageChannel channel(logger->getConnection());
		channel.write("flush", NULL);
		ensure(channel.read(args));
		ensure_equals(args.size(), 1u);
		ensure_equals(args[0], "ok");
		
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt";
		struct stat buf;
		ensure_equals(stat(filename.c_str(), &buf), 0);
		ensure(buf.st_size > 0);
	}
	
	TEST_METHOD(24) {
		// A transaction's data is not written out by the server
		// until the transaction is fully closed.
		SystemTime::forceAll(YESTERDAY);
		vector<string> args;
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello world");
		
		AnalyticsLogPtr log2 = logger2->continueTransaction(log->getTxnId(),
			log->getGroupName(), log->getCategory());
		log2->message("message 2");
		log2.reset();
		
		MessageChannel channel(logger->getConnection());
		channel.write("flush", NULL);
		ensure(channel.read(args));
		
		channel = MessageChannel(logger2->getConnection());
		channel.write("flush", NULL);
		ensure(channel.read(args));
		
		string filename = loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt";
		struct stat buf;
		ensure_equals(stat(filename.c_str(), &buf), 0);
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
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello world");
		log.reset();
		
		MessageClient client = createConnection();
		client.write("exit", "immediately", NULL);
		client.disconnect();
		
		// Assertion: the following doesn't block.
		joinLoggingServer();
	}
	
	TEST_METHOD(28) {
		// AnalyticsLogger treats a server that's semi-gracefully exiting as
		// one that's refusing connections.
		SystemTime::forceAll(YESTERDAY);
		
		MessageClient client = createConnection();
		client.write("exit", "semi-gracefully", NULL);
		client.disconnect();
		
		logger->setMaxConnectTries(1);
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		ensure(log->isNull());
	}
	
	TEST_METHOD(29) {
		// One can supply a custom node name per openTransaction command.
		MessageClient client1 = createConnection();
		vector<string> args;
		string filename = loggingDir + "/1/" FOOBAR_REMOTEHOST_PREFIX "/requests/2010/01/13/12/log.txt";
		
		SystemTime::forceAll(TODAY);
		
		client1.write("openTransaction",
			TODAY_TXN_ID, "foobar", "remote", "requests", TODAY_TIMESTAMP_STR,
			"", "true", NULL);
		client1.write("closeTransaction", TODAY_TXN_ID, TODAY_TIMESTAMP_STR, NULL);
		client1.write("flush", NULL);
		client1.read(args);
		client1.disconnect();
		
		ensure(fileExists(filename));
	}
	
	TEST_METHOD(30) {
		// A transaction is only written to the sink if it passes all given filters.
		// Test logging of new transaction.
		SystemTime::forceAll(YESTERDAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar", "requests", "",
			"uri == \"/foo\""
			"\1"
			"uri != \"/bar\"");
		log->message("URI: /foo");
		log->message("transaction 1");
		log->flushToDiskAfterClose(true);
		log.reset();
		
		log = logger->newTransaction("foobar", "requests", "",
			"uri == \"/foo\""
			"\1"
			"uri == \"/bar\"");
		log->message("URI: /foo");
		log->message("transaction 2");
		log->flushToDiskAfterClose(true);
		log.reset();
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		ensure("(1)", data.find("transaction 1\n") != string::npos);
		ensure("(2)", data.find("transaction 2\n") == string::npos);
	}
	
	/************************************/
}
