/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
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
#include <algorithm>
#include <stdlib.h>
#include <signal.h>

#include <agents/Base.h>
#include <agents/LoggingAgent/OptionParser.h>
#include <agents/LoggingAgent/LoggingServer.h>
#include <agents/LoggingAgent/AdminServer.h>

#include <AccountsDatabase.h>
#include <Account.h>
#include <Exceptions.h>
#include <FileDescriptor.h>
#include <BackgroundEventLoop.h>
#include <ResourceLocator.h>
#include <MessageServer.h>
#include <Constants.h>
#include <Utils.h>
#include <Utils/IOUtils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/VariantMap.h>

using namespace oxt;
using namespace Passenger;


/***** Constants and working objects *****/

namespace Passenger {
namespace LoggingAgent {
	struct WorkingObjects {
		string password;
		FileDescriptor serverSocketFd;
		vector<int> adminSockets;
		vector<LoggingAgent::AdminServer::Authorization> adminAuthorizations;

		ResourceLocator *resourceLocator;
		BackgroundEventLoop *bgloop;
		ServerKit::Context *serverKitContext;
		AccountsDatabasePtr accountsDatabase;
		LoggingServer *loggingServer;

		LoggingAgent::AdminServer *adminServer;
		EventFd exitEvent;
		EventFd allClientsDisconnectedEvent;

		struct ev_signal sigintWatcher;
		struct ev_signal sigtermWatcher;
		struct ev_signal sigquitWatcher;
		unsigned int terminationCount;

		WorkingObjects()
			: resourceLocator(NULL),
			  bgloop(NULL),
			  serverKitContext(NULL),
			  loggingServer(NULL),
			  terminationCount(0)
			{ }
	};
} // namespace LoggingAgent
} // namespace Passenger

using namespace Passenger::LoggingAgent;

static VariantMap *agentsOptions;
static WorkingObjects *workingObjects;


/***** Functions *****/

static void printInfo(EV_P_ struct ev_signal *watcher, int revents);
static void onTerminationSignal(EV_P_ struct ev_signal *watcher, int revents);
static void adminServerShutdownFinished(LoggingAgent::AdminServer *server);
static void waitForExitEvent();

void
loggingAgentFeedbackFdBecameReadable(ev::io &watcher, int revents) {
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
makeFileWorldReadableAndWritable(const string &path) {
	int ret;

	do {
		ret = chmod(path.c_str(), parseModeString("u=rw,g=rw,o=rw"));
	} while (ret == -1 && errno == EINTR);
}

static void
parseAndAddAdminAuthorization(const string &description) {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	LoggingAgent::AdminServer::Authorization auth;
	vector<string> args;

	split(description, ':', args);

	if (args.size() == 2) {
		auth.level = LoggingAgent::AdminServer::FULL;
		auth.username = args[0];
		auth.password = strip(readAll(args[1]));
	} else if (args.size() == 3) {
		auth.level = LoggingAgent::AdminServer::parseLevel(args[0]);
		auth.username = args[1];
		auth.password = strip(readAll(args[2]));
	} else {
		P_BUG("Too many elements in authorization description");
	}

	wo->adminAuthorizations.push_back(auth);
}

static void
initializePrivilegedWorkingObjects() {
	TRACE_POINT();
	const VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects = new WorkingObjects();

	wo->password = options.get("logging_agent_password", false);
	if (wo->password.empty()) {
		wo->password = strip(readAll(options.get("logging_agent_password_file")));
	}

	vector<string> authorizations = options.getStrSet("logging_agent_authorizations",
		false);
	string description;

	UPDATE_TRACE_POINT();
	foreach (description, authorizations) {
		parseAndAddAdminAuthorization(description);
	}
}

static void
startListening() {
	TRACE_POINT();
	const VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects;
	string address;
	vector<string> adminAddresses;

	address = options.get("logging_agent_address");
	wo->serverSocketFd = createServer(address.c_str());
	if (getSocketAddressType(address) == SAT_UNIX) {
		makeFileWorldReadableAndWritable(parseUnixSocketAddress(address));
	}

	UPDATE_TRACE_POINT();
	adminAddresses = options.getStrSet("logging_agent_admin_addresses",
		false);
	foreach (address, adminAddresses) {
		wo->adminSockets.push_back(createServer(address));
		if (getSocketAddressType(address) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(address));
		}
	}
}

