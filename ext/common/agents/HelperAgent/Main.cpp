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
#include <agents/Base.h>
#include <Constants.h>
#include <ServerKit/Server.h>
#include <ApplicationPool2/Pool.h>
#include <MessageServer.h>
#include <MessageReadersWriters.h>
#include <FileDescriptor.h>
#include <ResourceLocator.h>
#include <BackgroundEventLoop.cpp>
#include <ServerInstanceDir.h>
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
			writeArrayMessage(commonContext.fd, "exit command received", NULL);
			return true;
		} else {
			return false;
		}
	}
};
#endif


/***** Structures, constants, global variables and forward declarations *****/

struct WorkingObjects {
	int serverFds[SERVER_KIT_MAX_SERVER_ENDPOINTS];
	ResourceLocator resourceLocator;
	RandomGeneratorPtr randomGenerator;
	UnionStation::CorePtr unionStationCore;
	SpawnerConfigPtr spawnerConfig;
	SpawnerFactoryPtr spawnerFactory;
	BackgroundEventLoop *bgloop;
	struct ev_signal sigquitWatcher;
	PoolPtr appPool;
	ServerKit::Context *serverKitContext;
	RequestHandler *requestHandler;
};

static WorkingObjects *workingObjects;


/***** Server stuff *****/

static void
initializePrivilegedWorkingObjects() {
	TRACE_POINT();
	workingObjects = new WorkingObjects();
}

static void
initializeSingleAppMode(WorkingObjects *wo) {
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
startListening(WorkingObjects *wo) {
	TRACE_POINT();
	vector<string> addresses = agentsOptions->getStrSet("server_listen_addresses");

	for (unsigned int i = 0; i < addresses.size(); i++) {
		wo->serverFds[i] = createServer(addresses[i]);
	}
}

static void
createPidFile(WorkingObjects *wo) {
	TRACE_POINT();
}

static void
lowerPrivilege(WorkingObjects *wo) {
	TRACE_POINT();
}

static void
onSigquit(EV_P_ struct ev_signal *watcher, int revents) {
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
initializeNonPrivilegedWorkingObjects(WorkingObjects *wo) {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;
	vector<string> addresses = options.getStrSet("server_listen_addresses");

	wo->resourceLocator = ResourceLocator(options.get("passenger_root"));

	wo->randomGenerator = boost::make_shared<RandomGenerator>();
	// Check whether /dev/urandom is actually random.
	// https://code.google.com/p/phusion-passenger/issues/detail?id=516
	if (wo->randomGenerator->generateByteString(16) == wo->randomGenerator->generateByteString(16)) {
		throw RuntimeException("Your random number device, /dev/urandom, appears to be broken. "
			"It doesn't seem to be returning random data. Please fix this.");
	}

	UPDATE_TRACE_POINT();
	//wo->unionStationCore = boost::make_shared<UnionStation::Core>(
	//	"TODO: logging agent address", "logging", "TODO: logging agent password");
	wo->spawnerConfig = boost::make_shared<SpawnerConfig>();
	wo->spawnerConfig->resourceLocator = &wo->resourceLocator;
	wo->spawnerConfig->agentsOptions = agentsOptions;
	wo->spawnerConfig->randomGenerator = wo->randomGenerator;
	wo->spawnerConfig->finalize();

	UPDATE_TRACE_POINT();
	wo->spawnerFactory = boost::make_shared<SpawnerFactory>(wo->spawnerConfig);

	wo->bgloop = new BackgroundEventLoop(true, true);
	ev_signal_init(&wo->sigquitWatcher, onSigquit, SIGQUIT);
	ev_signal_start(wo->bgloop->loop, &wo->sigquitWatcher);

	wo->appPool = boost::make_shared<Pool>(wo->spawnerFactory, agentsOptions);
	wo->appPool->initialize();
	wo->appPool->setMax(options.getInt("max_pool_size"));
	wo->appPool->setMaxIdleTime(options.getInt("pool_idle_time") * 1000000);

	UPDATE_TRACE_POINT();
	wo->serverKitContext = new ServerKit::Context(wo->bgloop->safe);
	wo->requestHandler = new RequestHandler(wo->serverKitContext, agentsOptions);
	wo->requestHandler->resourceLocator = &wo->resourceLocator;
	wo->requestHandler->appPool = wo->appPool;
	wo->requestHandler->initialize();

	UPDATE_TRACE_POINT();
	for (unsigned int i = 0; i < addresses.size(); i++) {
		wo->requestHandler->listen(wo->serverFds[i]);
	}
	wo->requestHandler->createSpareClients();
}

static void
reportInitializationInfo(WorkingObjects *wo) {
	vector<string> addresses = agentsOptions->getStrSet("server_listen_addresses");
	string address;

	P_NOTICE("PassengerAgent server online, PID " << getpid () <<
		", listening on " << addresses.size() << " socket(s):");
	foreach (address, addresses) {
		if (startsWith(address, "tcp://")) {
			address.erase(0, sizeof("tcp://") - 1);
			address.insert(0, "http://");
			address.append("/");
		}
		P_NOTICE(" * " << address);
	}

	if (feedbackFdAvailable()) {
		writeArrayMessage(FEEDBACK_FD,
			"initialized",
			NULL);
	}
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
mainLoop(WorkingObjects *wo) {
	TRACE_POINT();
	installDiagnosticsDumper(dumpDiagnosticsOnCrash, NULL);
	wo->bgloop->start("Main event loop", 0);
	while (true) {
		sleep(100);
	}
	installDiagnosticsDumper(NULL, NULL);
}

static void
cleanup(WorkingObjects *wo) {
	TRACE_POINT();
}

static int
runServer() {
	TRACE_POINT();
	P_DEBUG("Starting PassengerAgent server...");

	try {
		UPDATE_TRACE_POINT();
		initializePrivilegedWorkingObjects();
		initializeSingleAppMode(workingObjects);
		startListening(workingObjects);
		createPidFile(workingObjects);
		lowerPrivilege(workingObjects);
		initializeNonPrivilegedWorkingObjects(workingObjects);

		UPDATE_TRACE_POINT();
		reportInitializationInfo(workingObjects);
		mainLoop(workingObjects);

		UPDATE_TRACE_POINT();
		cleanup(workingObjects);
	} catch (const tracable_exception &e) {
		P_CRITICAL("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}

	P_TRACE(2, "PassengerAgent server exiting with code 0.");
	return 0;
}


/***** Entry point and command line argument parsing *****/

static void
parseOptions(int argc, const char *argv[], VariantMap &options) {
	int i = 2;

	while (i < argc) {
		if (!parseServerOption(argc, argv, i, options)) {
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
	set<string> defaultListenAddress;
	defaultListenAddress.insert(DEFAULT_HTTP_SERVER_LISTEN_ADDRESS);

	options.setDefaultStrSet("server_listen_addresses", defaultListenAddress);
	options.setDefaultBool("multi_app", false);
	options.setDefault("environment", DEFAULT_APP_ENV);
	options.setDefaultInt("max_pool_size", DEFAULT_MAX_POOL_SIZE);
	options.setDefaultInt("pool_idle_time", DEFAULT_POOL_IDLE_TIME);
	options.setDefaultInt("min_instances", 1);

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
	*agentsOptions = initializeAgent(argc, &argv, "PassengerHelperAgent", parseOptions, 2);
	setAgentsOptionsDefaults();
	sanityCheckOptions();
	return runServer();
}
