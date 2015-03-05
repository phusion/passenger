/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion
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

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif
#ifdef __linux__
	#define SUPPORTS_PER_THREAD_CPU_AFFINITY
	#include <sched.h>
	#include <pthread.h>
#endif

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
#include <fcntl.h>
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
#include <boost/atomic.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>

#include <ev++.h>

#include <agents/HelperAgent/OptionParser.h>
#include <agents/HelperAgent/RequestHandler.h>
#include <agents/HelperAgent/AdminServer.h>
#include <agents/Base.h>
#include <Constants.h>
#include <ServerKit/Server.h>
#include <ServerKit/AcceptLoadBalancer.h>
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


/***** Structures, constants, global variables and forward declarations *****/

namespace Passenger {
namespace ServerAgent {
	struct ThreadWorkingObjects {
		BackgroundEventLoop *bgloop;
		ServerKit::Context *serverKitContext;
		RequestHandler *requestHandler;

		ThreadWorkingObjects()
			: bgloop(NULL),
			  serverKitContext(NULL),
			  requestHandler(NULL)
			{ }
	};

	struct AdminWorkingObjects {
		BackgroundEventLoop *bgloop;
		ServerKit::Context *serverKitContext;
		AdminServer *adminServer;

		AdminWorkingObjects()
			: bgloop(NULL),
			  serverKitContext(NULL),
			  adminServer(NULL)
			{ }
	};

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
		PoolPtr appPool;

		ServerKit::AcceptLoadBalancer<RequestHandler> loadBalancer;
		vector<ThreadWorkingObjects> threadWorkingObjects;
		struct ev_signal sigintWatcher;
		struct ev_signal sigtermWatcher;
		struct ev_signal sigquitWatcher;

		AdminWorkingObjects adminWorkingObjects;

		EventFd exitEvent;
		EventFd allClientsDisconnectedEvent;
		unsigned int terminationCount;
		boost::atomic<unsigned int> shutdownCounter;
		oxt::thread *prestarterThread;

		WorkingObjects()
			: terminationCount(0),
			  shutdownCounter(0)
		{
			for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
				serverFds[i] = -1;
				adminServerFds[i] = -1;
			}
		}
	};
} // namespace ServerAgent
} // namespace Passenger

using namespace Passenger::ServerAgent;

static VariantMap *agentsOptions;
static WorkingObjects *workingObjects;


/***** Server stuff *****/

static void waitForExitEvent();
static void cleanup();
static void deletePidFile();
static void abortLongRunningConnections(const ApplicationPool2::ProcessPtr &process);
static void requestHandlerShutdownFinished(RequestHandler *server);
static void adminServerShutdownFinished(ServerAgent::AdminServer *server);
static void printInfoInThread();

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

	wo->prestarterThread = NULL;

	wo->password = options.get("server_password", false);
	if (wo->password == "-") {
		wo->password.clear();
	} else if (wo->password.empty() && options.has("server_password_file")) {
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
		P_NOTICE(AGENT_EXE " server running in multi-application mode.");
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
				"Type '" AGENT_EXE " server --help' for more information.\n",
				options.get("app_root").c_str());
			exit(1);
		}

		options.set("app_type", getAppTypeName(appType));
		options.set("startup_file", getAppTypeStartupFile(appType));
	}

	P_NOTICE(AGENT_EXE " server running in single-application mode.");
	P_NOTICE("Serving app     : " << options.get("app_root"));
	P_NOTICE("App type        : " << options.get("app_type"));
	P_NOTICE("App startup file: " << options.get("startup_file"));
}

static void
makeFileWorldReadableAndWritable(const string &path) {
	int ret;

	do {
		ret = chmod(path.c_str(), parseModeString("u=rw,g=rw,o=rw"));
	} while (ret == -1 && errno == EINTR);
}

