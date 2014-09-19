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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <cstring>
#include <cassert>
#include <cerrno>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>

#include <set>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <sstream>

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>

#include <ev++.h>

#include <agents/HelperAgent/OptionParser.h>
#include <agents/HelperAgent/RequestHandler.h>
#include <agents/HelperAgent/AdminServer.h>
#include <agents/Base.h>
#include <Constants.h>
#include <ServerKit/Server.h>
#include <ApplicationPool2/Pool.h>
#include <MessageServer.h>
#include <MessageReadersWriters.h>
#include <FileDescriptor.h>
#include <ResourceLocator.h>
#include <BackgroundEventLoop.cpp>
#include <UnionStation/Core.h>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/Timer.h>
#include <Utils/IOUtils.h>
#include <Utils/json.h>
#include <Utils/MessageIO.h>
#include <Utils/VariantMap.h>

using namespace boost;
using namespace oxt;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

static VariantMap *agentsOptions;


#if 0
class RemoteController: public MessageServer::Handler {
private:
	struct SpecificContext: public MessageServer::ClientContext {
	};

	typedef MessageServer::CommonClientContext CommonClientContext;

	boost::shared_ptr<RequestHandler> requestHandler;
	PoolPtr pool;


	/*********************************************
	 * Message handler methods
	 *********************************************/

	void processDetachProcess(CommonClientContext &commonContext, SpecificContext *specificContext,
		const vector<string> &args)
	{
		TRACE_POINT();
		commonContext.requireRights(Account::DETACH);
		if (pool->detachProcess((pid_t) atoi(args[1]))) {
			writeArrayMessage(commonContext.fd, "true", NULL);
		} else {
			writeArrayMessage(commonContext.fd, "false", NULL);
		}
	}

	void processDetachProcessByKey(CommonClientContext &commonContext, SpecificContext *specificContext,
		const vector<string> &args)
	{
		TRACE_POINT();
		commonContext.requireRights(Account::DETACH);
		// TODO: implement this
		writeArrayMessage(commonContext.fd, "false", NULL);
	}

	bool processInspect(CommonClientContext &commonContext, SpecificContext *specificContext,
		const vector<string> &args)
	{
		TRACE_POINT();
		commonContext.requireRights(Account::INSPECT_BASIC_INFO);
		if ((args.size() - 1) % 2 != 0) {
			return false;
		}

		VariantMap options = argsToOptions(args);
		writeScalarMessage(commonContext.fd, pool->inspect(Pool::InspectOptions(options)));
		return true;
	}

	void processToXml(CommonClientContext &commonContext, SpecificContext *specificContext,
		const vector<string> &args)
	{
		TRACE_POINT();
		commonContext.requireRights(Account::INSPECT_BASIC_INFO);
		bool includeSensitiveInfo =
			commonContext.account->hasRights(Account::INSPECT_SENSITIVE_INFO) &&
			args[1] == "true";
		writeScalarMessage(commonContext.fd, pool->toXml(includeSensitiveInfo));
	}

	void processBacktraces(CommonClientContext &commonContext, SpecificContext *specificContext,
		const vector<string> &args)
	{
		TRACE_POINT();
		commonContext.requireRights(Account::INSPECT_BACKTRACES);
		writeScalarMessage(commonContext.fd, oxt::thread::all_backtraces());
	}

	void processRestartAppGroup(CommonClientContext &commonContext, SpecificContext *specificContext,
		const vector<string> &args)
	{
		TRACE_POINT();
		commonContext.requireRights(Account::RESTART);
		VariantMap options = argsToOptions(args, 2);
		RestartMethod method = RM_DEFAULT;
		if (options.get("method", false) == "blocking") {
			method = RM_BLOCKING;
		} else if (options.get("method", false) == "rolling") {
			method = RM_ROLLING;
		}
		bool result = pool->restartGroupByName(args[1], method);
		writeArrayMessage(commonContext.fd, result ? "true" : "false", NULL);
	}

	void processRequests(CommonClientContext &commonContext, SpecificContext *specificContext,
		const vector<string> &args)
	{
		TRACE_POINT();
		stringstream stream;
		commonContext.requireRights(Account::INSPECT_REQUESTS);
		//requestHandler->inspect(stream);
		writeScalarMessage(commonContext.fd, stream.str());
	}

public:
	RemoteController(const boost::shared_ptr<RequestHandler> &requestHandler, const PoolPtr &pool) {
		this->requestHandler = requestHandler;
		this->pool = pool;
	}

