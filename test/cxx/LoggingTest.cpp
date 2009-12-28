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
		static const unsigned long long YESTERDAY = 1263299017000000ull;  // January 12, 2009, 13:23:37
		static const unsigned long long TODAY     = 1263385422000000ull;  // January 13, 2009, 13:23:42
		static const unsigned long long TOMORROW  = 1263471822000000ull;  // January 14, 2009, 13:23:42
		
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
			SystemTime::releaseAll();
		}
	};
	
	DEFINE_TEST_GROUP(LoggingTest);
	
	TEST_METHOD(1) {
		// Test logging of new transaction.
		SystemTime::forceUsec(YESTERDAY);
		TxnLogPtr log = logger->newTransaction("foobar");
		log->message("hello");
		log->message("world");
		
		string data = readAll(loggingDir + "/1/foobar/2010/01/12/13/web_txns.txt");
		ensure(data.find("hello\n") != string::npos);
		ensure(data.find("world\n") != string::npos);
		
		ensure(!logger->isNull());
		ensure(!log->isNull());
	}
	
	TEST_METHOD(2) {
		// Test logging of existing transaction.
		SystemTime::forceUsec(YESTERDAY);
		
		TxnLogPtr log = logger->newTransaction("foobar");
		ensure_equals(log->getGroupName(), "foobar");
		log->message("message 1");
		
		TxnLogPtr log2 = logger->continueTransaction(log->getGroupName(), log->getId());
		log->message("message 2");
		
		string data = readAll(loggingDir + "/1/foobar/2010/01/12/13/web_txns.txt");
		ensure(data.find("message 1\n") != string::npos);
		ensure(data.find("message 2\n") != string::npos);
	}
	
	TEST_METHOD(3) {
		// Test logging with different points in time.
		SystemTime::forceUsec(YESTERDAY);
		TxnLogPtr log = logger->newTransaction("foobar");
		log->message("message 1");
		SystemTime::forceUsec(TODAY);
		log->message("message 2");
		
		SystemTime::forceUsec(TOMORROW);
		TxnLogPtr log2 = logger->continueTransaction(log->getGroupName(), log->getId());
		log2->message("message 3");
		
		TxnLogPtr log3 = logger->newTransaction("foobar");
		log3->message("message 4");
		
		string yesterdayData = readAll(loggingDir + "/1/foobar/2010/01/12/13/web_txns.txt");
		string tomorrowData = readAll(loggingDir + "/1/foobar/2010/01/14/13/web_txns.txt");
		
		ensure("(1)", yesterdayData.find(toString(YESTERDAY) + " message 1\n") != string::npos);
		ensure("(2)", yesterdayData.find(toString(TODAY) + " message 2\n") != string::npos);
		ensure("(3)", yesterdayData.find(toString(TOMORROW) + " message 3\n") != string::npos);
		ensure("(4)", tomorrowData.find(toString(TOMORROW) + " message 4\n") != string::npos);
	}
	
	TEST_METHOD(4) {
		// newTransaction() and continueTransaction() write a ATTACH message
		// to the log file, while TxnLogPtr writes an DETACH message upon
		// destruction.
		SystemTime::forceUsec(YESTERDAY);
		TxnLogPtr log = logger->newTransaction("foobar");
		SystemTime::forceUsec(TODAY);
		TxnLogPtr log2 = logger->continueTransaction(log->getGroupName(), log->getId());
		log2.reset();
		SystemTime::forceUsec(TOMORROW);
		log.reset();
		
		string data = readAll(loggingDir + "/1/foobar/2010/01/12/13/web_txns.txt");
		ensure("(1)", data.find(toString(YESTERDAY) + " ATTACH\n") != string::npos);
		ensure("(2)", data.find(toString(TODAY) + " ATTACH\n") != string::npos);
		ensure("(3)", data.find(toString(TODAY) + " DETACH\n") != string::npos);
		ensure("(4)", data.find(toString(TOMORROW) + " DETACH\n") != string::npos);
	}
	
	TEST_METHOD(5) {
		// newTransaction() generates a new ID, while continueTransaction()
		// reuses the ID.
		TxnLogPtr log = logger->newTransaction("foobar");
		TxnLogPtr log2 = logger->newTransaction("foobar");
		TxnLogPtr log3 = logger->continueTransaction(log->getGroupName(), log->getId());
		TxnLogPtr log4 = logger->continueTransaction(log2->getGroupName(), log2->getId());
		
		ensure_equals(log->getId(), log3->getId());
		ensure_equals(log2->getId(), log4->getId());
		ensure(log->getId() != log2->getId());
	}
	
	TEST_METHOD(6) {
		// An empty TxnLog doesn't do anything.
		TxnLog log;
		ensure(log.isNull());
		log.message("hello world");
		ensure_equals(getFileType(loggingDir), FT_NONEXISTANT);
	}
	
	TEST_METHOD(7) {
		// An empty TxnLogger doesn't do anything.
		TxnLogger logger;
		ensure(logger.isNull());
		
		TxnLogPtr log = logger.newTransaction("foo");
		ensure(log->isNull());
		log->message("hello world");
		ensure_equals(getFileType(loggingDir), FT_NONEXISTANT);
	}
	
	TEST_METHOD(8) {
		// Test group name validation.
		
		// Empty strings not allowed.
		ensure("(1)", !TxnLogger::validateGroupName(""));
		ensure("(2)", !TxnLogger::validateGroupName(" "));
		ensure("(3)", !TxnLogger::validateGroupName("   "));
		
		// No leading or trailing spaces.
		ensure("(4)", !TxnLogger::validateGroupName("hello "));
		ensure("(5)", !TxnLogger::validateGroupName(" hello"));
		
		// Disallowed characters.
		const char *disallowedChars[] = { "\r", "\n", "@", "%", "$",
			"~", "/", "\\", "[", "]", "{", "}", "?", "!" };
		for (unsigned int i = 0; i < sizeof(disallowedChars) / sizeof(char *); i++) {
			ensure("(6)", !TxnLogger::validateGroupName(disallowedChars[i]));
		}
		
		ensure("(10)", TxnLogger::validateGroupName("hello"));
		ensure("(11)", TxnLogger::validateGroupName("hello world"));
		ensure("(12)", TxnLogger::validateGroupName("foobar"));
		ensure("(13)", TxnLogger::validateGroupName("Sumiyoshi Kanako"));
		ensure("(14)", TxnLogger::validateGroupName("lAJ83.kj 1ke-J8si3fKJe_83jr"));
	}
}