static void
lowerPrivilege() {
	TRACE_POINT();
	const VariantMap &options = *agentsOptions;
	string userName = options.get("analytics_log_user", false);

	if (geteuid() == 0 && !userName.empty()) {
		string groupName = options.get("analytics_log_group", false);
		struct passwd *pwUser = getpwnam(userName.c_str());
		gid_t gid;

		if (pwUser == NULL) {
			throw RuntimeException("Cannot lookup user information for user " +
				userName);
		}

		if (groupName.empty()) {
			gid = pwUser->pw_gid;
			groupName = getGroupName(pwUser->pw_gid);
		} else {
			gid = lookupGid(groupName);
		}

		if (initgroups(userName.c_str(), gid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " AGENT_EXE " logger's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set supplementary groups", e);
		}
		if (setgid(gid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " AGENT_EXE " logger's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set group ID to " + toString(gid), e);
		}
		if (setuid(pwUser->pw_uid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " AGENT_EXE " logger's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set user ID to " + toString(pwUser->pw_uid), e);
		}

		setenv("USER", pwUser->pw_name, 1);
		setenv("HOME", pwUser->pw_dir, 1);
		setenv("UID", toString(gid).c_str(), 1);
	}
}

static void
initializeUnprivilegedWorkingObjects() {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects;
	int fd;

	wo->resourceLocator = new ResourceLocator(options.get("passenger_root"));
	options.set("union_station_gateway_cert", findUnionStationGatewayCert(
		*wo->resourceLocator, options.get("union_station_gateway_cert", false)));

	UPDATE_TRACE_POINT();
	wo->bgloop = new BackgroundEventLoop(true, true);
	wo->serverKitContext = new ServerKit::Context(wo->bgloop->safe);

	UPDATE_TRACE_POINT();
	wo->accountsDatabase = boost::make_shared<AccountsDatabase>();
	wo->accountsDatabase->add("logging", wo->password, false);
	wo->loggingServer = new LoggingServer(wo->bgloop->loop,
		wo->serverSocketFd, wo->accountsDatabase, options);

	UPDATE_TRACE_POINT();
	wo->adminServer = new LoggingAgent::AdminServer(wo->serverKitContext);
	wo->adminServer->loggingServer = wo->loggingServer;
	wo->adminServer->exitEvent = &wo->exitEvent;
	wo->adminServer->shutdownFinishCallback = adminServerShutdownFinished;
	wo->adminServer->authorizations = wo->adminAuthorizations;
	foreach (fd, wo->adminSockets) {
		wo->adminServer->listen(fd);
	}

	UPDATE_TRACE_POINT();
	ev_signal_init(&wo->sigquitWatcher, printInfo, SIGQUIT);
	ev_signal_start(wo->bgloop->loop, &wo->sigquitWatcher);
	ev_signal_init(&wo->sigintWatcher, onTerminationSignal, SIGINT);
	ev_signal_start(wo->bgloop->loop, &wo->sigintWatcher);
	ev_signal_init(&wo->sigtermWatcher, onTerminationSignal, SIGTERM);
	ev_signal_start(wo->bgloop->loop, &wo->sigtermWatcher);
}

static void
reportInitializationInfo() {
	TRACE_POINT();

	P_NOTICE(AGENT_EXE " logger online, PID " << getpid());
	if (feedbackFdAvailable()) {
		writeArrayMessage(FEEDBACK_FD,
			"initialized",
			NULL);
	}
}

static void
printInfo(EV_P_ struct ev_signal *watcher, int revents) {
	cerr << "---------- Begin LoggingAgent status ----------\n";
	workingObjects->loggingServer->dump(cerr);
	cerr.flush();
	cerr << "---------- End LoggingAgent status   ----------\n";
}

static void
onTerminationSignal(EV_P_ struct ev_signal *watcher, int revents) {
	WorkingObjects *wo = workingObjects;

	// Start output after '^C'
	printf("\n");

	wo->terminationCount++;
	if (wo->terminationCount < 3) {
		P_NOTICE("Signal received. Gracefully shutting down... (send signal " <<
			(3 - wo->terminationCount) << " more time(s) to force shutdown)");
		workingObjects->exitEvent.notify();
	} else {
		P_NOTICE("Signal received. Forcing shutdown.");
		_exit(2);
	}
}

static void
mainLoop() {
	workingObjects->bgloop->start("Main event loop", 0);
	waitForExitEvent();
}

static void
shutdownAdminServer() {
	workingObjects->adminServer->shutdown();
}

static void
adminServerShutdownFinished(LoggingAgent::AdminServer *server) {
	workingObjects->allClientsDisconnectedEvent.notify();
}

/* Wait until the watchdog closes the feedback fd (meaning it
 * was killed) or until we receive an exit message.
 */
static void
waitForExitEvent() {
	this_thread::disable_syscall_interruption dsi;
	WorkingObjects *wo = workingObjects;
	fd_set fds;
	int largestFd = -1;

	FD_ZERO(&fds);
	if (feedbackFdAvailable()) {
		FD_SET(FEEDBACK_FD, &fds);
		largestFd = std::max(largestFd, FEEDBACK_FD);
	}
	FD_SET(wo->exitEvent.fd(), &fds);
	largestFd = std::max(largestFd, wo->exitEvent.fd());

	TRACE_POINT();
	if (syscalls::select(largestFd + 1, &fds, NULL, NULL, NULL) == -1) {
		int e = errno;
		throw SystemException("select() failed", e);
	}

	if (FD_ISSET(FEEDBACK_FD, &fds)) {
		UPDATE_TRACE_POINT();
		/* If the watchdog has been killed then we'll exit. There's no
		 * point in keeping the logging agent running because we can't
		 * detect when the web server exits, and because this logging
		 * agent doesn't own the instance directory. As soon as
		 * passenger-status is run, the instance directory will be
		 * cleaned up, making the server inaccessible.
		 */
		_exit(2);
	} else {
		UPDATE_TRACE_POINT();
		/* We received an exit command. */
		P_NOTICE("Received command to shutdown gracefully. "
			"Waiting until all clients have disconnected...");
		wo->bgloop->safe->runLater(shutdownAdminServer);

		UPDATE_TRACE_POINT();
		FD_ZERO(&fds);
		FD_SET(wo->allClientsDisconnectedEvent.fd(), &fds);
		if (syscalls::select(wo->allClientsDisconnectedEvent.fd() + 1,
			&fds, NULL, NULL, NULL) == -1)
		{
			int e = errno;
			throw SystemException("select() failed", e);
		}

		P_INFO("All clients have now disconnected. Proceeding with graceful shutdown");
	}
}

static void
cleanup() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;

	P_DEBUG("Shutting down " AGENT_EXE " logger...");
	wo->bgloop->stop();
	delete wo->adminServer;
	P_NOTICE(AGENT_EXE " logger shutdown finished");
}

static int
runLoggingAgent() {
	TRACE_POINT();
	P_NOTICE("Starting " AGENT_EXE " logger...");

	try {
		UPDATE_TRACE_POINT();
		initializePrivilegedWorkingObjects();
		startListening();
		lowerPrivilege();
		initializeUnprivilegedWorkingObjects();

		UPDATE_TRACE_POINT();
		reportInitializationInfo();
		mainLoop();

		UPDATE_TRACE_POINT();
		cleanup();
	} catch (const tracable_exception &e) {
		P_ERROR("ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}

	return 0;
}


/***** Entry point and command line argument parsing *****/

static void
parseOptions(int argc, const char *argv[], VariantMap &options) {
	OptionParser p(loggingAgentUsage);
	int i = 2;

	while (i < argc) {
		if (parseLoggingAgentOption(argc, argv, i, options)) {
			continue;
		} else if (p.isFlag(argv[i], 'h', "--help")) {
			loggingAgentUsage();
			exit(0);
		} else {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s logger --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		}
	}
}

static void
preinitialize(VariantMap &options) {
	// Set log_level here so that initializeAgent() calls setLogLevel()
	// and setLogFile() with the right value.
	if (options.has("logging_agent_log_level")) {
		options.setInt("log_level", options.getInt("logging_agent_log_level"));
	}
	if (options.has("logging_agent_log_file")) {
		options.setInt("debug_log_file", options.getInt("logging_agent_log_file"));
	}
}

static void
setAgentsOptionsDefaults() {
	VariantMap &options = *agentsOptions;
	set<string> defaultAdminListenAddress;
	defaultAdminListenAddress.insert(DEFAULT_LOGGING_AGENT_ADMIN_LISTEN_ADDRESS);

	options.setDefault("logging_agent_address", DEFAULT_LOGGING_AGENT_LISTEN_ADDRESS);
	options.setDefaultStrSet("logging_agent_admin_addresses", defaultAdminListenAddress);
}

static void
sanityCheckOptions() {
	VariantMap &options = *agentsOptions;
	string webServerType = options.get("web_server_type", false);
	bool ok = true;

	if (!options.has("passenger_root")) {
		fprintf(stderr, "ERROR: please set the --passenger-root argument.\n");
		ok = false;
	}

	if (!options.has("logging_agent_password")
	 && !options.has("logging_agent_password_file"))
	{
		fprintf(stderr, "ERROR: please set the --password-file argument.\n");
		ok = false;
	}

	// Sanity check user accounts
	string user = options.get("analytics_log_user", false);
	if (!user.empty()) {
		struct passwd *pwUser = getpwnam(user.c_str());
		if (pwUser == NULL) {
			fprintf(stderr, "ERROR: the username specified by --user, '%s', does not exist.\n",
				user.c_str());
			ok = false;
		}

		string group = options.get("analytics_log_group", false);
		if (!group.empty() && lookupGid(group) == (gid_t) -1) {
			fprintf(stderr, "ERROR: the group name specified by --group, '%s', does not exist.\n",
				group.c_str());
			ok = false;
		}
	} else if (options.has("analytics_log_group")) {
		fprintf(stderr, "ERROR: setting --group also requires you to set --user.\n");
		ok = false;
	}

	if (!ok) {
		exit(1);
	}
}

int
loggingAgentMain(int argc, char *argv[]) {
	agentsOptions = new VariantMap();
	*agentsOptions = initializeAgent(argc, &argv, AGENT_EXE " logger",
		parseOptions, preinitialize, 2);

	CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
	if (code != CURLE_OK) {
		P_CRITICAL("ERROR: Could not initialize libcurl: " << curl_easy_strerror(code));
		exit(1);
	}

	setAgentsOptionsDefaults();
	sanityCheckOptions();
	return runLoggingAgent();
}
