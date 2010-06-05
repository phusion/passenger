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
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/thread.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "../AgentBase.h"
#include "../AccountsDatabase.h"
#include "../Account.h"
#include "../ServerInstanceDir.h"
#include "LoggingServer.h"
#include "../Exceptions.h"
#include "../Utils.h"
#include "../Utils/IOUtils.h"
#include "../Utils/Base64.h"
#include "../Utils/VariantMap.h"

using namespace oxt;
using namespace Passenger;


static struct ev_loop *eventLoop;

static struct ev_loop *
createEventLoop() {
	struct ev_loop *loop;
	
	// libev doesn't like choosing epoll and kqueue because the author thinks they're broken,
	// so let's try to force it.
	loop = ev_default_loop(EVBACKEND_EPOLL);
	if (loop == NULL) {
		loop = ev_default_loop(EVBACKEND_KQUEUE);
	}
	if (loop == NULL) {
		loop = ev_default_loop(0);
	}
	if (loop == NULL) {
		throw RuntimeException("Cannot create an event loop");
	} else {
		return loop;
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
feedbackFdBecameReadable(ev::io &watcher, int revents) {
	/* This event indicates that the watchdog has been killed.
	 * In this case we'll kill all descendant
	 * processes and exit. There's no point in keeping this agent
	 * running because we can't detect when the web server exits,
	 * and because this agent doesn't own the server instance
	 * directory. As soon as passenger-status is run, the server
	 * instance directory will be cleaned up, making this agent's
	 * services inaccessible.
	 */
	syscalls::killpg(getpgrp(), SIGKILL);
	_exit(2); // In case killpg() fails.
}

int
main(int argc, char *argv[]) {
	VariantMap options        = initializeAgent(argc, argv, "PassengerLoggingAgent");
	string socketAddress      = options.get("logging_agent_address");
	string loggingDir         = options.get("analytics_log_dir");
	string username           = options.get("analytics_log_user");
	string groupname          = options.get("analytics_log_group");
	string permissions        = options.get("analytics_log_permissions");
	string password           = options.get("logging_agent_password");
	
	try {
		/********** Now begins the real initialization **********/
		
		/* Create all the necessary objects and sockets... */
		AccountsDatabasePtr  accountsDatabase;
		FileDescriptor       serverSocketFd;
		struct passwd       *user;
		struct group        *group;
		int                  ret;
		
		eventLoop = createEventLoop();
		accountsDatabase = ptr(new AccountsDatabase());
		serverSocketFd = createServer(socketAddress.c_str());
		if (getSocketAddressType(socketAddress) == SAT_UNIX) {
			do {
				ret = chmod(parseUnixSocketAddress(socketAddress).c_str(),
					S_ISVTX |
					S_IRUSR | S_IWUSR | S_IXUSR |
					S_IRGRP | S_IWGRP | S_IXGRP |
					S_IROTH | S_IWOTH | S_IXOTH);
			} while (ret == -1 && errno == EINTR);
		}
		
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
		
		/* Now setup the actual logging server. */
		accountsDatabase->add("logging", Base64::decode(password), false);
		LoggingServer server(eventLoop, serverSocketFd,
			accountsDatabase, loggingDir);
		
		if (feedbackFdAvailable()) {
			MessageChannel feedbackChannel(FEEDBACK_FD);
			ev::io feedbackFdWatcher(eventLoop);
			feedbackFdWatcher.set<&feedbackFdBecameReadable>();
			feedbackFdWatcher.start(FEEDBACK_FD, ev::READ);
			feedbackChannel.write("initialized", NULL);
		}
		
		
		/********** Initialized! Enter main loop... **********/
		
		ev_loop(eventLoop, 0);
		return 0;
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}
}
