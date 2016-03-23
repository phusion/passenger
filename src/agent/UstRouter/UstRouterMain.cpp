/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
// Include ev++.h early to avoid macro clash on EV_ERROR.
#include <ev++.h>

#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/thread.hpp>

#include <sys/types.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <stdexcept>
#include <stdlib.h>
#include <signal.h>

#include <Shared/Base.h>
#include <Shared/ApiServerUtils.h>
#include <UstRouter/OptionParser.h>
#include <UstRouter/Controller.h>
#include <UstRouter/ApiServer.h>

#include <Exceptions.h>
#include <FileDescriptor.h>
#include <BackgroundEventLoop.h>
#include <ResourceLocator.h>
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
namespace UstRouter {
	struct WorkingObjects {
		FileDescriptor serverSocketFd;
		vector<int> apiSockets;
		ResourceLocator *resourceLocator;
		ApiAccountDatabase apiAccountDatabase;

		BackgroundEventLoop *bgloop;
		ServerKit::Context *serverKitContext;
		Controller *controller;

		BackgroundEventLoop *apiBgloop;
		ServerKit::Context *apiServerKitContext;
		UstRouter::ApiServer *apiServer;
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
			  controller(NULL),
			  apiBgloop(NULL),
			  apiServerKitContext(NULL),
			  apiServer(NULL),
			  exitEvent(__FILE__, __LINE__, "WorkingObjects: exitEvent"),
			  allClientsDisconnectedEvent(__FILE__, __LINE__, "WorkingObjects: allClientsDisconnectedEvent"),
			  terminationCount(0)
			{ }
	};
} // namespace UstRouter
} // namespace Passenger

using namespace Passenger::UstRouter;

static VariantMap *agentsOptions;
static WorkingObjects *workingObjects;


/***** Functions *****/

static void printInfo(EV_P_ struct ev_signal *watcher, int revents);
static void printInfoInThread();
static void onTerminationSignal(EV_P_ struct ev_signal *watcher, int revents);
static void apiServerShutdownFinished(UstRouter::ApiServer *server);
static void waitForExitEvent();