static void
startListening() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	vector<string> addresses = agentsOptions->getStrSet("server_addresses");
	vector<string> adminAddresses = agentsOptions->getStrSet("server_admin_addresses", false);

	for (unsigned int i = 0; i < addresses.size(); i++) {
		wo->serverFds[i] = createServer(addresses[i]);
		if (getSocketAddressType(addresses[i]) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(addresses[i]));
		}
	}
	for (unsigned int i = 0; i < adminAddresses.size(); i++) {
		wo->adminServerFds[i] = createServer(adminAddresses[i]);
		if (getSocketAddressType(adminAddresses[i]) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(adminAddresses[i]));
		}
	}
}

static void
createPidFile() {
	TRACE_POINT();
	string pidFile = agentsOptions->get("server_pid_file", false);
	if (!pidFile.empty()) {
		char pidStr[32];

		snprintf(pidStr, sizeof(pidStr), "%lld", (long long) getpid());

		int fd = syscalls::open(pidFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd == -1) {
			int e = errno;
			throw FileSystemException("Cannot create PID file " + pidFile, e, pidFile);
		}

		UPDATE_TRACE_POINT();
		writeExact(fd, pidStr, strlen(pidStr));
		syscalls::close(fd);
	}
}

static void
lowerPrivilege() {
	TRACE_POINT();
}

static void
printInfo(EV_P_ struct ev_signal *watcher, int revents) {
	oxt::thread(printInfoInThread, "Information printer");
}

static void
inspectRequestHandlerStateAsJson(RequestHandler *rh, string *result) {
	*result = rh->inspectStateAsJson().toStyledString();
}

static void
inspectRequestHandlerConfigAsJson(RequestHandler *rh, string *result) {
	*result = rh->getConfigAsJson().toStyledString();
}

static void
getMbufStats(struct MemoryKit::mbuf_pool *input, struct MemoryKit::mbuf_pool *result) {
	*result = *input;
}

static void
printInfoInThread() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	unsigned int i;

	cerr << "### Backtraces\n";
	cerr << "\n" << oxt::thread::all_backtraces();
	cerr << "\n";
	cerr.flush();

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		string json;

		cerr << "### Request handler state (thread " << (i + 1) << ")\n";
		two->bgloop->safe->runSync(boost::bind(inspectRequestHandlerStateAsJson,
			two->requestHandler, &json));
		cerr << json;
		cerr << "\n";
		cerr.flush();
	}

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		string json;

		cerr << "### Request handler config (thread " << (i + 1) << ")\n";
		two->bgloop->safe->runSync(boost::bind(inspectRequestHandlerConfigAsJson,
			two->requestHandler, &json));
		cerr << json;
		cerr << "\n";
		cerr.flush();
	}

	struct MemoryKit::mbuf_pool stats;
	cerr << "### mbuf stats\n\n";
	wo->threadWorkingObjects[0].bgloop->safe->runSync(boost::bind(getMbufStats,
		&wo->threadWorkingObjects[0].serverKitContext->mbuf_pool,
		&stats));
	cerr << "nfree_mbuf_blockq    : " << stats.nfree_mbuf_blockq << "\n";
	cerr << "nactive_mbuf_blockq  : " << stats.nactive_mbuf_blockq << "\n";
	cerr << "mbuf_block_chunk_size: " << stats.mbuf_block_chunk_size << "\n";
	cerr << "\n";
	cerr.flush();

	cerr << "### Pool state\n";
	cerr << "\n" << wo->appPool->inspect();
	cerr << "\n";
	cerr.flush();
}

