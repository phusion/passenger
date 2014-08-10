/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
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
#include <stdlib.h>
#include <signal.h>

#include <agents/Base.h>
#include <agents/LoggingAgent/LoggingServer.h>
#include <agents/LoggingAgent/AdminController.h>

#include <AccountsDatabase.h>
#include <Account.h>
#include <Exceptions.h>
#include <ResourceLocator.h>
#include <MessageServer.h>
#include <Utils.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/Base64.h>
#include <Utils/VariantMap.h>

using namespace oxt;
using namespace Passenger;


/***** Agent options *****/

static VariantMap agentsOptions;
static string passengerRoot;
static string socketAddress;
static string adminSocketAddress;
static string password;
static string username;
static string groupname;
static string adminToolStatusPassword;

/***** Constants and working objects *****/

static const int MESSAGE_SERVER_THREAD_STACK_SIZE = 128 * 1024;

struct WorkingObjects {
	ResourceLocatorPtr resourceLocator;
	FileDescriptor serverSocketFd;
	AccountsDatabasePtr adminAccountsDatabase;
	MessageServerPtr adminServer;
	boost::shared_ptr<oxt::thread> adminServerThread;
	AccountsDatabasePtr accountsDatabase;
	LoggingServerPtr loggingServer;

	~WorkingObjects() {
		// Stop thread before destroying anything else.
		if (adminServerThread != NULL) {
			adminServerThread->interrupt_and_join();
		}
	}
};

static struct ev_loop *eventLoop = NULL;
static LoggingServer *loggingServer = NULL;
static int exitCode = 0;


/***** Functions *****/

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

static string
myself() {
	struct passwd *entry = getpwuid(geteuid());
	if (entry != NULL) {
		return entry->pw_name;
	} else {
		throw NonExistentUserException(string("The current user, UID ") +
			toString(geteuid()) + ", doesn't have a corresponding " +
			"entry in the system's user database. Please fix your " +
			"system's user database first.");
	}
}

static void
initializeBareEssentials(int argc, char *argv[]) {
	agentsOptions = initializeAgent(argc, argv, "PassengerLoggingAgent");
	curl_global_init(CURL_GLOBAL_ALL);
	if (agentsOptions.get("test_binary", false) == "1") {
		printf("PASS\n");
		exit(0);
	}
}

static string
findUnionStationGatewayCert(const ResourceLocator &locator,
	const string &cert)
{
	if (cert.empty()) {
		return locator.getResourcesDir() + "/union_station_gateway.crt";
	} else if (cert != "-") {
		return cert;
	} else {
		return "";
	}
}

static void
initializeOptions(WorkingObjects &wo) {
	passengerRoot      = agentsOptions.get("passenger_root");
	socketAddress      = agentsOptions.get("logging_agent_address");
	adminSocketAddress = agentsOptions.get("logging_agent_admin_address");
	password           = agentsOptions.get("logging_agent_password");
	username           = agentsOptions.get("analytics_log_user", false, myself());
	groupname          = agentsOptions.get("analytics_log_group", false);
	adminToolStatusPassword = agentsOptions.get("admin_tool_status_password");

	wo.resourceLocator = boost::make_shared<ResourceLocator>(passengerRoot);
	agentsOptions.set("union_station_gateway_cert", findUnionStationGatewayCert(
		*wo.resourceLocator, agentsOptions.get("union_station_gateway_cert", false)));
}

static void
initializePrivilegedWorkingObjects(WorkingObjects &wo) {
	wo.serverSocketFd = createServer(socketAddress.c_str());
	if (getSocketAddressType(socketAddress) == SAT_UNIX) {
		int ret;

		do {
			ret = chmod(parseUnixSocketAddress(socketAddress).c_str(),
				S_ISVTX |
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IWGRP | S_IXGRP |
				S_IROTH | S_IWOTH | S_IXOTH);
		} while (ret == -1 && errno == EINTR);
	}

	wo.adminAccountsDatabase = boost::make_shared<AccountsDatabase>();
	wo.adminAccountsDatabase->add("_passenger-status", adminToolStatusPassword, false);
	wo.adminServer = boost::make_shared<MessageServer>(parseUnixSocketAddress(adminSocketAddress),
		wo.adminAccountsDatabase);
}