void
ustRouterFeedbackFdBecameReadable(ev::io &watcher, int revents) {
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
initializePrivilegedWorkingObjects() {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects = new WorkingObjects();

	options.set("ust_router_username", "logging");

	string password = options.get("ust_router_password", false);
	if (password.empty()) {
		password = strip(readAll(options.get("ust_router_password_file")));
		options.set("ust_router_password", password);
	}

	vector<string> authorizations = options.getStrSet("ust_router_authorizations",
		false);
	string description;

	UPDATE_TRACE_POINT();
	foreach (description, authorizations) {
		try {
			wo->apiAccountDatabase.add(description);
		} catch (const ArgumentException &e) {
			throw std::runtime_error(e.what());
		}
	}

	// Initialize ResourceLocator here in case passenger_root's parent
	// directory is not executable by the unprivileged user.
	wo->resourceLocator = new ResourceLocator(options.get("passenger_root"));
}

static void
setUlimits() {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;

	if (options.has("core_file_descriptor_ulimit")) {
		unsigned int number = options.getUint("core_file_descriptor_ulimit");
		struct rlimit limit;
		int ret;

		limit.rlim_cur = number;
		limit.rlim_max = number;
		do {
			ret = setrlimit(RLIMIT_NOFILE, &limit);
		} while (ret == -1 && errno == EINTR);

		if (ret == -1) {
			int e = errno;
			P_ERROR("Unable to set file descriptor ulimit to " << number
				<< ": " << strerror(e) << " (errno=" << e << ")");
		}
	}
}

static void
startListening() {
	TRACE_POINT();
	const VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects;
	string address;
	vector<string> apiAddresses;

	address = options.get("ust_router_address");
	wo->serverSocketFd.assign(createServer(address, 0, true,
		__FILE__, __LINE__), NULL, 0);
	P_LOG_FILE_DESCRIPTOR_PURPOSE(wo->serverSocketFd,
		"Server address: " << wo->serverSocketFd);
	if (getSocketAddressType(address) == SAT_UNIX) {
		makeFileWorldReadableAndWritable(parseUnixSocketAddress(address));
	}

	UPDATE_TRACE_POINT();
	apiAddresses = options.getStrSet("ust_router_api_addresses",
		false);
	foreach (address, apiAddresses) {
		wo->apiSockets.push_back(createServer(address, 0, true,
			__FILE__, __LINE__));
		P_LOG_FILE_DESCRIPTOR_PURPOSE(wo->apiSockets.back(),
			"Server address: " << wo->apiSockets.back());
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
			throw SystemException("Unable to lower " SHORT_PROGRAM_NAME " UstRouter's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set supplementary groups", e);
		}
		if (setgid(gid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " SHORT_PROGRAM_NAME " UstRouter's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set group ID to " + toString(gid), e);
		}
		if (setuid(pwUser->pw_uid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " SHORT_PROGRAM_NAME " UstRouter's privilege "
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

	options.set("union_station_gateway_cert", findUnionStationGatewayCert(
		*wo->resourceLocator, options.get("union_station_gateway_cert", false)));

	UPDATE_TRACE_POINT();
	wo->bgloop = new BackgroundEventLoop(true, true);
	wo->serverKitContext = new ServerKit::Context(wo->bgloop->safe,
		wo->bgloop->libuv_loop);
	wo->controller = new Controller(wo->serverKitContext, options);
	wo->controller->listen(wo->serverSocketFd);

	UPDATE_TRACE_POINT();
	if (!wo->apiSockets.empty()) {
		wo->apiBgloop = new BackgroundEventLoop(true, true);
		wo->apiServerKitContext = new ServerKit::Context(wo->apiBgloop->safe,
			wo->apiBgloop->libuv_loop);
		wo->apiServer = new UstRouter::ApiServer(wo->apiServerKitContext);
		wo->apiServer->controller = wo->controller;
		wo->apiServer->apiAccountDatabase = &wo->apiAccountDatabase;
		wo->apiServer->instanceDir = options.get("instance_dir", false);
		wo->apiServer->fdPassingPassword = options.get("watchdog_fd_passing_password", false);
		wo->apiServer->exitEvent = &wo->exitEvent;
		wo->apiServer->shutdownFinishCallback = apiServerShutdownFinished;
		foreach (fd, wo->apiSockets) {
			wo->apiServer->listen(fd);
		}
	}

	UPDATE_TRACE_POINT();
	ev_signal_init(&wo->sigquitWatcher, printInfo, SIGQUIT);
	ev_signal_start(wo->bgloop->libev_loop, &wo->sigquitWatcher);
	ev_signal_init(&wo->sigintWatcher, onTerminationSignal, SIGINT);
	ev_signal_start(wo->bgloop->libev_loop, &wo->sigintWatcher);
	ev_signal_init(&wo->sigtermWatcher, onTerminationSignal, SIGTERM);
	ev_signal_start(wo->bgloop->libev_loop, &wo->sigtermWatcher);
}

static void
reportInitializationInfo() {
	TRACE_POINT();
	if (feedbackFdAvailable()) {
		P_NOTICE(SHORT_PROGRAM_NAME " UstRouter online, PID " << getpid());
		writeArrayMessage(FEEDBACK_FD,
			"initialized",
			NULL);
	} else {
		vector<string> apiAddresses = agentsOptions->getStrSet("ust_router_api_addresses", false);

		P_NOTICE(SHORT_PROGRAM_NAME " UstRouter online, PID " << getpid()
			<< ", listening on " << agentsOptions->get("ust_router_address"));

		if (!apiAddresses.empty()) {
			string address;
			P_NOTICE("API server listening on " << apiAddresses.size() << " socket(s):");
			foreach (address, apiAddresses) {
				if (startsWith(address, "tcp://")) {
					address.erase(0, sizeof("tcp://") - 1);
					address.insert(0, "http://");
					address.append("/");
				}
				P_NOTICE(" * " << address);
			}
		}
	}
}

static void
printInfo(EV_P_ struct ev_signal *watcher, int revents) {
	oxt::thread(printInfoInThread, "Information printer");
}

static void
inspectControllerStateAsJson(Controller *controller, string *result) {
	*result = controller->inspectStateAsJson().toStyledString();
}

static void
getMbufStats(struct MemoryKit::mbuf_pool *input, struct MemoryKit::mbuf_pool *result) {
	*result = *input;
}

static void
printInfoInThread() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;

	cerr << "### Backtraces\n";
	cerr << "\n" << oxt::thread::all_backtraces();
	cerr << "\n";
	cerr.flush();

	string json;
	cerr << "### Controller state\n";
	wo->bgloop->safe->runSync(boost::bind(inspectControllerStateAsJson,
		wo->controller, &json));
	cerr << json;
	cerr << "\n";
	cerr.flush();

	struct MemoryKit::mbuf_pool stats;
	cerr << "### mbuf stats\n\n";
	wo->bgloop->safe->runSync(boost::bind(getMbufStats,
		&wo->serverKitContext->mbuf_pool,
		&stats));
	cerr << "nfree_mbuf_blockq    : " << stats.nfree_mbuf_blockq << "\n";
	cerr << "nactive_mbuf_blockq  : " << stats.nactive_mbuf_blockq << "\n";
	cerr << "mbuf_block_chunk_size: " << stats.mbuf_block_chunk_size << "\n";
	cerr << "\n";
	cerr.flush();
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
	if (workingObjects->apiBgloop != NULL) {
		workingObjects->apiBgloop->start("API event loop", 0);
	}
	waitForExitEvent();
}

static void
shutdownController() {
	workingObjects->controller->shutdown();
}

static void
shutdownApiServer() {
	workingObjects->apiServer->shutdown();
}

static void
apiServerShutdownFinished(UstRouter::ApiServer *server) {
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
		 * point in keeping the UstRouter running because we can't
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
		wo->bgloop->safe->runLater(shutdownController);
		if (wo->apiBgloop != NULL) {
			wo->apiBgloop->safe->runLater(shutdownApiServer);
		}

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

	P_DEBUG("Shutting down " SHORT_PROGRAM_NAME " UstRouter...");
	wo->bgloop->stop();
	if (wo->apiServer != NULL) {
		wo->apiBgloop->stop();
		delete wo->apiServer;
	}
	P_NOTICE(SHORT_PROGRAM_NAME " UstRouter shutdown finished");
}

static int
runUstRouter() {
	TRACE_POINT();
	P_NOTICE("Starting " SHORT_PROGRAM_NAME " UstRouter...");

	try {
		UPDATE_TRACE_POINT();
		initializePrivilegedWorkingObjects();
		setUlimits();
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
	} catch (const std::runtime_error &e) {
		P_CRITICAL("ERROR: " << e.what());
		return 1;
	}

	return 0;
}


/***** Entry point and command line argument parsing *****/

static void
parseOptions(int argc, const char *argv[], VariantMap &options) {
	OptionParser p(ustRouterUsage);
	int i = 2;

	while (i < argc) {
		if (parseUstRouterOption(argc, argv, i, options)) {
			continue;
		} else if (p.isFlag(argv[i], 'h', "--help")) {
			ustRouterUsage();
			exit(0);
		} else {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s ust-router --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		}
	}
}

static void
preinitialize(VariantMap &options) {
	// Set log_level here so that initializeAgent() calls setLogLevel()
	// and setLogFile() with the right value.
	if (options.has("ust_router_log_level")) {
		options.setInt("log_level", options.getInt("ust_router_log_level"));
	}
	if (options.has("ust_router_log_file")) {
		options.setInt("debug_log_file", options.getInt("ust_router_log_file"));
	}
}

static void
setAgentsOptionsDefaults() {
	VariantMap &options = *agentsOptions;

	options.setDefault("ust_router_address", DEFAULT_UST_ROUTER_LISTEN_ADDRESS);
	options.setDefault("ust_router_default_node_name", getHostName());
}

static void
sanityCheckOptions() {
	VariantMap &options = *agentsOptions;
	bool ok = true;

	if (!options.has("passenger_root")) {
		fprintf(stderr, "ERROR: please set the --passenger-root argument.\n");
		ok = false;
	}

	if (!options.has("ust_router_password")
	 && !options.has("ust_router_password_file"))
	{
		fprintf(stderr, "ERROR: please set the --password-file argument.\n");
		ok = false;
	}

	if (options.getBool("ust_router_dev_mode", false, false)) {
		if (!options.has("ust_router_dump_dir")) {
			fprintf(stderr, "ERROR: if development mode is enabled, you must also set the --dump-dir argument.\n");
			ok = false;
		} else {
			FileType ft = getFileType(options.get("ust_router_dump_dir"));
			if (ft != FT_DIRECTORY) {
				fprintf(stderr, "ERROR: '%s' is not a valid directory.\n",
					options.get("ust_router_dump_dir").c_str());
				ok = false;
			}
		}
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
ustRouterMain(int argc, char *argv[]) {
	agentsOptions = new VariantMap();
	*agentsOptions = initializeAgent(argc, &argv, SHORT_PROGRAM_NAME " ust-router",
		parseOptions, preinitialize, 2);

	CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
	if (code != CURLE_OK) {
		P_CRITICAL("ERROR: Could not initialize libcurl: " << curl_easy_strerror(code));
		exit(1);
	}

	setAgentsOptionsDefaults();
	sanityCheckOptions();

	restoreOomScore(agentsOptions);

	return runUstRouter();
}