static void
dumpDiagnosticsOnCrash(void *userData) {
	WorkingObjects *wo = workingObjects;
	unsigned int i;

	cerr << "### Backtraces\n";
	cerr << oxt::thread::all_backtraces();
	cerr.flush();

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		cerr << "### Request handler state (thread " << (i + 1) << ")\n";
		cerr << two->requestHandler->inspectStateAsJson();
		cerr << "\n";
		cerr.flush();
	}

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		cerr << "### Request handler config (thread " << (i + 1) << ")\n";
		cerr << two->requestHandler->getConfigAsJson();
		cerr << "\n";
		cerr.flush();
	}

	cerr << "### Pool state (simple)\n";
	// Do not lock, the crash may occur within the pool.
	Pool::InspectOptions options;
	options.verbose = true;
	cerr << wo->appPool->inspect(options, false);
	cerr << "\n";
	cerr.flush();

	cerr << "### mbuf stats\n\n";
	cerr << "nfree_mbuf_blockq  : " <<
		wo->threadWorkingObjects[0].serverKitContext->mbuf_pool.nfree_mbuf_blockq << "\n";
	cerr << "nactive_mbuf_blockq: " <<
		wo->threadWorkingObjects[0].serverKitContext->mbuf_pool.nactive_mbuf_blockq << "\n";
	cerr << "mbuf_block_chunk_size: " <<
		wo->threadWorkingObjects[0].serverKitContext->mbuf_pool.mbuf_block_chunk_size << "\n";
	cerr << "\n";
	cerr.flush();

	cerr << "### Pool state (XML)\n";
	cerr << wo->appPool->toXml(true, false);
	cerr << "\n\n";
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

	if (options.get("server_software").find(SERVER_TOKEN_NAME) == string::npos
	 && options.get("server_software").find(FLYING_PASSENGER_NAME) == string::npos)
	{
		options.set("server_software", options.get("server_software") +
			(" " SERVER_TOKEN_NAME "/" PASSENGER_VERSION));
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
	wo->appPool = boost::make_shared<Pool>(wo->spawnerFactory, agentsOptions);
	wo->appPool->initialize();
	wo->appPool->setMax(options.getInt("max_pool_size"));
	wo->appPool->setMaxIdleTime(options.getInt("pool_idle_time") * 1000000ULL);
	wo->appPool->enableSelfChecking(options.getBool("selfchecks"));
	wo->appPool->abortLongRunningConnectionsCallback = abortLongRunningConnections;

	UPDATE_TRACE_POINT();
	unsigned int nthreads = options.getInt("server_threads");
	BackgroundEventLoop *firstLoop = NULL; // Avoid compiler warning
	wo->threadWorkingObjects.reserve(nthreads);
	for (unsigned int i = 0; i < nthreads; i++) {
		UPDATE_TRACE_POINT();
		ThreadWorkingObjects two;

		if (i == 0) {
			two.bgloop = firstLoop = new BackgroundEventLoop(true, true);
		} else {
			two.bgloop = new BackgroundEventLoop(true, false);
		}

		UPDATE_TRACE_POINT();
		two.serverKitContext = new ServerKit::Context(two.bgloop->safe);
		two.serverKitContext->secureModePassword = wo->password;
		two.serverKitContext->defaultFileBufferedChannelConfig.bufferDir =
			options.get("data_buffer_dir");
		two.serverKitContext->defaultFileBufferedChannelConfig.threshold =
			options.getUint("file_buffer_threshold");

		UPDATE_TRACE_POINT();
		two.requestHandler = new RequestHandler(two.serverKitContext, agentsOptions, i + 1);
		two.requestHandler->minSpareClients = 128;
		two.requestHandler->clientFreelistLimit = 1024;
		two.requestHandler->resourceLocator = &wo->resourceLocator;
		two.requestHandler->appPool = wo->appPool;
		two.requestHandler->unionStationCore = wo->unionStationCore;
		two.requestHandler->shutdownFinishCallback = requestHandlerShutdownFinished;
		two.requestHandler->initialize();
		wo->shutdownCounter.fetch_add(1, boost::memory_order_relaxed);

		wo->threadWorkingObjects.push_back(two);
	}

	UPDATE_TRACE_POINT();
	ev_signal_init(&wo->sigquitWatcher, printInfo, SIGQUIT);
	ev_signal_start(firstLoop->loop, &wo->sigquitWatcher);
	ev_signal_init(&wo->sigintWatcher, onTerminationSignal, SIGINT);
	ev_signal_start(firstLoop->loop, &wo->sigintWatcher);
	ev_signal_init(&wo->sigtermWatcher, onTerminationSignal, SIGTERM);
	ev_signal_start(firstLoop->loop, &wo->sigtermWatcher);

	UPDATE_TRACE_POINT();
	if (!adminAddresses.empty()) {
		UPDATE_TRACE_POINT();
		AdminWorkingObjects *awo = &wo->adminWorkingObjects;

		awo->bgloop = new BackgroundEventLoop(true, false);
		awo->serverKitContext = new ServerKit::Context(awo->bgloop->safe);
		awo->serverKitContext->secureModePassword = wo->password;
		// Configure a large threshold so that it uses libeio as little as possible.
		// libeio runs on the RequestHandler's first thread, and if there's a
		// problem there we don't want it to affect the admin server.
		awo->serverKitContext->defaultFileBufferedChannelConfig.threshold = 1024 * 1024;
		awo->serverKitContext->defaultFileBufferedChannelConfig.bufferDir =
			options.get("data_buffer_dir");

		UPDATE_TRACE_POINT();
		awo->adminServer = new ServerAgent::AdminServer(awo->serverKitContext);
		awo->adminServer->requestHandlers.reserve(wo->threadWorkingObjects.size());
		for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
			awo->adminServer->requestHandlers.push_back(
				wo->threadWorkingObjects[i].requestHandler);
		}
		awo->adminServer->appPool = wo->appPool;
		awo->adminServer->exitEvent = &wo->exitEvent;
		awo->adminServer->shutdownFinishCallback = adminServerShutdownFinished;
		awo->adminServer->authorizations = wo->adminAuthorizations;

		wo->shutdownCounter.fetch_add(1, boost::memory_order_relaxed);
	}

	UPDATE_TRACE_POINT();
	/* We do not delete Unix domain socket files at shutdown because
	 * that can cause a race condition if the user tries to start another
	 * server with the same addresses at the same time. The new server
	 * would then delete the socket and replace it with its own,
	 * while the old server would delete the file yet again shortly after.
	 * This is especially noticeable on systems that heavily swap.
	 */
	for (unsigned int i = 0; i < addresses.size(); i++) {
		if (nthreads == 1) {
			ThreadWorkingObjects *two = &wo->threadWorkingObjects[0];
			two->requestHandler->listen(wo->serverFds[i]);
		} else {
			wo->loadBalancer.listen(wo->serverFds[i]);
		}
	}
	for (unsigned int i = 0; i < nthreads; i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		two->requestHandler->createSpareClients();
	}
	if (nthreads > 1) {
		wo->loadBalancer.servers.reserve(nthreads);
		for (unsigned int i = 0; i < nthreads; i++) {
			ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
			wo->loadBalancer.servers.push_back(two->requestHandler);
		}
	}
	for (unsigned int i = 0; i < adminAddresses.size(); i++) {
		wo->adminWorkingObjects.adminServer->listen(wo->adminServerFds[i]);
	}
}