static void
lowerPrivilege(const string &username, const struct passwd *user, gid_t gid) {
	int e;

	if (initgroups(username.c_str(), gid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to set supplementary groups for " <<
			"PassengerLoggingAgent: " << strerror(e) << " (" << e << ")");
	}
	if (setgid(gid) != 0) {
		e = errno;
		P_WARN("WARNING: Unable to lower PassengerLoggingAgent's "
			"privilege to that of user '" << username <<
			"': cannot set group ID to " << gid <<
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

	setenv("HOME", user->pw_dir, 1);
}

static void
maybeLowerPrivilege() {
	struct passwd *user;
	gid_t gid;

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
		gid = user->pw_gid;
		groupname = getGroupName(user->pw_gid);
	} else {
		gid = lookupGid(groupname);
		if (gid == (gid_t) -1) {
			throw NonExistentGroupException(string("The configuration option ") +
				"'PassengerAnalyticsLogGroup' (Apache) or " +
				"'passenger_analytics_log_group' (Nginx) was set to '" +
				groupname + "', but this group doesn't exist. Please fix " +
				"the configuration option.");
		}
	}

	/* Now's a good time to lower the privilege. */
	if (geteuid() == 0) {
		lowerPrivilege(username, user, gid);
	}
}

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
initializeUnprivilegedWorkingObjects(WorkingObjects &wo) {
	eventLoop = createEventLoop();
	wo.accountsDatabase = boost::make_shared<AccountsDatabase>();
	wo.accountsDatabase->add("logging", password, false);

	wo.loggingServer = boost::make_shared<LoggingServer>(eventLoop, wo.serverSocketFd,
		wo.accountsDatabase, agentsOptions);
	loggingServer = wo.loggingServer.get();

	wo.adminServer->addHandler(boost::make_shared<AdminController>(wo.loggingServer));
	boost::function<void ()> adminServerFunc = boost::bind(&MessageServer::mainLoop, wo.adminServer.get());
	wo.adminServerThread = boost::make_shared<oxt::thread>(
		boost::bind(runAndPrintExceptions, adminServerFunc, true),
		"AdminServer thread", MESSAGE_SERVER_THREAD_STACK_SIZE
	);
}

void
caughtExitSignal(ev::sig &watcher, int revents) {
	P_INFO("Caught signal, exiting...");
	ev_break(eventLoop, EVBREAK_ONE);
	/* We only consider the "exit" command to be a graceful way to shut down
	 * the logging agent, so upon receiving an exit signal we want to return
	 * a non-zero exit code. This is because we want the watchdog to restart
	 * the logging agent when it's killed by SIGTERM.
	 */
	exitCode = 1;
}

void
printInfo(ev::sig &watcher, int revents) {
	cerr << "---------- Begin LoggingAgent status ----------\n";
	loggingServer->dump(cerr);
	cerr.flush();
	cerr << "---------- End LoggingAgent status   ----------\n";
}

static void
runMainLoop(WorkingObjects &wo) {
	ev::io feedbackFdWatcher(eventLoop);
	ev::sig sigintWatcher(eventLoop);
	ev::sig sigtermWatcher(eventLoop);
	ev::sig sigquitWatcher(eventLoop);

	sigintWatcher.set<&caughtExitSignal>();
	sigintWatcher.start(SIGINT);
	sigtermWatcher.set<&caughtExitSignal>();
	sigtermWatcher.start(SIGTERM);
	sigquitWatcher.set<&printInfo>();
	sigquitWatcher.start(SIGQUIT);

	P_WARN("PassengerLoggingAgent online, listening at " << socketAddress);
	if (feedbackFdAvailable()) {
		feedbackFdWatcher.set<&feedbackFdBecameReadable>();
		feedbackFdWatcher.start(FEEDBACK_FD, ev::READ);
		writeArrayMessage(FEEDBACK_FD, "initialized", NULL);
	}
	ev_run(eventLoop, 0);
}

int
main(int argc, char *argv[]) {
	initializeBareEssentials(argc, argv);
	P_DEBUG("Starting PassengerLoggingAgent...");

	try {
		TRACE_POINT();
		WorkingObjects wo;

		initializeOptions(wo);
		initializePrivilegedWorkingObjects(wo);
		maybeLowerPrivilege();
		initializeUnprivilegedWorkingObjects(wo);
		runMainLoop(wo);
		P_DEBUG("Logging agent exiting with code " << exitCode << ".");
		return exitCode;
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}
}
