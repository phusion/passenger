/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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

#include <string>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
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
#include "Logging.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace std;
using namespace boost;
using namespace oxt;
using namespace Passenger;

class Server {
private:
	static const unsigned int MESSAGE_SERVER_STACK_SIZE = 64 * 1024;
	
	ServerInstanceDir serverInstanceDir;
	ServerInstanceDir::GenerationPtr generation;
	FileDescriptor feedbackFd;
	MessageChannel feedbackChannel;
	AccountsDatabasePtr accountsDatabase;
	MessageServerPtr messageServer;
	ApplicationPool::PoolPtr pool;
	shared_ptr<oxt::thread> messageServerThread;
	
	string receiveWebServerPassword() {
		vector<string> args;
		
		if (!feedbackChannel.read(args)) {
			throw IOException("Cannot read the web server account password");
		}
		if (args[0] != "web server account password") {
			throw IOException("Unexpected input message");
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
		unsigned int generationNumber)
		: serverInstanceDir(webServerPid, tempDir, false)
	{
		TRACE_POINT();
		string webServerPassword;
		
		setLogLevel(logLevel);
		this->feedbackFd  = feedbackFd;
		feedbackChannel   = MessageChannel(feedbackFd);
		
		UPDATE_TRACE_POINT();
		webServerPassword = receiveWebServerPassword();
		generation        = serverInstanceDir.getGeneration(generationNumber);
		accountsDatabase  = AccountsDatabase::createDefault(generation, userSwitching, defaultUser);
		accountsDatabase->add("_web_server", webServerPassword, false,
			Account::GET | Account::SET_PARAMETERS | Account::EXIT);
		messageServer = ptr(new MessageServer(generation->getPath() + "/socket", accountsDatabase));
		if (geteuid() == 0 && !userSwitching) {
			lowerPrivilege(defaultUser);
		}
		
		UPDATE_TRACE_POINT();
		pool.reset(new ApplicationPool::Pool(findSpawnServer(passengerRoot.c_str()), "", rubyCommand));
		messageServer->addHandler(ptr(new ApplicationPool::Server(pool)));
		messageServer->addHandler(ptr(new BacktracesServer()));
		
		UPDATE_TRACE_POINT();
		feedbackChannel.write("initialized", messageServer->getSocketFilename().c_str(), NULL);
	}
	
	~Server() {
		TRACE_POINT();
		messageServerThread->interrupt_and_join();
	}
	
	void mainLoop() {
		TRACE_POINT();
		
		messageServerThread.reset(new oxt::thread(
			boost::bind(&MessageServer::mainLoop, messageServer.get()),
			"MessageServer thread",
			MESSAGE_SERVER_STACK_SIZE
		));
		
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(feedbackFd, &fds);
		try {
			UPDATE_TRACE_POINT();
			if (syscalls::select(feedbackFd + 1, &fds, NULL, NULL, NULL) == -1) {
				int e = errno;
				throw SystemException("select() failed", e);
			}
		} catch (const boost::thread_interrupted &) {
			// Do nothing.
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
		 * HelperServer as well as all descendent processes. */
		setpgrp();
		
		ignoreSigpipe();
		setup_syscall_interruption_support();
		
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
		unsigned int generationNumber = atoll(argv[11]);
		
		UPDATE_TRACE_POINT();
		Server server(logLevel, feedbackFd, webServerPid, tempDir,
			userSwitching, defaultUser, workerUid, workerGid,
			passengerRoot, rubyCommand, generationNumber);
		P_DEBUG("Passenger helper server started on PID " << getpid());
		
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
	
	P_TRACE(2, "Helper server exited.");
	return 0;
}
