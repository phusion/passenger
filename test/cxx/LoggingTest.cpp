#include "TestSupport.h"
#include "Logging.h"
#include "LoggingServer.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/thread.hpp>

using namespace Passenger;
using namespace std;
using namespace oxt;

namespace tut {
	struct LoggingTest {
		static const unsigned long long YESTERDAY = 1261166119000ull;  // December 18, 2009
		static const unsigned long long TODAY     = 1261252183000ull;  // December 19, 2009
		static const unsigned long long TOMORROW  = 1261338988000ull;  // December 20, 2009
		
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		string socketFilename;
		string loggingDir;
		AccountsDatabasePtr accountsDatabase;
		MessageServerPtr server;
		shared_ptr<oxt::thread> serverThread;
		TxnLoggerPtr logger;
		
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
			
			logger = ptr(new TxnLogger(loggingDir, socketFilename, "test", "1234"));
		}
		
		~LoggingTest() {
			serverThread->interrupt_and_join();
			SystemTime::release();
			SystemTime::releaseMsec();
		}
	};
	
	DEFINE_TEST_GROUP(LoggingTest);
	
	TEST_METHOD(1) {
		// Test logging of new transaction.
		SystemTime::forceMsec(YESTERDAY);
		TxnLogPtr log = logger->newTransaction();
		log->message("hello");
		log->message("world");
		
		string data = readAll(loggingDir + "/1/2009/12/18/web_txns.txt");
		ensure(data.find("hello\n") != string::npos);
		ensure(data.find("world\n") != string::npos);
	}
	
	TEST_METHOD(2) {
		// Test logging of existing transaction.
		SystemTime::forceMsec(YESTERDAY);
		
		TxnLogPtr log = logger->newTransaction();
		log->message("message 1");
		
		TxnLogPtr log2 = logger->continueTransaction(log->getFullId());
		log->message("message 2");
		
		string data = readAll(loggingDir + "/1/2009/12/18/web_txns.txt");
		ensure(data.find("message 1\n") != string::npos);
		ensure(data.find("message 2\n") != string::npos);
	}
	
	TEST_METHOD(3) {
		// Test logging with different points in time.
		SystemTime::forceMsec(YESTERDAY);
		TxnLogPtr log = logger->newTransaction();
		log->message("message 1");
		SystemTime::forceMsec(TODAY);
		log->message("message 2");
		
		SystemTime::forceMsec(TOMORROW);
		TxnLogPtr log2 = logger->continueTransaction(log->getFullId());
		log2->message("message 3");
		
		TxnLogPtr log3 = logger->newTransaction();
		log3->message("message 4");
		
		string yesterdayData = readAll(loggingDir + "/1/2009/12/18/web_txns.txt");
		string tomorrowData = readAll(loggingDir + "/1/2009/12/20/web_txns.txt");
		
		ensure("(1)", yesterdayData.find(toString(YESTERDAY) + ": message 1\n") != string::npos);
		ensure("(2)", yesterdayData.find(toString(TODAY) + ": message 2\n") != string::npos);
		ensure("(3)", yesterdayData.find(toString(TOMORROW) + ": message 3\n") != string::npos);
		ensure("(4)", tomorrowData.find(toString(TOMORROW) + ": message 4\n") != string::npos);
	}
	
	TEST_METHOD(4) {
		// newTransaction() and continueTransaction() write a BEGIN message
		// to the log file, while TxnLogPtr writes an END message upon
		// destruction.
		SystemTime::forceMsec(YESTERDAY);
		TxnLogPtr log = logger->newTransaction();
		SystemTime::forceMsec(TODAY);
		TxnLogPtr log2 = logger->continueTransaction(log->getFullId());
		log2.reset();
		SystemTime::forceMsec(TOMORROW);
		log.reset();
		
		string data = readAll(loggingDir + "/1/2009/12/18/web_txns.txt");
		ensure("(1)", data.find(toString(YESTERDAY) + ": BEGIN\n") != string::npos);
		ensure("(2)", data.find(toString(TODAY) + ": BEGIN\n") != string::npos);
		ensure("(3)", data.find(toString(TODAY) + ": END\n") != string::npos);
		ensure("(4)", data.find(toString(TOMORROW) + ": END\n") != string::npos);
	}
	
	TEST_METHOD(5) {
		// newTransaction() generates a new ID, while continueTransaction()
		// reuses the ID.
		TxnLogPtr log = logger->newTransaction();
		TxnLogPtr log2 = logger->newTransaction();
		TxnLogPtr log3 = logger->continueTransaction(log->getFullId());
		TxnLogPtr log4 = logger->continueTransaction(log2->getFullId());
		
		ensure_equals(log->getShortId(), log3->getShortId());
		ensure_equals(log->getFullId(), log3->getFullId());
		ensure_equals(log2->getShortId(), log4->getShortId());
		ensure_equals(log2->getFullId(), log4->getFullId());
		ensure(log->getShortId() != log2->getShortId());
		ensure(log->getFullId() != log2->getFullId());
	}
	
	TEST_METHOD(6) {
		// An empty TxnLog doesn't do anything.
		TxnLog log;
		log.message("hello world");
		ensure_equals(getFileType(loggingDir), FT_NONEXISTANT);
	}
}
