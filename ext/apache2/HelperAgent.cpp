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
#include <pwd.h>
#include <grp.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "AgentBase.h"
#include "HelperAgent/BacktracesServer.h"
#include "Constants.h"
#include "ApplicationPool/Pool.h"
#include "ApplicationPool/Server.h"
#include "AccountsDatabase.h"
#include "Account.h"
#include "MessageServer.h"
#include "ServerInstanceDir.h"
#include "ResourceLocator.h"
#include "MessageChannel.h"
#include "FileDescriptor.h"
#include "Logging.h"
#include "Exceptions.h"
#include "Utils.h"
#include "Utils/Timer.h"

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
	static const unsigned int MESSAGE_SERVER_STACK_SIZE =
		#ifdef __FreeBSD__
			// localtime() on FreeBSD needs some more stack space.
			1024 * 96;
		#else
			1024 * 64;
		#endif
	
	ServerInstanceDir serverInstanceDir;
	ServerInstanceDir::GenerationPtr generation;
	FileDescriptor feedbackFd;
	MessageChannel feedbackChannel;
	AnalyticsLoggerPtr analyticsLogger;
	AccountsDatabasePtr accountsDatabase;
	MessageServerPtr messageServer;
	ApplicationPool::PoolPtr pool;
	ResourceLocator resourceLocator;
	shared_ptr<oxt::thread> prestarterThread;
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
	 * Lowers this process's privilege to that of <em>username</em> and <em>groupname</em>.
	 */
	void lowerPrivilege(const string &username, const string &groupname) {
		struct passwd *userEntry;
		struct group  *groupEntry;
		int            e;
		
		userEntry = getpwnam(username.c_str());
		if (userEntry == NULL) {
			throw NonExistentUserException(string("Unable to lower Passenger "
				"HelperServer's privilege to that of user '") + username +
				"': user does not exist.");
		}
		groupEntry = getgrnam(groupname.c_str());
		if (groupEntry == NULL) {
			throw NonExistentGroupException(string("Unable to lower Passenger "
				"HelperServer's privilege to that of user '") + username +
				"': user does not exist.");
		}
		
		if (initgroups(username.c_str(), userEntry->pw_gid) != 0) {
			e = errno;
			throw SystemException(string("Unable to lower Passenger HelperServer's "
				"privilege to that of user '") + username +
				"': cannot set supplementary groups for this user", e);
		}
		if (setgid(groupEntry->gr_gid) != 0) {
			e = errno;
			throw SystemException(string("Unable to lower Passenger HelperServer's "
				"privilege to that of user '") + username +
				"': cannot set group ID", e);
		}
		if (setuid(userEntry->pw_uid) != 0) {
			e = errno;
			throw SystemException(string("Unable to lower Passenger HelperServer's "
				"privilege to that of user '") + username +
				"': cannot set user ID", e);
		}
	}
	
public:
	Server(FileDescriptor feedbackFd,
		pid_t webServerPid, const string &tempDir,
		bool userSwitching, const string &defaultUser, const string &defaultGroup,
		const string &passengerRoot, const string &rubyCommand,
		unsigned int generationNumber, unsigned int maxPoolSize,
		unsigned int maxInstancesPerApp, unsigned int poolIdleTime,
		const VariantMap &options)
		: serverInstanceDir(webServerPid, tempDir, false),
		  resourceLocator(passengerRoot)
	{
		TRACE_POINT();
		string messageSocketPassword;
		string loggingAgentPassword;
		
		this->feedbackFd  = feedbackFd;
		feedbackChannel   = MessageChannel(feedbackFd);
		
		UPDATE_TRACE_POINT();
		messageSocketPassword = Base64::decode(options.get("message_socket_password"));
		loggingAgentPassword  = options.get("logging_agent_password");
		
		generation       = serverInstanceDir.getGeneration(generationNumber);
		accountsDatabase = AccountsDatabase::createDefault(generation,
			userSwitching, defaultUser, defaultGroup);
		accountsDatabase->add("_web_server", messageSocketPassword, false,
			Account::GET | Account::DETACH | Account::SET_PARAMETERS | Account::EXIT);
		messageServer = ptr(new MessageServer(generation->getPath() + "/socket", accountsDatabase));
		
		createFile(generation->getPath() + "/helper_server.pid",
			toString(getpid()), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		
		if (geteuid() == 0 && !userSwitching) {
			lowerPrivilege(defaultUser, defaultGroup);
		}
		
		UPDATE_TRACE_POINT();
		analyticsLogger = ptr(new AnalyticsLogger(options.get("logging_agent_address"),
			"logging", loggingAgentPassword));
		
		pool = ptr(new ApplicationPool::Pool(
			resourceLocator.getSpawnServerFilename(), generation,
			accountsDatabase, rubyCommand,
			analyticsLogger,
			options.getInt("log_level"),
			options.get("debug_log_file", false)
		));
		pool->setMax(maxPoolSize);
		pool->setMaxPerApp(maxInstancesPerApp);
		pool->setMaxIdleTime(poolIdleTime);
		
		messageServer->addHandler(ptr(new TimerUpdateHandler(exitTimer)));
		messageServer->addHandler(ptr(new ApplicationPool::Server(pool, analyticsLogger)));
		messageServer->addHandler(ptr(new BacktracesServer()));
		messageServer->addHandler(ptr(new ExitHandler(exitEvent)));
		
		UPDATE_TRACE_POINT();
		feedbackChannel.write("initialized",
			"",  // Request socket filename; not available in the Apache helper server.
			messageServer->getSocketFilename().c_str(),
			NULL);
		
		prestarterThread = ptr(new oxt::thread(
			boost::bind(prestartWebApps, resourceLocator, options.get("prestart_urls"))
		));
	}
	
	~Server() {
		TRACE_POINT();
		prestarterThread->interrupt_and_join();
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

int
main(int argc, char *argv[]) {
	TRACE_POINT();
	VariantMap options = initializeAgent(argc, argv, "PassengerHelperAgent");
	pid_t   webServerPid  = options.getPid("web_server_pid");
	string  tempDir       = options.get("temp_dir");
	bool    userSwitching = options.getBool("user_switching");
	string  defaultUser   = options.get("default_user");
	string  defaultGroup  = options.get("default_group");
	string  passengerRoot = options.get("passenger_root");
	string  rubyCommand   = options.get("ruby");
	unsigned int generationNumber   = options.getInt("generation_number");
	unsigned int maxPoolSize        = options.getInt("max_pool_size");
	unsigned int maxInstancesPerApp = options.getInt("max_instances_per_app");
	unsigned int poolIdleTime       = options.getInt("pool_idle_time");
	
	try {
		UPDATE_TRACE_POINT();
		Server server(FEEDBACK_FD, webServerPid, tempDir,
			userSwitching, defaultUser, defaultGroup,
			passengerRoot, rubyCommand, generationNumber,
			maxPoolSize, maxInstancesPerApp, poolIdleTime,
			options);
		
		UPDATE_TRACE_POINT();
		server.mainLoop();
	} catch (const tracable_exception &e) {
		P_ERROR(e.what() << "\n" << e.backtrace());
		return 1;
	} catch (const std::exception &e) {
		P_ERROR(e.what());
		return 1;
	} catch (...) {
		P_ERROR("Unknown exception thrown in main thread.");
		throw;
	}
	
	return 0;
}
