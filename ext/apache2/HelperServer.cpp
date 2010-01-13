/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <string>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "ApplicationPool/Pool.h"
#include "ApplicationPool/Server.h"
#include "AccountsDatabase.h"
#include "Account.h"
#include "MessageServer.h"
#include "BacktracesServer.h"
#include "ServerInstanceDir.h"
#include "MessageChannel.h"
#include "FileDescriptor.h"
#include "Timer.h"
#include "Logging.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace std;
using namespace boost;
using namespace oxt;
using namespace Passenger;

class Server;


class TimerUpdateHandler: public MessageServer::Handler {
private:
	Timer &timer;
	unsigned int clients;
	
public:
	TimerUpdateHandler(Timer &_timer): timer(_timer) {
		clients = 0;
	}
	
	virtual MessageServer::ClientContextPtr newClient(MessageServer::CommonClientContext &commonContext) {
		clients++;
		timer.stop();
		return MessageServer::ClientContextPtr();
	}
	
	virtual void clientDisconnected(MessageServer::CommonClientContext &commonContext,
	                                MessageServer::ClientContextPtr &handlerSpecificContext)
	{
		clients--;
		if (clients == 0) {
			timer.start();
		}
	}
	
	virtual bool processMessage(MessageServer::CommonClientContext &commonContext,
	                            MessageServer::ClientContextPtr &handlerSpecificContext,
	                            const vector<string> &args)
	{
		return false;
	}
};

class ExitHandler: public MessageServer::Handler {
private:
	EventFd &exitEvent;
	
public:
	ExitHandler(EventFd &_exitEvent)
		: exitEvent(_exitEvent)
	{ }
	
	virtual bool processMessage(MessageServer::CommonClientContext &commonContext,
	                            MessageServer::ClientContextPtr &handlerSpecificContext,
	                            const vector<string> &args)
	{
		if (args[0] == "exit") {
			TRACE_POINT();
			commonContext.requireRights(Account::EXIT);
			UPDATE_TRACE_POINT();
			exitEvent.notify();
			UPDATE_TRACE_POINT();
			commonContext.channel.write("exit command received", NULL);
			return true;
		} else {
			return false;
		}
	}
};

class Server {
private:
	static const unsigned int MESSAGE_SERVER_STACK_SIZE = 64 * 1024;
	
	ServerInstanceDir serverInstanceDir;
	ServerInstanceDir::GenerationPtr generation;
	FileDescriptor feedbackFd;
	MessageChannel feedbackChannel;
	TxnLoggerPtr txnLogger;
	AccountsDatabasePtr accountsDatabase;
	MessageServerPtr messageServer;
	ApplicationPool::PoolPtr pool;
	shared_ptr<oxt::thread> messageServerThread;
	EventFd exitEvent;
	Timer exitTimer;
	
	string receivePassword() {
		TRACE_POINT();
		vector<string> args;
		
		if (!feedbackChannel.read(args)) {
			throw IOException("The watchdog unexpectedly closed the connection.");
		}
		if (args[0] != "request socket password" && args[0] != "message socket password") {
			throw IOException("Unexpected input message '" + args[0] + "'");
		}
		return Base64::decode(args[1]);
	}
	
	/**
	 * Lowers this process's privilege to that of <em>username</em>.
	 */
	void lowerPrivilege(const string &username) {
		struct passwd *entry;
		
		entry = getpwnam(username.c_str());
		if (entry != NULL) {
			if (initgroups(username.c_str(), entry->pw_gid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
					"privilege to that of user '" << username <<
					"': cannot set supplementary groups for this "
					"user: " << strerror(e) << " (" << e << ")");
			}
			if (setgid(entry->pw_gid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
					"privilege to that of user '" << username <<
					"': cannot set group ID: " << strerror(e) <<
					" (" << e << ")");
			}
			if (setuid(entry->pw_uid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
					"privilege to that of user '" << username <<
					"': cannot set user ID: " << strerror(e) <<
					" (" << e << ")");
			}
		} else {
			P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
				"privilege to that of user '" << username <<
				"': user does not exist.");
		}
	}
	