static void
prestartWebApps() {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects;

	boost::function<void ()> func = boost::bind(prestartWebApps,
		wo->resourceLocator,
		options.get("default_ruby"),
		options.getStrSet("prestart_urls", false)
	);
	wo->prestarterThread = new oxt::thread(
		boost::bind(runAndPrintExceptions, func, true)
	);
}

static void
reportInitializationInfo() {
	TRACE_POINT();
	if (feedbackFdAvailable()) {
		P_NOTICE(AGENT_EXE " server online, PID " << getpid());
		writeArrayMessage(FEEDBACK_FD,
			"initialized",
			NULL);
	} else {
		vector<string> addresses = agentsOptions->getStrSet("server_addresses");
		vector<string> adminAddresses = agentsOptions->getStrSet("server_admin_addresses", false);
		string address;

		P_NOTICE(AGENT_EXE " server online, PID " << getpid() <<
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
	WorkingObjects *wo = workingObjects;
	#ifdef SUPPORTS_PER_THREAD_CPU_AFFINITY
		unsigned int maxCpus = boost::thread::hardware_concurrency();
		bool cpuAffine = agentsOptions->getBool("server_cpu_affine")
			&& maxCpus <= CPU_SETSIZE;
	#endif

	installDiagnosticsDumper(dumpDiagnosticsOnCrash, NULL);
	for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		two->bgloop->start("Main event loop: thread " + toString(i + 1), 0);
		#ifdef SUPPORTS_PER_THREAD_CPU_AFFINITY
			if (cpuAffine) {
				cpu_set_t cpus;
				int result;

				CPU_ZERO(&cpus);
				CPU_SET(i % maxCpus, &cpus);
				P_DEBUG("Setting CPU affinity of server thread " << (i + 1)
					<< " to CPU " << (i % maxCpus + 1));
				result = pthread_setaffinity_np(two->bgloop->getNativeHandle(),
					maxCpus, &cpus);
				if (result != 0) {
					P_WARN("Cannot set CPU affinity on server thread " << (i + 1)
						<< ": " << strerror(result) << " (errno=" << result << ")");
				}
			}
		#endif
	}
	if (wo->adminWorkingObjects.adminServer != NULL) {
		wo->adminWorkingObjects.bgloop->start("Admin event loop", 0);
	}
	if (wo->threadWorkingObjects.size() > 1) {
		wo->loadBalancer.start();
	}
	waitForExitEvent();
}

static void
abortLongRunningConnectionsOnRequestHandler(RequestHandler *requestHandler,
	string gupid)
{
	requestHandler->disconnectLongRunningConnections(gupid);
}

static void
abortLongRunningConnections(const ApplicationPool2::ProcessPtr &process) {
	// We are inside the ApplicationPool lock. Be very careful here.
	WorkingObjects *wo = workingObjects;
	P_NOTICE("Disconnecting long-running connections for process " <<
		process->pid << ", application " << process->getGroup()->name);
	for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
		wo->threadWorkingObjects[i].bgloop->safe->runLater(
			boost::bind(abortLongRunningConnectionsOnRequestHandler,
				wo->threadWorkingObjects[i].requestHandler,
				string(process->gupid, process->gupidSize)));
	}
}

