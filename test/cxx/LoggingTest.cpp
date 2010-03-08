#include "TestSupport.h"
#include "Logging.h"
#include "LoggingAgent/LoggingServer.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>

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
		#define FOOBAR_LOCALHOST_PREFIX FOOBAR_MD5 "/" LOCALHOST_MD5
		
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		string socketFilename;
		string loggingDir;
		AccountsDatabasePtr accountsDatabase;
		MessageServerPtr server;
		shared_ptr<oxt::thread> serverThread;
		AnalyticsLoggerPtr logger;
		
		LoggingTest() {
			createServerInstanceDirAndGeneration(serverInstanceDir, generation);
			socketFilename = generation->getPath() + "/logging.socket";
			loggingDir = generation->getPath() + "/logs";
			accountsDatabase = ptr(new AccountsDatabase());
			accountsDatabase->add("test", "1234", false);
			
			server = ptr(new MessageServer(socketFilename, accountsDatabase));
			server->addHandler(ptr(new LoggingServer(loggingDir)));
			serverThread = ptr(new oxt::thread(
				boost::bind(&MessageServer::mainLoop, server.get())
			));
			
			logger = ptr(new AnalyticsLogger(socketFilename, "test", "1234",
				"localhost"));
		}
		
		~LoggingTest() {
			serverThread->interrupt_and_join();
			SystemTime::releaseAll();
		}
	};
	
	DEFINE_TEST_GROUP(LoggingTest);
	
	TEST_METHOD(1) {
		// Test logging of new transaction.
		SystemTime::forceUsec(YESTERDAY);
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("hello");
		log->message("world");
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		ensure(data.find("hello\n") != string::npos);
		ensure(data.find("world\n") != string::npos);
		
		ensure(!logger->isNull());
		ensure(!log->isNull());
	}
	
	TEST_METHOD(2) {
		// Test logging of existing transaction.
		SystemTime::forceUsec(YESTERDAY);
		
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		ensure_equals(log->getGroupName(), "foobar");
		log->message("message 1");
		
		AnalyticsLogPtr log2 = logger->continueTransaction(log->getGroupName(), log->getTxnId());
		log->message("message 2");
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		ensure(data.find("message 1\n") != string::npos);
		ensure(data.find("message 2\n") != string::npos);
	}
	
	TEST_METHOD(3) {
		// Test logging with different points in time.
		SystemTime::forceUsec(YESTERDAY);
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		log->message("message 1");
		SystemTime::forceUsec(TODAY);
		log->message("message 2");
		
		SystemTime::forceUsec(TOMORROW);
		AnalyticsLogPtr log2 = logger->continueTransaction(log->getGroupName(), log->getTxnId());
		log2->message("message 3");
		
		AnalyticsLogPtr log3 = logger->newTransaction("foobar");
		log3->message("message 4");
		
		string yesterdayData = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		string tomorrowData = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/14/12/log.txt");
		
		ensure("(1)", yesterdayData.find(toString(YESTERDAY) + " message 1\n") != string::npos);
		ensure("(2)", yesterdayData.find(toString(TODAY) + " message 2\n") != string::npos);
		ensure("(3)", yesterdayData.find(toString(TOMORROW) + " message 3\n") != string::npos);
		ensure("(4)", tomorrowData.find(toString(TOMORROW) + " message 4\n") != string::npos);
	}
	
	TEST_METHOD(4) {
		// newTransaction() and continueTransaction() write a ATTACH message
		// to the log file, while AnalyticsLogPtr writes an DETACH message upon
		// destruction.
		SystemTime::forceUsec(YESTERDAY);
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		SystemTime::forceUsec(TODAY);
		AnalyticsLogPtr log2 = logger->continueTransaction(log->getGroupName(), log->getTxnId());
		log2.reset();
		SystemTime::forceUsec(TOMORROW);
		log.reset();
		
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/requests/2010/01/12/12/log.txt");
		ensure("(1)", data.find(toString(YESTERDAY) + " ATTACH\n") != string::npos);
		ensure("(2)", data.find(toString(TODAY) + " ATTACH\n") != string::npos);
		ensure("(3)", data.find(toString(TODAY) + " DETACH\n") != string::npos);
		ensure("(4)", data.find(toString(TOMORROW) + " DETACH\n") != string::npos);
	}
	
	TEST_METHOD(5) {
		// newTransaction() generates a new ID, while continueTransaction()
		// reuses the ID.
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		AnalyticsLogPtr log2 = logger->newTransaction("foobar");
		AnalyticsLogPtr log3 = logger->continueTransaction(log->getGroupName(), log->getTxnId());
		AnalyticsLogPtr log4 = logger->continueTransaction(log2->getGroupName(), log2->getTxnId());
		
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
		string data = readAll(loggingDir + "/1/" FOOBAR_MD5 "/group_name.txt");
		ensure_equals(data, "foobar");
	}
	
	TEST_METHOD(9) {
		// It creates a file node_name.txt under the node directory.
		AnalyticsLogPtr log = logger->newTransaction("foobar");
		string data = readAll(loggingDir + "/1/" FOOBAR_LOCALHOST_PREFIX "/node_name.txt");
		ensure_equals(data, "localhost");
	}
}
