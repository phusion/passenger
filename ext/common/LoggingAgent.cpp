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

#include "AccountsDatabase.h"
#include "Account.h"
#include "Timer.h"
#include "Base64.h"
#include "ServerInstanceDir.h"
#include "MessageServer.h"
#include "LoggingServer.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace oxt;
using namespace Passenger;



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


static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

/**
 * Lowers this process's privilege to user <em>username</em> and
 * group <em>groupname</em>. <em>groupname</em> may be the empty string,
 * in which case <em>username</em>'s default group will be used.
 */
static void
lowerPrivilege(const string &username, const string &groupname = "") {
	struct passwd *entry;
	
	entry = getpwnam(username.c_str());
	if (entry != NULL) {
		gid_t groupId;
		
		if (initgroups(username.c_str(), entry->pw_gid) != 0) {
			int e = errno;
			P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
				"privilege to that of user '" << username <<
				"': cannot set supplementary groups for this "
				"user: " << strerror(e) << " (" << e << ")");
		}
		if (groupname.empty()) {
			groupId = entry->pw_gid;
		} else {
			struct group *group;
			
			group = getgrnam(groupname.c_str());
			if (group == NULL) {
				P_WARN("WARNING: Group '" << groupname <<
					"' not found; using default group for user '" <<
					username << "' instead.");
				groupId = entry->pw_gid;
			} else {
				groupId = group->gr_gid;
			}
		}
		if (setgid(groupId) != 0) {
			int e = errno;
			P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
				"privilege to that of user '" << username <<
				"': cannot set group ID to " << groupId <<
				": " << strerror(e) <<
				" (" << e << ")");
		}
		if (setuid(entry->pw_uid) != 0) {
			int e = errno;
			P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
				"privilege to that of user '" << username <<
				"': cannot set user ID: " << strerror(e) <<
				" (" << e << ")");
		}
	} else {
		P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
			"privilege to that of user '" << username <<
			"': user does not exist.");
	}
}

int
main(int argc, char *argv[]) {
	int    feedbackFd       = atoi(argv[1]);
	pid_t  webServerPid     = (pid_t) atoll(argv[2]);
	string tempDir          = argv[3];
	int    generationNumber = atoi(argv[4]);
	string loggingDir       = argv[5];
	string username         = argv[6];
	string groupname        = argv[7];
	
	/********** Boilerplate environment setup code.... **********/
	
	/* Become the process group leader so that the watchdog can kill this
	 * app as well as all descendant processes. */
	setpgrp();
	
	ignoreSigpipe();
	setup_syscall_interruption_support();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	// Change process title.
	strncpy(argv[0], "PassengerLoggingAgent", strlen(argv[0]));
	for (int i = 1; i < argc; i++) {
		memset(argv[i], '\0', strlen(argv[i]));
	}
	
	
	try {
		/********** Now begins the real initialization **********/
		
		/* Create all the necessary objects... */
		ServerInstanceDirPtr serverInstanceDir;
		ServerInstanceDir::GenerationPtr generation;
		AccountsDatabasePtr  accountsDatabase;
		MessageServerPtr     messageServer;
		
		serverInstanceDir = ptr(new ServerInstanceDir(webServerPid, tempDir, false));
		generation = serverInstanceDir->getGeneration(generationNumber);
		accountsDatabase = ptr(new AccountsDatabase());
		messageServer = ptr(new MessageServer(generation->getPath() + "/logging.socket",
			accountsDatabase));
		
		// TODO: check whether this logic is right....
		if (username.empty()) {
			username = "nobody";
		}
		if (groupname.empty()) {
			groupname = "nobody";
		}
		
		/* Create the logging directory if necessary. */
		if (getFileType(loggingDir) == FT_NONEXISTANT) {
			if (geteuid() == 0) {
				struct passwd *user = getpwnam(username.c_str());
				struct group *group = getgrnam(groupname.c_str());
				
				if (user == NULL) {
					P_ERROR("Cannot create directory " << loggingDir <<
						" with owner '" << username <<
						"': user does not exist");
					return 1;
				}
				if (group == NULL) {
					P_ERROR("Cannot create directory " << loggingDir <<
						" with group '" << username <<
						"': group does not exist");
					return 1;
				}
				makeDirTree(loggingDir, "u=rwx,g=,o=", user->pw_uid, group->gr_gid);
			} else {
				makeDirTree(loggingDir);
			}
		}
		
		/* Now's a good time to lower the privilege. */
		if (geteuid() == 0) {
			if (username.empty()) {
				// TODO: autodetect the logging dir's owner
				lowerPrivilege("nobody");
			} else {
				lowerPrivilege(username, groupname);
			}
		}
		
		/* Retrieve desired password for protecting the logging socket */
		MessageChannel feedbackChannel(feedbackFd);
		vector<string> args;
		
		if (!feedbackChannel.read(args)) {
			throw IOException("The watchdog unexpectedly closed the connection.");
		} else if (args[0] != "logging socket password") {
			throw IOException("Unexpected input message '" + args[0] + "'");
		}
		
		/* Now setup the actual logging server. */
		oxt::thread *messageServerThread;
		Timer        exitTimer;
		EventFd      exitEvent;
		
		accountsDatabase->add("logging", Base64::decode(args[1]), false);
		messageServer->addHandler(ptr(new TimerUpdateHandler(exitTimer)));
		messageServer->addHandler(ptr(new LoggingServer(loggingDir)));
		messageServer->addHandler(ptr(new ExitHandler(exitEvent)));
		messageServerThread = new oxt::thread(
			boost::bind(&MessageServer::mainLoop, messageServer.get())
		);
		
		
		/********** Initialized! Enter main loop... **********/
		
		feedbackChannel.write("initialized", NULL);
		
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
		if (syscalls::select(largestFd + 1, &fds, NULL, NULL, NULL) == -1) {
			int e = errno;
			throw SystemException("select() failed", e);
		}
		
		if (FD_ISSET(feedbackFd, &fds)) {
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
			/* We received an exit command. We want to exit 5 seconds after
			 * the last client has disconnected, .
			 */
			exitTimer.start();
			exitTimer.wait(5000);
		}
		
		messageServerThread->interrupt_and_join();
		
		return 0;
	} catch (const tracable_exception &e) {
		P_ERROR(e.what() << "\n" << e.backtrace());
		return 1;
	}
}