	virtual MessageServer::ClientContextPtr newClient(CommonClientContext &commonContext) {
		return boost::make_shared<SpecificContext>();
	}

	virtual bool processMessage(CommonClientContext &commonContext,
	                            MessageServer::ClientContextPtr &_specificContext,
	                            const vector<string> &args)
	{
		SpecificContext *specificContext = (SpecificContext *) _specificContext.get();
		try {
			if (isCommand(args, "detach_process", 1)) {
				processDetachProcess(commonContext, specificContext, args);
			} else if (isCommand(args, "detach_process_by_key", 1)) {
				processDetachProcessByKey(commonContext, specificContext, args);
			} else if (args[0] == "inspect") {
				return processInspect(commonContext, specificContext, args);
			} else if (isCommand(args, "toXml", 1)) {
				processToXml(commonContext, specificContext, args);
			} else if (isCommand(args, "backtraces", 0)) {
				processBacktraces(commonContext, specificContext, args);
			} else if (isCommand(args, "restart_app_group", 1, 99)) {
				processRestartAppGroup(commonContext, specificContext, args);
			} else if (isCommand(args, "requests", 0)) {
				processRequests(commonContext, specificContext, args);
			} else {
				return false;
			}
		} catch (const SecurityException &) {
			/* Client does not have enough rights to perform a certain action.
			 * It has already been notified of this; ignore exception and move on.
			 */
		}
		return true;
	}
};
#endif


/***** Structures, constants, global variables and forward declarations *****/

// Avoid namespace conflict with Watchdog's WorkingObjects.
namespace {
	struct WorkingObjects {
		int serverFds[SERVER_KIT_MAX_SERVER_ENDPOINTS];
		int adminServerFds[SERVER_KIT_MAX_SERVER_ENDPOINTS];
		string password;
		vector<ServerAgent::AdminServer::Authorization> adminAuthorizations;

		ResourceLocator resourceLocator;
		RandomGeneratorPtr randomGenerator;
		UnionStation::CorePtr unionStationCore;
		SpawnerConfigPtr spawnerConfig;
		SpawnerFactoryPtr spawnerFactory;
		BackgroundEventLoop *bgloop;
		struct ev_signal sigintWatcher;
		struct ev_signal sigtermWatcher;
		struct ev_signal sigquitWatcher;
		PoolPtr appPool;
		ServerKit::Context *serverKitContext;
		RequestHandler *requestHandler;
		ServerAgent::AdminServer *adminServer;
		EventFd exitEvent;
		EventFd allClientsDisconnectedEvent;
		unsigned int terminationCount;
		unsigned int shutdownCounter;

		WorkingObjects()
			: bgloop(NULL),
			  serverKitContext(NULL),
			  requestHandler(NULL),
			  adminServer(NULL),
			  terminationCount(0),
			  shutdownCounter(0)
			{ }
	};
}

static WorkingObjects *workingObjects;


/***** Server stuff *****/

static void waitForExitEvent();
static void cleanup();
static void requestHandlerShutdownFinished(RequestHandler *server);
static void adminServerShutdownFinished(ServerAgent::AdminServer *server);