static void
shutdownRequestHandler(ThreadWorkingObjects *two) {
	two->requestHandler->shutdown();
}

static void
shutdownAdminServer() {
	workingObjects->adminWorkingObjects.adminServer->shutdown();
}

static void
serverShutdownFinished() {
	unsigned int i = workingObjects->shutdownCounter.fetch_sub(1, boost::memory_order_release);
	if (i == 1) {
		boost::atomic_thread_fence(boost::memory_order_acquire);
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

		for (unsigned i = 0; i < wo->threadWorkingObjects.size(); i++) {
			ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
			two->bgloop->safe->runLater(boost::bind(shutdownRequestHandler, two));
		}
		if (wo->threadWorkingObjects.size() > 1) {
			wo->loadBalancer.shutdown();
		}
		if (wo->adminWorkingObjects.adminServer != NULL) {
			wo->adminWorkingObjects.bgloop->safe->runLater(shutdownAdminServer);
		}

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

	P_DEBUG("Shutting down " AGENT_EXE " server...");
	wo->appPool->destroy();
	installDiagnosticsDumper(NULL, NULL);
	for (unsigned i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		two->bgloop->stop();
	}
	if (wo->adminWorkingObjects.adminServer != NULL) {
		wo->adminWorkingObjects.bgloop->stop();
	}
	wo->appPool.reset();
	for (unsigned i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		delete two->requestHandler;
	}
	if (wo->prestarterThread != NULL) {
		wo->prestarterThread->interrupt_and_join();
		delete wo->prestarterThread;
	}
	for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
		if (wo->serverFds[i] != -1) {
			close(wo->serverFds[i]);
		}
		if (wo->adminServerFds[i] != -1) {
			close(wo->adminServerFds[i]);
		}
	}
	deletePidFile();
	P_NOTICE(AGENT_EXE " server shutdown finished");
}

