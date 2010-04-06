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
#include <boost/bind.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/thread.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "../AccountsDatabase.h"
#include "../Account.h"
#include "../ServerInstanceDir.h"
#include "LoggingServer.h"
#include "../Exceptions.h"
#include "../Utils.h"
#include "../Utils/Base64.h"

using namespace oxt;
using namespace Passenger;


static struct ev_loop *eventLoop;
static bool feedbackFdBecameReadable = false;


/* class TimerUpdateHandler: public MessageServer::Handler {
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
}; */


static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

static struct ev_loop *
createEventLoop() {
	// libev doesn't like choosing epoll and kqueue because the author thinks they're broken,
	// so let's try to force it.
	ev_default_loop(EVBACKEND_EPOLL);
	if (eventLoop == NULL) {
		eventLoop = ev_default_loop(EVBACKEND_KQUEUE);
	}
	if (eventLoop == NULL) {
		eventLoop = ev_default_loop(0);
	}
	if (eventLoop == NULL) {
		throw RuntimeException("Cannot create an event loop");
	} else {
		return eventLoop;
	}
}

static void
lowerPrivilege(const string &username, const struct passwd *user, const struct group *group) {
	int e;
	
	if (initgroups(username.c_str(), group->gr_gid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to set supplementary groups for " <<
			"PassengerLoggingAgent: " << strerror(e) << " (" << e << ")");
	}
	if (setgid(group->gr_gid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
			"privilege to that of user '" << username <<
			"': cannot set group ID to " << group->gr_gid <<
			": " << strerror(e) <<
			" (" << e << ")");
	}
	if (setuid(user->pw_uid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
			"privilege to that of user '" << username <<
			"': cannot set user ID: " << strerror(e) <<
			" (" << e << ")");
	}
}

void
stopLoopBecauseOfFeedbackFd(ev::io &watcher, int revents) {
	ev_unloop(eventLoop, EVUNLOOP_ONE);
	feedbackFdBecameReadable = true;
}

int
main(int argc, char *argv[]) {
	int    feedbackFd       = atoi(argv[1]);
	int    logLevel         = atoi(argv[2]);
	pid_t  webServerPid     = (pid_t) atoll(argv[3]);
	string tempDir          = argv[4];
	int    generationNumber = atoi(argv[5]);
	string loggingDir       = argv[6];
	string username         = argv[7];
	string groupname        = argv[8];
	string permissions      = argv[9];
	
	/********** Boilerplate environment setup code.... **********/
	
	ignoreSigpipe();
	setup_syscall_interruption_support();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	// Change process title.
	strncpy(argv[0], "PassengerLoggingAgent", strlen(argv[0]));
	for (int i = 1; i < argc; i++) {
		memset(argv[i], '\0', strlen(argv[i]));
	}
	
	setLogLevel(logLevel);
	
	
	try {
		/********** Now begins the real initialization **********/
		
		/* Create all the necessary objects and sockets... */
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		AccountsDatabasePtr  accountsDatabase;
		FileDescriptor       serverSocketFd;
		struct passwd       *user;
		struct group        *group;
		
		eventLoop = createEventLoop();
		serverInstanceDir = ptr(new ServerInstanceDir(webServerPid, tempDir, false));
		generation = serverInstanceDir->getGeneration(generationNumber);
		accountsDatabase = ptr(new AccountsDatabase());
		serverSocketFd = createUnixServer((generation->getPath() + "/logging.socket").c_str());
		
		/* Sanity check user accounts. */
		
		user = getpwnam(username.c_str());
		if (user == NULL) {
			throw NonExistentUserException(string("The configuration option ") +
				"'PassengerAnalyticsLogUser' (Apache) or " +
				"'passenger_analytics_log_user' (Nginx) was set to '" +
				username + "', but this user doesn't exist. Please fix " +
				"the configuration option.");
		}
		
		if (groupname.empty()) {
			group = getgrgid(user->pw_gid);
			if (group == NULL) {
				throw NonExistentGroupException(string("The configuration option ") +
					"'PassengerAnalyticsLogGroup' (Apache) or " +
					"'passenger_analytics_log_group' (Nginx) wasn't set, " +
					"so PassengerLoggingAgent tried to use the default group " +
					"for user '" + username + "' - which is GID #" +
					toString(user->pw_gid) + " - as the group for the analytics " +
					"log dir, but this GID doesn't exist. " +
					"You can solve this problem by explicitly " +
					"setting PassengerAnalyticsLogGroup (Apache) or " +
					"passenger_analytics_log_group (Nginx) to a group that " +
					"does exist. In any case, it looks like your system's user " +
					"database is broken; Phusion Passenger can work fine even " +
					"with this broken user database, but you should still fix it.");
			} else {
				groupname = group->gr_name;
			}
		} else {
			group = getgrnam(groupname.c_str());
			if (group == NULL) {
				throw NonExistentGroupException(string("The configuration option ") +
					"'PassengerAnalyticsLogGroup' (Apache) or " +
					"'passenger_analytics_log_group' (Nginx) was set to '" +
					groupname + "', but this group doesn't exist. Please fix " +
					"the configuration option.");
			}
		}
		
		/* Create the logging directory if necessary. */
		if (getFileType(loggingDir) == FT_NONEXISTANT) {
			if (geteuid() == 0) {
				makeDirTree(loggingDir, permissions, user->pw_uid, group->gr_gid);
			} else {
				makeDirTree(loggingDir, permissions);
			}
		}
		
		/* Now's a good time to lower the privilege. */
		if (geteuid() == 0) {
			lowerPrivilege(username, user, group);
		}
		
		/* Retrieve desired password for protecting the logging socket */
		MessageChannel feedbackChannel(feedbackFd);
		vector<string> args;
		
		if (!feedbackChannel.read(args)) {
			throw IOException("The watchdog unexpectedly closed the connection.");
		} else if (args[0] != "logging agent password") {
			throw IOException("Unexpected input message '" + args[0] + "'");
		}
		
		/* Now setup the actual logging server. */
		accountsDatabase->add("logging", Base64::decode(args[1]), false);
		LoggingServer server(eventLoop, serverSocketFd,
			accountsDatabase, loggingDir);
		ev::io feedbackFdWatcher(eventLoop);
		feedbackFdWatcher.set<&stopLoopBecauseOfFeedbackFd>();
		feedbackFdWatcher.start(feedbackFd, ev::READ);
		
		
		/********** Initialized! Enter main loop... **********/
		
		feedbackChannel.write("initialized", NULL);
		/* When the watchdog closes the feedback fd (meaning it was killed)
		 * or when we receive an exit message, the loop will be stopped.
		 */
		ev_loop(eventLoop, 0);
		if (feedbackFdBecameReadable) {
			/* If the watchdog has been killed then we'll kill all descendant
			 * processes and exit. There's no point in keeping this agent
			 * running because we can't detect when the web server exits,
			 * and because this agent doesn't own the server instance
			 * directory. As soon as passenger-status is run, the server
			 * instance directory will be cleaned up, making this agent's
			 * services inaccessible.
			 */
			syscalls::killpg(getpgrp(), SIGKILL);
			_exit(2); // In case killpg() fails.
		} else {
			/* We want to exit 5 seconds after the last client has exited,
			 * so install some checking watchers and timers and start the
			 * loop again.
			 */
			//ev_loop(loop, 0);
			//exitTimer.start();
			//exitTimer.wait(5000);
		}
		
		return 0;
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}
}