static void
parseAndAddAdminAuthorization(const string &description) {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	ServerAgent::AdminServer::Authorization auth;
	vector<string> args;

	split(description, ':', args);

	if (args.size() == 2) {
		auth.level = ServerAgent::AdminServer::FULL;
		auth.username = args[0];
		auth.password = strip(readAll(args[1]));
	} else if (args.size() == 3) {
		auth.level = ServerAgent::AdminServer::parseLevel(args[0]);
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

	wo->password = options.get("server_password", false);
	if (wo->password.empty() && options.has("server_password_file")) {
		wo->password = strip(readAll(options.get("server_password_file")));
	}

	vector<string> authorizations = options.getStrSet("server_authorizations",
		false);
	string description;

	UPDATE_TRACE_POINT();
	foreach (description, authorizations) {
		parseAndAddAdminAuthorization(description);
	}
}

static void
initializeSingleAppMode() {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;

	if (options.getBool("multi_app")) {
		P_NOTICE("PassengerAgent server running in multi-application mode.");
		return;
	}

	if (!options.has("app_type")) {
		P_DEBUG("Autodetecting application type...");
		AppTypeDetector detector(NULL, 0);
		PassengerAppType appType = detector.checkAppRoot(options.get("app_root"));
		if (appType == PAT_NONE || appType == PAT_ERROR) {
			fprintf(stderr, "ERROR: unable to autodetect what kind of application "
				"lives in %s. Please specify information about the app using "
				"--app-type and --startup-file, or specify a correct location to "
				"the application you want to serve.\n"
				"Type 'PassengerAgent server --help' for more information.\n",
				options.get("app_root").c_str());
			exit(1);
		}

		options.set("app_type", getAppTypeName(appType));
		options.set("startup_file", getAppTypeStartupFile(appType));
	}

	P_NOTICE("PassengerAgent server running in single-application mode.");
	P_NOTICE("Serving app     : " << options.get("app_root"));
	P_NOTICE("App type        : " << options.get("app_type"));
	P_NOTICE("App startup file: " << options.get("startup_file"));
}

static void
startListening() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	vector<string> addresses = agentsOptions->getStrSet("server_addresses");
	vector<string> adminAddresses = agentsOptions->getStrSet("server_admin_addresses", false);

	for (unsigned int i = 0; i < addresses.size(); i++) {
		wo->serverFds[i] = createServer(addresses[i]);
	}
	for (unsigned int i = 0; i < adminAddresses.size(); i++) {
		wo->adminServerFds[i] = createServer(adminAddresses[i]);
	}
}

static void
createPidFile() {
	TRACE_POINT();
}

static void
lowerPrivilege() {
	TRACE_POINT();
}

static void
printInfo(EV_P_ struct ev_signal *watcher, int revents) {
	WorkingObjects *wo = workingObjects;

	cerr << "### Request handler state\n";
	cerr << wo->requestHandler->inspectStateAsJson();
	cerr << "\n";
	cerr.flush();

	cerr << "### Request handler config\n";
	cerr << wo->requestHandler->getConfigAsJson();
	cerr << "\n";
	cerr.flush();

	cerr << "### Pool state\n";
	cerr << "\n" << wo->appPool->inspect();
	cerr << "\n";
	cerr.flush();

	cerr << "### mbuf stats\n\n";
	cerr << "nfree_mbuf_blockq    : " <<
		wo->serverKitContext->mbuf_pool.nfree_mbuf_blockq << "\n";
	cerr << "nactive_mbuf_blockq  : " <<
		wo->serverKitContext->mbuf_pool.nactive_mbuf_blockq << "\n";
	cerr << "mbuf_block_chunk_size: " <<
		wo->serverKitContext->mbuf_pool.mbuf_block_chunk_size << "\n";
	cerr << "\n";
	cerr.flush();

	cerr << "### Backtraces\n";
	cerr << "\n" << oxt::thread::all_backtraces();
	cerr << "\n";
	cerr.flush();
}