static void
deletePidFile() {
	TRACE_POINT();
	string pidFile = agentsOptions->get("server_pid_file", false);
	if (!pidFile.empty()) {
		syscalls::unlink(pidFile.c_str());
	}
}

static int
runServer() {
	TRACE_POINT();
	P_NOTICE("Starting " AGENT_EXE " server...");

	try {
		UPDATE_TRACE_POINT();
		initializePrivilegedWorkingObjects();
		initializeSingleAppMode();
		startListening();
		createPidFile();
		lowerPrivilege();
		initializeNonPrivilegedWorkingObjects();
		prestartWebApps();

		UPDATE_TRACE_POINT();
		reportInitializationInfo();
		mainLoop();

		UPDATE_TRACE_POINT();
		cleanup();
	} catch (const tracable_exception &e) {
		P_CRITICAL("ERROR: " << e.what() << "\n" << e.backtrace());
		deletePidFile();
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
}

static void
preinitialize(VariantMap &options) {
	// Set log_level here so that initializeAgent() calls setLogLevel()
	// and setLogFile() with the right value.
	if (options.has("server_log_level")) {
		options.setInt("log_level", options.getInt("server_log_level"));
	}
	if (options.has("server_log_file")) {
		options.set("debug_log_file", options.get("server_log_file"));
	}
}

static string
inferDefaultGroup(const string &defaultUser) {
	struct passwd *userEntry = getpwnam(defaultUser.c_str());
	if (userEntry == NULL) {
		throw ConfigurationException(
			string("The user that PassengerDefaultUser refers to, '") +
			defaultUser + "', does not exist.");
	}
	return getGroupName(userEntry->pw_gid);
}

static void
setAgentsOptionsDefaults() {
	VariantMap &options = *agentsOptions;
	set<string> defaultAddress;
	defaultAddress.insert(DEFAULT_HTTP_SERVER_LISTEN_ADDRESS);

	options.setDefaultBool("user_switching", true);
	options.setDefault("default_user", DEFAULT_WEB_APP_USER);
	if (!options.has("default_group")) {
		options.set("default_group",
			inferDefaultGroup(options.get("default_user")));
	}
	options.setDefaultStrSet("server_addresses", defaultAddress);
	options.setDefaultBool("multi_app", false);
	options.setDefault("environment", DEFAULT_APP_ENV);
	options.setDefault("spawn_method", DEFAULT_SPAWN_METHOD);
	options.setDefaultBool("load_shell_envvars", false);
	options.setDefault("concurrency_model", DEFAULT_CONCURRENCY_MODEL);
	options.setDefaultInt("app_thread_count", DEFAULT_APP_THREAD_COUNT);
	options.setDefaultInt("max_pool_size", DEFAULT_MAX_POOL_SIZE);
	options.setDefaultInt("pool_idle_time", DEFAULT_POOL_IDLE_TIME);
	options.setDefaultInt("min_instances", 1);
	options.setDefaultInt("stat_throttle_rate", DEFAULT_STAT_THROTTLE_RATE);
	options.setDefault("server_software", SERVER_TOKEN_NAME "/" PASSENGER_VERSION);
	options.setDefaultBool("show_version_in_header", true);
	options.setDefaultBool("sticky_sessions", false);
	options.setDefault("sticky_sessions_cookie_name", DEFAULT_STICKY_SESSIONS_COOKIE_NAME);
	options.setDefaultBool("turbocaching", true);
	options.setDefault("data_buffer_dir", getSystemTempDir());
	options.setDefaultUint("file_buffer_threshold", DEFAULT_FILE_BUFFERED_CHANNEL_THRESHOLD);
	options.setDefaultInt("response_buffer_high_watermark", DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK);
	options.setDefaultBool("selfchecks", false);
	options.setDefaultBool("server_graceful_exit", true);
	options.setDefaultInt("server_threads", boost::thread::hardware_concurrency());
	options.setDefaultBool("server_cpu_affine", false);
	options.setDefault("friendly_error_pages", "auto");
	options.setDefaultBool("rolling_restarts", false);
	options.setDefaultBool("resist_deployment_errors", false);

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
	options.setDefaultBool("debugger", false);
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
			fprintf(stderr, "ERROR: '%s' is not a valid application type. Supported app types are:",
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
	if (options.get("concurrency_model") != "process" && options.get("concurrency_model") != "thread") {
		fprintf(stderr, "ERROR: '%s' is not a valid concurrency model. Supported concurrency "
			"models are: process, thread.\n",
			options.get("concurrency_model").c_str());
		ok = false;
	} else if (options.get("concurrency_model") != "process") {
		#ifndef PASSENGER_IS_ENTERPRISE
			fprintf(stderr, "ERROR: the '%s' concurrency model is only supported in "
				PROGRAM_NAME " Enterprise.\nYou are currently using the open source "
				PROGRAM_NAME ". Buy " PROGRAM_NAME " Enterprise here: https://www.phusionpassenger.com/enterprise\n",
				options.get("concurrency_model").c_str());
			ok = false;
		#endif
	}
	if (options.getInt("app_thread_count") < 1) {
		fprintf(stderr, "ERROR: the value passed to --app-thread-count must be at least 1.\n");
		ok = false;
	} else if (options.getInt("app_thread_count") > 1) {
		#ifndef PASSENGER_IS_ENTERPRISE
			fprintf(stderr, "ERROR: the --app-thread-count option is only supported in "
				PROGRAM_NAME " Enterprise.\nYou are currently using the open source "
				PROGRAM_NAME ". Buy " PROGRAM_NAME " Enterprise here: https://www.phusionpassenger.com/enterprise\n");
			ok = false;
		#endif
	}
	if (options.has("max_request_time")) {
		if (options.getInt("max_request_time", false, 0) < 1) {
			fprintf(stderr, "ERROR: the value passed to --max-request-time must be at least 1.\n");
			ok = false;
		}
		#ifndef PASSENGER_IS_ENTERPRISE
			fprintf(stderr, "ERROR: the --max-request-time option is only supported in "
				PROGRAM_NAME " Enterprise.\nYou are currently using the open source "
				PROGRAM_NAME ". Buy " PROGRAM_NAME " Enterprise here: https://www.phusionpassenger.com/enterprise\n");
			ok = false;
		#endif
	}
	if (RequestHandler::parseBenchmarkMode(options.get("benchmark_mode", false))
		== RequestHandler::BM_UNKNOWN)
	{
		fprintf(stderr, "ERROR: '%s' is not a valid mode for --benchmark.\n",
			options.get("benchmark_mode", false).c_str());
		ok = false;
	}
	if (options.getInt("server_threads") < 1) {
		fprintf(stderr, "ERROR: you may only specify for --threads a number greater than or equal to 1.\n");
		ok = false;
	}
	if (options.getInt("max_pool_size") < 1) {
		fprintf(stderr, "ERROR: you may only specify for --max-pool-size a number greater than or equal to 1.\n");
		ok = false;
	}

	if (!ok) {
		exit(1);
	}
}

int
serverMain(int argc, char *argv[]) {
	agentsOptions = new VariantMap();
	*agentsOptions = initializeAgent(argc, &argv, AGENT_EXE " server", parseOptions,
		preinitialize, 2);
	setAgentsOptionsDefaults();
	sanityCheckOptions();
	return runServer();
}