public:
	Server(unsigned int logLevel, FileDescriptor feedbackFd,
		pid_t webServerPid, const string &tempDir,
		bool userSwitching, const string &defaultUser, uid_t workerUid, gid_t workerGid,
		const string &passengerRoot, const string &rubyCommand,
		unsigned int generationNumber, unsigned int maxPoolSize,
		unsigned int maxInstancesPerApp, unsigned int poolIdleTime,
		const string &analyticsLogDir)
		: serverInstanceDir(webServerPid, tempDir, false)
	{
		TRACE_POINT();
		vector<string> args;
		string messageSocketPassword;
		string loggingSocketPassword;
		
		setLogLevel(logLevel);
		this->feedbackFd  = feedbackFd;
		feedbackChannel   = MessageChannel(feedbackFd);
		
		UPDATE_TRACE_POINT();
		if (!feedbackChannel.read(args)) {
			throw IOException("The watchdog unexpectedly closed the connection.");
		}
		if (args[0] != "passwords") {
			throw IOException("Unexpected input message '" + args[0] + "'");
		}
		messageSocketPassword = Base64::decode(args[2]);
		loggingSocketPassword = Base64::decode(args[3]);
		
		generation        = serverInstanceDir.getGeneration(generationNumber);
		accountsDatabase  = AccountsDatabase::createDefault(generation, userSwitching, defaultUser);
		accountsDatabase->add("_web_server", messageSocketPassword, false,
			Account::GET | Account::DETACH | Account::SET_PARAMETERS | Account::EXIT);
		messageServer = ptr(new MessageServer(generation->getPath() + "/socket", accountsDatabase));
		
		createFile(generation->getPath() + "/helper_server.pid",
			toString(getpid()), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		
		if (geteuid() == 0 && !userSwitching) {
			lowerPrivilege(defaultUser);
		}
		
		UPDATE_TRACE_POINT();
		txnLogger = ptr(new TxnLogger(analyticsLogDir,
			generation->getPath() + "/logging.socket",
			"logging", loggingSocketPassword));
		
		pool = ptr(new ApplicationPool::Pool(
			findSpawnServer(passengerRoot.c_str()), generation,
			accountsDatabase->get("_backend"), rubyCommand
		));
		pool->setMax(maxPoolSize);
		pool->setMaxPerApp(maxInstancesPerApp);
		pool->setMaxIdleTime(poolIdleTime);
		
		messageServer->addHandler(ptr(new TimerUpdateHandler(exitTimer)));
		messageServer->addHandler(ptr(new ApplicationPool::Server(pool)));
		messageServer->addHandler(ptr(new BacktracesServer()));
		messageServer->addHandler(ptr(new ExitHandler(exitEvent)));
		
		UPDATE_TRACE_POINT();
		feedbackChannel.write("initialized",
			"",  // Request socket filename; not available in the Apache helper server.
			messageServer->getSocketFilename().c_str(),
			NULL);
	}
	
	~Server() {
		TRACE_POINT();
		if (messageServerThread != NULL) {
			messageServerThread->interrupt_and_join();
		}
	}
	
	void mainLoop() {
		TRACE_POINT();
		
		messageServerThread.reset(new oxt::thread(
			boost::bind(&MessageServer::mainLoop, messageServer.get()),
			"MessageServer thread",
			MESSAGE_SERVER_STACK_SIZE
		));
		
		/* Wait until the watchdog closes the feedback fd (meaning it
		 * was killed) or until we receive an exit message.
		 */
		this_thread::disable_syscall_interruption dsi;
		fd_set fds;
		int largestFd;
		
		FD_ZERO(&fds);
		FD_SET(feedbackFd, &fds);
		FD_SET(exitEvent.fd(), &fds);
		largestFd = (feedbackFd > exitEvent.fd()) ? (int) feedbackFd : exitEvent.fd();
		UPDATE_TRACE_POINT();
		if (syscalls::select(largestFd + 1, &fds, NULL, NULL, NULL) == -1) {
			int e = errno;
			throw SystemException("select() failed", e);
		}
		
		if (FD_ISSET(feedbackFd, &fds)) {
			/* If the watchdog has been killed then we'll kill all descendant
			 * processes and exit. There's no point in keeping this helper
			 * server running because we can't detect when the web server exits,
			 * and because this helper server doesn't own the server instance
			 * directory. As soon as passenger-status is run, the server
			 * instance directory will be cleaned up, making this helper server
			 * inaccessible.
			 */
			syscalls::killpg(getpgrp(), SIGKILL);
			_exit(2); // In case killpg() fails.
		} else {
			/* We received an exit command. We want to exit 5 seconds after
			 * the last client has disconnected, .
			 */
			exitTimer.start();
			exitTimer.wait(5000);
		}
	}
};

static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

int
main(int argc, char *argv[]) {
	TRACE_POINT();
	try {
		/* Become the process group leader so that the watchdog can kill the
		 * HelperServer as well as all descendant processes. */
		setpgrp();
		
		ignoreSigpipe();
		setup_syscall_interruption_support();
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
		
		unsigned int   logLevel   = atoi(argv[1]);
		FileDescriptor feedbackFd = atoi(argv[2]);
		pid_t   webServerPid  = (pid_t) atoll(argv[3]);
		string  tempDir       = argv[4];
		bool    userSwitching = strcmp(argv[5], "true") == 0;
		string  defaultUser   = argv[6];
		uid_t   workerUid     = (uid_t) atoll(argv[7]);
		gid_t   workerGid     = (uid_t) atoll(argv[8]);
		string  passengerRoot = argv[9];
		string  rubyCommand   = argv[10];
		unsigned int generationNumber  = atoll(argv[11]);
		unsigned int maxPoolSize        = atoi(argv[12]);
		unsigned int maxInstancesPerApp = atoi(argv[13]);
		unsigned int poolIdleTime       = atoi(argv[14]);
		string  analyticsLogDir = argv[15];
		
		// Change process title.
		strncpy(argv[0], "PassengerHelperServer", strlen(argv[0]));
		for (int i = 1; i < argc; i++) {
			memset(argv[i], '\0', strlen(argv[i]));
		}
		
		UPDATE_TRACE_POINT();
		Server server(logLevel, feedbackFd, webServerPid, tempDir,
			userSwitching, defaultUser, workerUid, workerGid,
			passengerRoot, rubyCommand, generationNumber,
			maxPoolSize, maxInstancesPerApp, poolIdleTime,
			analyticsLogDir);
		P_DEBUG("Phusion Passenger helper server started on PID " << getpid());
		
		UPDATE_TRACE_POINT();
		server.mainLoop();
	} catch (const tracable_exception &e) {
		P_ERROR(e.what() << "\n" << e.backtrace());
		return 1;
	} catch (const exception &e) {
		P_ERROR(e.what());
		return 1;
	} catch (...) {
		P_ERROR("Unknown exception thrown in main thread.");
		throw;
	}
	
	P_TRACE(2, "Phusion Passenger Helper server exited.");
	return 0;
}