static void
dumpDiagnosticsOnCrash(void *userData) {
	WorkingObjects *wo = workingObjects;

	cerr << "### Request handler state\n";
	cerr << wo->requestHandler->inspectStateAsJson();
	cerr << "\n";
	cerr.flush();

	cerr << "### Request handler config\n";
	cerr << wo->requestHandler->getConfigAsJson();
	cerr << "\n";
	cerr.flush();

	cerr << "### Pool state (simple)\n";
	// Do not lock, the crash may occur within the pool.
	Pool::InspectOptions options;
	options.verbose = true;
	cerr << wo->appPool->inspect(options, false);
	cerr << "\n";
	cerr.flush();

	cerr << "### Pool state (XML)\n";
	cerr << wo->appPool->toXml(true, false);
	cerr << "\n\n";
	cerr.flush();

	cerr << "### mbuf stats\n\n";
	cerr << "nfree_mbuf_blockq  : " <<
		wo->serverKitContext->mbuf_pool.nfree_mbuf_blockq << "\n";
	cerr << "nactive_mbuf_blockq: " <<
		wo->serverKitContext->mbuf_pool.nactive_mbuf_blockq << "\n";
	cerr << "mbuf_block_chunk_size: " <<
		wo->serverKitContext->mbuf_pool.mbuf_block_chunk_size << "\n";
	cerr << "\n";
	cerr.flush();

	cerr << "### Backtraces\n";
	cerr << oxt::thread::all_backtraces();
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
initializeNonPrivilegedWorkingObjects() {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects;

	if (options.get("server_software").find(PROGRAM_NAME) == string::npos) {
		options.set("server_software", options.get("server_software") +
			(" " PROGRAM_NAME "/" PASSENGER_VERSION));
	}
	setenv("SERVER_SOFTWARE", options.get("server_software").c_str(), 1);
	options.set("data_buffer_dir", absolutizePath(options.get("data_buffer_dir")));

	vector<string> addresses = options.getStrSet("server_addresses");
	vector<string> adminAddresses = options.getStrSet("server_admin_addresses", false);

	wo->resourceLocator = ResourceLocator(options.get("passenger_root"));

	wo->randomGenerator = boost::make_shared<RandomGenerator>();
	// Check whether /dev/urandom is actually random.
	// https://code.google.com/p/phusion-passenger/issues/detail?id=516
	if (wo->randomGenerator->generateByteString(16) == wo->randomGenerator->generateByteString(16)) {
		throw RuntimeException("Your random number device, /dev/urandom, appears to be broken. "
			"It doesn't seem to be returning random data. Please fix this.");
	}

	UPDATE_TRACE_POINT();
	if (options.has("logging_agent_address")) {
		wo->unionStationCore = boost::make_shared<UnionStation::Core>(
			options.get("logging_agent_address"),
			"logging",
			options.get("logging_agent_password"));
	}

	UPDATE_TRACE_POINT();
	wo->spawnerConfig = boost::make_shared<SpawnerConfig>();
	wo->spawnerConfig->resourceLocator = &wo->resourceLocator;
	wo->spawnerConfig->agentsOptions = agentsOptions;
	wo->spawnerConfig->randomGenerator = wo->randomGenerator;
	wo->spawnerConfig->instanceDir = options.get("instance_dir", false);
	if (!wo->spawnerConfig->instanceDir.empty()) {
		wo->spawnerConfig->instanceDir = absolutizePath(wo->spawnerConfig->instanceDir);
	}
	wo->spawnerConfig->finalize();

	UPDATE_TRACE_POINT();
	wo->spawnerFactory = boost::make_shared<SpawnerFactory>(wo->spawnerConfig);

	wo->bgloop = new BackgroundEventLoop(true, true);
	ev_signal_init(&wo->sigquitWatcher, printInfo, SIGQUIT);
	ev_signal_start(wo->bgloop->loop, &wo->sigquitWatcher);
	ev_signal_init(&wo->sigintWatcher, onTerminationSignal, SIGINT);
	ev_signal_start(wo->bgloop->loop, &wo->sigintWatcher);
	ev_signal_init(&wo->sigtermWatcher, onTerminationSignal, SIGTERM);
	ev_signal_start(wo->bgloop->loop, &wo->sigtermWatcher);

	UPDATE_TRACE_POINT();
	wo->appPool = boost::make_shared<Pool>(wo->spawnerFactory, agentsOptions);
	wo->appPool->initialize();
	wo->appPool->setMax(options.getInt("max_pool_size"));
	wo->appPool->setMaxIdleTime(options.getInt("pool_idle_time") * 1000000);

	UPDATE_TRACE_POINT();
	wo->serverKitContext = new ServerKit::Context(wo->bgloop->safe);
	wo->serverKitContext->secureModePassword = wo->password;
	wo->serverKitContext->defaultFileBufferedChannelConfig.bufferDir =
		options.get("data_buffer_dir");
	wo->requestHandler = new RequestHandler(wo->serverKitContext, agentsOptions);
	wo->requestHandler->minSpareClients = 128;
	wo->requestHandler->clientFreelistLimit = 1024;
	wo->requestHandler->resourceLocator = &wo->resourceLocator;
	wo->requestHandler->appPool = wo->appPool;
	wo->requestHandler->unionStationCore = wo->unionStationCore;
	wo->requestHandler->shutdownFinishCallback = requestHandlerShutdownFinished;
	wo->requestHandler->initialize();
	wo->shutdownCounter++;

	UPDATE_TRACE_POINT();
	if (!adminAddresses.empty()) {
		wo->adminServer = new ServerAgent::AdminServer(wo->serverKitContext);
		wo->adminServer->requestHandler = wo->requestHandler;
		wo->adminServer->appPool = wo->appPool;
		wo->adminServer->exitEvent = &wo->exitEvent;
		wo->adminServer->shutdownFinishCallback = adminServerShutdownFinished;
		wo->adminServer->authorizations = wo->adminAuthorizations;
		wo->shutdownCounter++;
	}

	UPDATE_TRACE_POINT();
	for (unsigned int i = 0; i < addresses.size(); i++) {
		wo->requestHandler->listen(wo->serverFds[i]);
	}
	for (unsigned int i = 0; i < adminAddresses.size(); i++) {
		wo->adminServer->listen(wo->adminServerFds[i]);
	}
	wo->requestHandler->createSpareClients();
}

static void
reportInitializationInfo() {
	TRACE_POINT();
	if (feedbackFdAvailable()) {
		P_NOTICE("PassengerAgent server online, PID " << getpid());
		writeArrayMessage(FEEDBACK_FD,
			"initialized",
			NULL);
	} else {
		vector<string> addresses = agentsOptions->getStrSet("server_addresses");
		vector<string> adminAddresses = agentsOptions->getStrSet("server_admin_addresses", false);
		string address;

		P_NOTICE("PassengerAgent server online, PID " << getpid() <<
			", listening on " << addresses.size() << " socket(s):");
		foreach (address, addresses) {
			if (startsWith(address, "tcp://")) {
				address.erase(0, sizeof("tcp://") - 1);
				address.insert(0, "http://");
				address.append("/");
			}
			P_NOTICE(" * " << address);
		}

		if (!adminAddresses.empty()) {
			P_NOTICE("Admin server listening on " << adminAddresses.size() << " socket(s):");
			foreach (address, adminAddresses) {
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
mainLoop() {
	TRACE_POINT();
	installDiagnosticsDumper(dumpDiagnosticsOnCrash, NULL);
	workingObjects->bgloop->start("Main event loop", 0);
	waitForExitEvent();
}

static void
shutdownRequestHandler() {
	workingObjects->requestHandler->shutdown();
}

static void
shutdownAdminServer() {
	if (workingObjects->adminServer != NULL) {
		workingObjects->adminServer->shutdown();
	}
}

static void
serverShutdownFinished() {
	workingObjects->shutdownCounter--;
	if (workingObjects->shutdownCounter == 0) {
		workingObjects->allClientsDisconnectedEvent.notify();
	}
}

static void
requestHandlerShutdownFinished(RequestHandler *server) {
	serverShutdownFinished();
}

static void
adminServerShutdownFinished(ServerAgent::AdminServer *server) {
	serverShutdownFinished();
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
		installDiagnosticsDumper(NULL, NULL);
		throw SystemException("select() failed", e);
	}

	if (FD_ISSET(FEEDBACK_FD, &fds)) {
		UPDATE_TRACE_POINT();
		/* If the watchdog has been killed then we'll kill all descendant
		 * processes and exit. There's no point in keeping the server agent
		 * running because we can't detect when the web server exits,
		 * and because this server agent doesn't own the instance
		 * directory. As soon as passenger-status is run, the instance
		 * directory will be cleaned up, making the server inaccessible.
		 */
		P_WARN("Watchdog seems to be killed; forcing shutdown of all subprocesses");
		// We send a SIGTERM first to allow processes to gracefully shut down.
		syscalls::killpg(getpgrp(), SIGTERM);
		usleep(500000);
		syscalls::killpg(getpgrp(), SIGKILL);
		_exit(2); // In case killpg() fails.
	} else {
		UPDATE_TRACE_POINT();
		/* We received an exit command. */
		P_NOTICE("Received command to shutdown gracefully. "
			"Waiting until all clients have disconnected...");
		wo->appPool->prepareForShutdown();
		wo->bgloop->safe->runLater(shutdownRequestHandler);
		wo->bgloop->safe->runLater(shutdownAdminServer);

		UPDATE_TRACE_POINT();
		FD_ZERO(&fds);
		FD_SET(wo->allClientsDisconnectedEvent.fd(), &fds);
		if (syscalls::select(wo->allClientsDisconnectedEvent.fd() + 1,
			&fds, NULL, NULL, NULL) == -1)
		{
			int e = errno;
			installDiagnosticsDumper(NULL, NULL);
			throw SystemException("select() failed", e);
		}

		P_INFO("All clients have now disconnected. Proceeding with graceful shutdown");
	}
}

static void
cleanup() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;

	P_DEBUG("Shutting down PassengerAgent server...");
	wo->appPool->destroy();
	installDiagnosticsDumper(NULL, NULL);
	wo->bgloop->stop();
	wo->appPool.reset();
	delete wo->requestHandler;
	P_NOTICE("PassengerAgent server shutdown finished");
}

static int
runServer() {
	TRACE_POINT();
	P_NOTICE("Starting PassengerAgent server...");

	try {
		UPDATE_TRACE_POINT();
		initializePrivilegedWorkingObjects();
		initializeSingleAppMode();
		startListening();
		createPidFile();
		lowerPrivilege();
		initializeNonPrivilegedWorkingObjects();

		UPDATE_TRACE_POINT();
		reportInitializationInfo();
		mainLoop();

		UPDATE_TRACE_POINT();
		cleanup();
	} catch (const tracable_exception &e) {
		P_CRITICAL("ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}

	return 0;
}


/***** Entry point and command line argument parsing *****/

static void
parseOptions(int argc, const char *argv[], VariantMap &options) {
	OptionParser p(serverUsage);
	int i = 2;

	while (i < argc) {
		if (parseServerOption(argc, argv, i, options)) {
			continue;
		} else if (p.isFlag(argv[i], 'h', "--help")) {
			serverUsage();
			exit(0);
		} else {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s server --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		}
	}

	// Set log_level here so that initializeAgent() calls setLogLevel()
	// with the right value.
	if (options.has("server_log_level")) {
		options.setInt("log_level", options.getInt("server_log_level"));
	}
}

static void
setAgentsOptionsDefaults() {
	VariantMap &options = *agentsOptions;
	set<string> defaultAddress;
	defaultAddress.insert(DEFAULT_HTTP_SERVER_LISTEN_ADDRESS);

	options.setDefaultStrSet("server_addresses", defaultAddress);
	options.setDefaultBool("multi_app", false);
	options.setDefault("environment", DEFAULT_APP_ENV);
	options.setDefaultInt("max_pool_size", DEFAULT_MAX_POOL_SIZE);
	options.setDefaultInt("pool_idle_time", DEFAULT_POOL_IDLE_TIME);
	options.setDefaultInt("min_instances", 1);
	options.setDefault("server_software", PROGRAM_NAME "/" PASSENGER_VERSION);
	options.setDefaultBool("show_version_in_header", true);
	options.setDefault("data_buffer_dir", getSystemTempDir());

	string firstAddress = options.getStrSet("server_addresses")[0];
	if (getSocketAddressType(firstAddress) == SAT_TCP) {
		string host;
		unsigned short port;

		parseTcpSocketAddress(firstAddress, host, port);
		options.setDefault("default_server_name", host);
		options.setDefaultInt("default_server_port", port);
	} else {
		options.setDefault("default_server_name", "localhost");
		options.setDefaultInt("default_server_port", 80);
	}

	options.setDefault("default_ruby", DEFAULT_RUBY);
	if (!options.getBool("multi_app") && !options.has("app_root")) {
		char *pwd = getcwd(NULL, 0);
		options.set("app_root", pwd);
		free(pwd);
	}
}

static void
sanityCheckOptions() {
	VariantMap &options = *agentsOptions;
	bool ok = true;

	if (!options.has("passenger_root")) {
		fprintf(stderr, "ERROR: please set the --passenger-root argument.\n");
		ok = false;
	}
	if (options.getBool("multi_app") && options.has("app_root")) {
		fprintf(stderr, "ERROR: you may not specify an application directory "
			"when in multi-app mode.\n");
		ok = false;
	}
	if (!options.getBool("multi_app") && options.has("app_type")) {
		PassengerAppType appType = getAppType(options.get("app_type"));
		if (appType == PAT_NONE || appType == PAT_ERROR) {
			fprintf(stderr, "ERROR: '%s' is not a valid applicaion type. Supported app types are:",
				options.get("app_type").c_str());
			const AppTypeDefinition *definition = &appTypeDefinitions[0];
			while (definition->type != PAT_NONE) {
				fprintf(stderr, " %s", definition->name);
				definition++;
			}
			fprintf(stderr, "\n");
			ok = false;
		}

		if (!options.has("startup_file")) {
			fprintf(stderr, "ERROR: if you've passed --app-type, then you must also pass --startup-file.\n");
			ok = false;
		}
	}

	if (!ok) {
		exit(1);
	}
}

int
serverMain(int argc, char *argv[]) {
	agentsOptions = new VariantMap();
	*agentsOptions = initializeAgent(argc, &argv, "PassengerAgent server", parseOptions, 2);
	setAgentsOptionsDefaults();
	sanityCheckOptions();
	return runServer();
}
