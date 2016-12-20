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

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif
#ifdef __linux__
	#define SUPPORTS_PER_THREAD_CPU_AFFINITY
	#include <sched.h>
	#include <pthread.h>
#endif
#ifdef USE_SELINUX
	#include <selinux/selinux.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/resource.h>
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
#include <stdexcept>

#include <curl/curl.h>

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/atomic.hpp>
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>

#include <ev++.h>
#include <jsoncpp/json.h>

#include <Shared/Base.h>
#include <Shared/ApiServerUtils.h>
#include <Constants.h>
#include <ServerKit/Server.h>
#include <ServerKit/AcceptLoadBalancer.h>
#include <MessageReadersWriters.h>
#include <FileDescriptor.h>
#include <ResourceLocator.h>
#include <BackgroundEventLoop.cpp>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/Timer.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/VariantMap.h>
#include <Core/OptionParser.h>
#include <Core/Controller.h>
#include <Core/ApiServer.h>
#include <Core/ApplicationPool/Pool.h>
#include <Core/UnionStation/Context.h>
#include <Core/SecurityUpdateChecker.h>

using namespace boost;
using namespace oxt;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;


/***** Structures, constants, global variables and forward declarations *****/

namespace Passenger {
namespace Core {
	struct ThreadWorkingObjects {
		BackgroundEventLoop *bgloop;
		ServerKit::Context *serverKitContext;
		Controller *controller;

		ThreadWorkingObjects()
			: bgloop(NULL),
			  serverKitContext(NULL),
			  controller(NULL)
			{ }
	};

	struct ApiWorkingObjects {
		BackgroundEventLoop *bgloop;
		ServerKit::Context *serverKitContext;
		ApiServer::ApiServer *apiServer;

		ApiWorkingObjects()
			: bgloop(NULL),
			  serverKitContext(NULL),
			  apiServer(NULL)
			{ }
	};

	struct WorkingObjects {
		int serverFds[SERVER_KIT_MAX_SERVER_ENDPOINTS];
		int apiServerFds[SERVER_KIT_MAX_SERVER_ENDPOINTS];
		string password;
		ApiAccountDatabase apiAccountDatabase;

		ResourceLocator resourceLocator;
		RandomGeneratorPtr randomGenerator;
		UnionStation::ContextPtr unionStationContext;
		SpawningKit::ConfigPtr spawningKitConfig;
		SpawningKit::FactoryPtr spawningKitFactory;
		PoolPtr appPool;

		ServerKit::AcceptLoadBalancer<Controller> loadBalancer;
		vector<ThreadWorkingObjects> threadWorkingObjects;
		struct ev_signal sigintWatcher;
		struct ev_signal sigtermWatcher;
		struct ev_signal sigquitWatcher;

		ApiWorkingObjects apiWorkingObjects;

		EventFd exitEvent;
		EventFd allClientsDisconnectedEvent;
		unsigned int terminationCount;
		boost::atomic<unsigned int> shutdownCounter;
		oxt::thread *prestarterThread;

		SecurityUpdateChecker *securityUpdateChecker;

		WorkingObjects()
			: exitEvent(__FILE__, __LINE__, "WorkingObjects: exitEvent"),
			  allClientsDisconnectedEvent(__FILE__, __LINE__, "WorkingObjects: allClientsDisconnectedEvent"),
			  terminationCount(0),
			  shutdownCounter(0),
			  prestarterThread(NULL),
			  securityUpdateChecker(NULL)
		{
			for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
				serverFds[i] = -1;
				apiServerFds[i] = -1;
			}
		}

		~WorkingObjects() {
			delete prestarterThread;
			if (securityUpdateChecker) {
				delete securityUpdateChecker;
			}

			vector<ThreadWorkingObjects>::iterator it, end = threadWorkingObjects.end();
			for (it = threadWorkingObjects.begin(); it != end; it++) {
				delete it->controller;
				delete it->serverKitContext;
				delete it->bgloop;
			}

			delete apiWorkingObjects.apiServer;
			delete apiWorkingObjects.serverKitContext;
			delete apiWorkingObjects.bgloop;
		}
	};
} // namespace Core
} // namespace Passenger

using namespace Passenger::Core;

static VariantMap *agentsOptions;
static WorkingObjects *workingObjects;


/***** Core stuff *****/

static void waitForExitEvent();
static void cleanup();
static void deletePidFile();
static void abortLongRunningConnections(const ApplicationPool2::ProcessPtr &process);
static void controllerShutdownFinished(Controller *controller);
static void apiServerShutdownFinished(Core::ApiServer::ApiServer *server);
static void printInfoInThread();

static void
initializePrivilegedWorkingObjects() {
	TRACE_POINT();
	const VariantMap &options = *agentsOptions;
	WorkingObjects *wo = workingObjects = new WorkingObjects();

	wo->prestarterThread = NULL;

	wo->password = options.get("core_password", false);
	if (wo->password == "-") {
		wo->password.clear();
	} else if (wo->password.empty() && options.has("core_password_file")) {
		wo->password = strip(readAll(options.get("core_password_file")));
	}

	vector<string> authorizations = options.getStrSet("core_authorizations",
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
}

static void
initializeSingleAppMode() {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;

	if (options.getBool("multi_app")) {
		P_NOTICE(SHORT_PROGRAM_NAME " core running in multi-application mode.");
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
				"Type '" SHORT_PROGRAM_NAME " core --help' for more information.\n",
				options.get("app_root").c_str());
			exit(1);
		}

		options.set("app_type", getAppTypeName(appType));
		options.set("startup_file", options.get("app_root") + "/" + getAppTypeStartupFile(appType));
	}

	P_NOTICE(SHORT_PROGRAM_NAME " core running in single-application mode.");
	P_NOTICE("Serving app     : " << options.get("app_root"));
	P_NOTICE("App type        : " << options.get("app_type"));
	P_NOTICE("App startup file: " << options.get("startup_file"));
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
makeFileWorldReadableAndWritable(const string &path) {
	int ret;

	do {
		ret = chmod(path.c_str(), parseModeString("u=rw,g=rw,o=rw"));
	} while (ret == -1 && errno == EINTR);
}

#ifdef USE_SELINUX
	// Set next socket context to *:system_r:passenger_instance_httpd_socket_t.
	// Note that this only sets the context of the socket file descriptor,
	// not the socket file on the filesystem. This is why we need selinuxRelabelFile().
	static void
	setSelinuxSocketContext() {
		security_context_t currentCon;
		string newCon;
		int e;

		if (getcon(&currentCon) == -1) {
			e = errno;
			P_DEBUG("Unable to obtain SELinux context: " <<
				strerror(e) << " (errno=" << e << ")");
			return;
		}

		P_DEBUG("Current SELinux process context: " << currentCon);

		if (strstr(currentCon, ":unconfined_r:unconfined_t:") == NULL) {
			goto cleanup;
		}

		newCon = replaceString(currentCon,
			":unconfined_r:unconfined_t:",
			":object_r:passenger_instance_httpd_socket_t:");
		if (setsockcreatecon((security_context_t) newCon.c_str()) == -1) {
			e = errno;
			P_WARN("Cannot set SELinux socket context to " << newCon <<
				": " << strerror(e) << " (errno=" << e << ")");
			goto cleanup;
		}

		cleanup:
		freecon(currentCon);
	}

	static void
	resetSelinuxSocketContext() {
		setsockcreatecon(NULL);
	}

	static void
	selinuxRelabelFile(const string &path, const char *newLabel) {
		security_context_t currentCon;
		string newCon;
		int e;

		if (getfilecon(path.c_str(), &currentCon) == -1) {
			e = errno;
			P_DEBUG("Unable to obtain SELinux context for file " <<
				path <<": " << strerror(e) << " (errno=" << e << ")");
			return;
		}

		P_DEBUG("SELinux context for " << path << ": " << currentCon);

		if (strstr(currentCon, ":object_r:passenger_instance_content_t:") == NULL) {
			goto cleanup;
		}
		newCon = replaceString(currentCon,
			":object_r:passenger_instance_content_t:",
			StaticString(":object_r:") + newLabel + ":");
		P_DEBUG("Relabeling " << path << " to: " << newCon);

		if (setfilecon(path.c_str(), (security_context_t) newCon.c_str()) == -1) {
			e = errno;
			P_WARN("Cannot set SELinux context for " << path <<
				" to " << newCon << ": " << strerror(e) <<
				" (errno=" << e << ")");
		}

		cleanup:
		freecon(currentCon);
	}
#endif

static void
startListening() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	vector<string> addresses = agentsOptions->getStrSet("core_addresses");
	vector<string> apiAddresses = agentsOptions->getStrSet("core_api_addresses", false);

	#ifdef USE_SELINUX
		// Set SELinux context on the first socket that we create
		// so that the web server can access it.
		setSelinuxSocketContext();
	#endif

	for (unsigned int i = 0; i < addresses.size(); i++) {
		wo->serverFds[i] = createServer(addresses[i], agentsOptions->getInt("socket_backlog"), true,
			__FILE__, __LINE__);
		#ifdef USE_SELINUX
			resetSelinuxSocketContext();
			if (i == 0 && getSocketAddressType(addresses[0]) == SAT_UNIX) {
				// setSelinuxSocketContext() sets the context of the
				// socket file descriptor but not the file on the filesystem.
				// So we relabel the socket file here.
				selinuxRelabelFile(parseUnixSocketAddress(addresses[0]),
					"passenger_instance_httpd_socket_t");
			}
		#endif
		P_LOG_FILE_DESCRIPTOR_PURPOSE(wo->serverFds[i],
			"Server address: " << addresses[i]);
		if (getSocketAddressType(addresses[i]) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(addresses[i]));
		}
	}
	for (unsigned int i = 0; i < apiAddresses.size(); i++) {
		wo->apiServerFds[i] = createServer(apiAddresses[i], 0, true,
			__FILE__, __LINE__);
		P_LOG_FILE_DESCRIPTOR_PURPOSE(wo->apiServerFds[i],
			"ApiServer address: " << apiAddresses[i]);
		if (getSocketAddressType(apiAddresses[i]) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(apiAddresses[i]));
		}
	}
}

static void
createPidFile() {
	TRACE_POINT();
	string pidFile = agentsOptions->get("core_pid_file", false);
	if (!pidFile.empty()) {
		char pidStr[32];

		snprintf(pidStr, sizeof(pidStr), "%lld", (long long) getpid());

		int fd = syscalls::open(pidFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd == -1) {
			int e = errno;
			throw FileSystemException("Cannot create PID file " + pidFile, e, pidFile);
		}

		UPDATE_TRACE_POINT();
		FdGuard guard(fd, __FILE__, __LINE__);
		writeExact(fd, pidStr, strlen(pidStr));
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
inspectControllerStateAsJson(Controller *controller, string *result) {
	*result = controller->inspectStateAsJson().toStyledString();
}

static void
inspectControllerConfigAsJson(Controller *controller, string *result) {
	*result = controller->getConfigAsJson().toStyledString();
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
		two->bgloop->safe->runSync(boost::bind(inspectControllerStateAsJson,
			two->controller, &json));
		cerr << json;
		cerr << "\n";
		cerr.flush();
	}

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		string json;

		cerr << "### Request handler config (thread " << (i + 1) << ")\n";
		two->bgloop->safe->runSync(boost::bind(inspectControllerConfigAsJson,
			two->controller, &json));
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
		cerr << two->controller->inspectStateAsJson();
		cerr << "\n";
		cerr.flush();
	}

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		cerr << "### Request handler config (thread " << (i + 1) << ")\n";
		cerr << two->controller->getConfigAsJson();
		cerr << "\n";
		cerr.flush();
	}

	cerr << "### Pool state (simple)\n";
	// Do not lock, the crash may occur within the pool.
	Pool::InspectOptions options(Pool::InspectOptions::makeAuthorized());
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
	Pool::ToXmlOptions options2(Pool::ToXmlOptions::makeAuthorized());
	options2.secrets = true;
	cerr << wo->appPool->toXml(options2, false);
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
spawningKitErrorHandler(const SpawningKit::ConfigPtr &config, SpawnException &e, const Options &options) {
	ApplicationPool2::processAndLogNewSpawnException(e, options, config);
}

static void
initializeCurl() {
	TRACE_POINT();
	CURLcode code = curl_global_init(CURL_GLOBAL_ALL); // Initializes underlying TLS stack
	if (code != CURLE_OK) {
		P_CRITICAL("Could not initialize libcurl: " << curl_easy_strerror(code));
		exit(1);
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

	vector<string> addresses = options.getStrSet("core_addresses");
	vector<string> apiAddresses = options.getStrSet("core_api_addresses", false);

	wo->resourceLocator = ResourceLocator(options.get("passenger_root"));

	wo->randomGenerator = boost::make_shared<RandomGenerator>();
	// Check whether /dev/urandom is actually random.
	// https://code.google.com/p/phusion-passenger/issues/detail?id=516
	if (wo->randomGenerator->generateByteString(16) == wo->randomGenerator->generateByteString(16)) {
		throw RuntimeException("Your random number device, /dev/urandom, appears to be broken. "
			"It doesn't seem to be returning random data. Please fix this.");
	}

	UPDATE_TRACE_POINT();
	if (options.has("ust_router_address")) {
		wo->unionStationContext = boost::make_shared<UnionStation::Context>(
			options.get("ust_router_address"),
			"logging",
			options.get("ust_router_password"));
	}

	UPDATE_TRACE_POINT();
	wo->spawningKitConfig = boost::make_shared<SpawningKit::Config>();
	wo->spawningKitConfig->resourceLocator = &wo->resourceLocator;
	wo->spawningKitConfig->agentsOptions = agentsOptions;
	wo->spawningKitConfig->errorHandler = spawningKitErrorHandler;
	wo->spawningKitConfig->unionStationContext = wo->unionStationContext;
	wo->spawningKitConfig->randomGenerator = wo->randomGenerator;
	wo->spawningKitConfig->instanceDir = options.get("instance_dir", false);
	if (!wo->spawningKitConfig->instanceDir.empty()) {
		wo->spawningKitConfig->instanceDir = absolutizePath(
			wo->spawningKitConfig->instanceDir);
	}
	wo->spawningKitConfig->finalize();

	UPDATE_TRACE_POINT();
	wo->spawningKitFactory = boost::make_shared<SpawningKit::Factory>(wo->spawningKitConfig);
	wo->appPool = boost::make_shared<Pool>(wo->spawningKitFactory, agentsOptions);
	wo->appPool->initialize();
	wo->appPool->setMax(options.getInt("max_pool_size"));
	wo->appPool->setMaxIdleTime(options.getInt("pool_idle_time") * 1000000ULL);
	wo->appPool->enableSelfChecking(options.getBool("selfchecks"));
	wo->appPool->abortLongRunningConnectionsCallback = abortLongRunningConnections;

	UPDATE_TRACE_POINT();
	unsigned int nthreads = options.getInt("core_threads");
	BackgroundEventLoop *firstLoop = NULL; // Avoid compiler warning
	wo->threadWorkingObjects.reserve(nthreads);
	for (unsigned int i = 0; i < nthreads; i++) {
		UPDATE_TRACE_POINT();
		ThreadWorkingObjects two;

		if (i == 0) {
			two.bgloop = firstLoop = new BackgroundEventLoop(true, true);
		} else {
			two.bgloop = new BackgroundEventLoop(true, true);
		}

		UPDATE_TRACE_POINT();
		two.serverKitContext = new ServerKit::Context(two.bgloop->safe,
			two.bgloop->libuv_loop);
		two.serverKitContext->secureModePassword = wo->password;
		two.serverKitContext->defaultFileBufferedChannelConfig.bufferDir =
			options.get("data_buffer_dir");
		two.serverKitContext->defaultFileBufferedChannelConfig.threshold =
			options.getUint("file_buffer_threshold");

		UPDATE_TRACE_POINT();
		two.controller = new Core::Controller(two.serverKitContext, agentsOptions, i + 1);
		two.controller->minSpareClients = 128;
		two.controller->clientFreelistLimit = 1024;
		two.controller->resourceLocator = &wo->resourceLocator;
		two.controller->appPool = wo->appPool;
		two.controller->unionStationContext = wo->unionStationContext;
		two.controller->shutdownFinishCallback = controllerShutdownFinished;
		two.controller->initialize();
		wo->shutdownCounter.fetch_add(1, boost::memory_order_relaxed);

		wo->threadWorkingObjects.push_back(two);
	}

	UPDATE_TRACE_POINT();
	ev_signal_init(&wo->sigquitWatcher, printInfo, SIGQUIT);
	ev_signal_start(firstLoop->libev_loop, &wo->sigquitWatcher);
	ev_signal_init(&wo->sigintWatcher, onTerminationSignal, SIGINT);
	ev_signal_start(firstLoop->libev_loop, &wo->sigintWatcher);
	ev_signal_init(&wo->sigtermWatcher, onTerminationSignal, SIGTERM);
	ev_signal_start(firstLoop->libev_loop, &wo->sigtermWatcher);

	UPDATE_TRACE_POINT();
	if (!apiAddresses.empty()) {
		UPDATE_TRACE_POINT();
		ApiWorkingObjects *awo = &wo->apiWorkingObjects;

		awo->bgloop = new BackgroundEventLoop(true, true);
		awo->serverKitContext = new ServerKit::Context(awo->bgloop->safe,
			awo->bgloop->libuv_loop);
		awo->serverKitContext->secureModePassword = wo->password;
		awo->serverKitContext->defaultFileBufferedChannelConfig.bufferDir =
			options.get("data_buffer_dir");
		awo->serverKitContext->defaultFileBufferedChannelConfig.threshold =
			options.getUint("file_buffer_threshold");

		UPDATE_TRACE_POINT();
		awo->apiServer = new Core::ApiServer::ApiServer(awo->serverKitContext);
		awo->apiServer->controllers.reserve(wo->threadWorkingObjects.size());
		for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
			awo->apiServer->controllers.push_back(
				wo->threadWorkingObjects[i].controller);
		}
		awo->apiServer->apiAccountDatabase = &wo->apiAccountDatabase;
		awo->apiServer->appPool = wo->appPool;
		awo->apiServer->instanceDir = options.get("instance_dir", false);
		awo->apiServer->fdPassingPassword = options.get("watchdog_fd_passing_password", false);
		awo->apiServer->exitEvent = &wo->exitEvent;
		awo->apiServer->shutdownFinishCallback = apiServerShutdownFinished;

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
			two->controller->listen(wo->serverFds[i]);
		} else {
			wo->loadBalancer.listen(wo->serverFds[i]);
		}
	}
	for (unsigned int i = 0; i < nthreads; i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		two->controller->createSpareClients();
	}
	if (nthreads > 1) {
		wo->loadBalancer.servers.reserve(nthreads);
		for (unsigned int i = 0; i < nthreads; i++) {
			ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
			wo->loadBalancer.servers.push_back(two->controller);
		}
	}
	for (unsigned int i = 0; i < apiAddresses.size(); i++) {
		wo->apiWorkingObjects.apiServer->listen(wo->apiServerFds[i]);
	}
}

static void
initializeSecurityUpdateChecker() {
	TRACE_POINT();

	VariantMap &options = *agentsOptions;
	if (options.getBool("disable_security_update_check", false, false)) {
		P_NOTICE("Security update check disabled.");
	} else {
		string proxy = options.get("security_update_check_proxy", false);

		string serverIntegration = options.get("integration_mode"); // nginx / apache / standalone
		string standaloneEngine = options.get("standalone_engine", false); // nginx / builtin
		if (!standaloneEngine.empty()) {
			serverIntegration.append(" " + standaloneEngine);
		}
		if (options.get("server_software").find(FLYING_PASSENGER_NAME) != string::npos) {
			serverIntegration.append(" flying");
		}
		string serverVersion = options.get("server_version", false); // not set in case of standalone / builtin

		workingObjects->securityUpdateChecker = new SecurityUpdateChecker(workingObjects->resourceLocator, proxy, serverIntegration, serverVersion, options.get("instance_dir",false));
		workingObjects->securityUpdateChecker->start(24 * 60 * 60);
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
		P_NOTICE(SHORT_PROGRAM_NAME " core online, PID " << getpid());
		writeArrayMessage(FEEDBACK_FD,
			"initialized",
			NULL);
	} else {
		vector<string> addresses = agentsOptions->getStrSet("core_addresses");
		vector<string> apiAddresses = agentsOptions->getStrSet("core_api_addresses", false);
		string address;

		P_NOTICE(SHORT_PROGRAM_NAME " core online, PID " << getpid() <<
			", listening on " << addresses.size() << " socket(s):");
		foreach (address, addresses) {
			if (startsWith(address, "tcp://")) {
				address.erase(0, sizeof("tcp://") - 1);
				address.insert(0, "http://");
				address.append("/");
			}
			P_NOTICE(" * " << address);
		}

		if (!apiAddresses.empty()) {
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
mainLoop() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	#ifdef SUPPORTS_PER_THREAD_CPU_AFFINITY
		unsigned int maxCpus = boost::thread::hardware_concurrency();
		bool cpuAffine = agentsOptions->getBool("core_cpu_affine")
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
				P_DEBUG("Setting CPU affinity of core thread " << (i + 1)
					<< " to CPU " << (i % maxCpus + 1));
				result = pthread_setaffinity_np(two->bgloop->getNativeHandle(),
					maxCpus, &cpus);
				if (result != 0) {
					P_WARN("Cannot set CPU affinity on core thread " << (i + 1)
						<< ": " << strerror(result) << " (errno=" << result << ")");
				}
			}
		#endif
	}
	if (wo->apiWorkingObjects.apiServer != NULL) {
		wo->apiWorkingObjects.bgloop->start("API event loop", 0);
	}
	if (wo->threadWorkingObjects.size() > 1) {
		wo->loadBalancer.start();
	}
	waitForExitEvent();
}

static void
abortLongRunningConnectionsOnController(Core::Controller *controller,
	string gupid)
{
	controller->disconnectLongRunningConnections(gupid);
}

static void
abortLongRunningConnections(const ApplicationPool2::ProcessPtr &process) {
	// We are inside the ApplicationPool lock. Be very careful here.
	WorkingObjects *wo = workingObjects;
	P_NOTICE("Checking whether to disconnect long-running connections for process " <<
		process->getPid() << ", application " << process->getGroup()->getName());
	for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
		wo->threadWorkingObjects[i].bgloop->safe->runLater(
			boost::bind(abortLongRunningConnectionsOnController,
				wo->threadWorkingObjects[i].controller,
				process->getGupid().toString()));
	}
}

static void
shutdownController(ThreadWorkingObjects *two) {
	two->controller->shutdown();
}

static void
shutdownApiServer() {
	workingObjects->apiWorkingObjects.apiServer->shutdown();
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
controllerShutdownFinished(Core::Controller *controller) {
	serverShutdownFinished();
}

static void
apiServerShutdownFinished(Core::ApiServer::ApiServer *server) {
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
			two->bgloop->safe->runLater(boost::bind(shutdownController, two));
		}
		if (wo->threadWorkingObjects.size() > 1) {
			wo->loadBalancer.shutdown();
		}
		if (wo->apiWorkingObjects.apiServer != NULL) {
			wo->apiWorkingObjects.bgloop->safe->runLater(shutdownApiServer);
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

	P_DEBUG("Shutting down " SHORT_PROGRAM_NAME " core...");
	wo->appPool->destroy();
	installDiagnosticsDumper(NULL, NULL);
	for (unsigned i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		two->bgloop->stop();
	}
	if (wo->apiWorkingObjects.apiServer != NULL) {
		wo->apiWorkingObjects.bgloop->stop();
	}
	wo->appPool.reset();
	for (unsigned i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		delete two->controller;
		two->controller = NULL;
	}
	if (wo->prestarterThread != NULL) {
		wo->prestarterThread->interrupt_and_join();
		delete wo->prestarterThread;
		wo->prestarterThread = NULL;
	}
	for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
		if (wo->serverFds[i] != -1) {
			close(wo->serverFds[i]);
		}
		if (wo->apiServerFds[i] != -1) {
			close(wo->apiServerFds[i]);
		}
	}
	deletePidFile();
	delete workingObjects;
	workingObjects = NULL;
	P_NOTICE(SHORT_PROGRAM_NAME " core shutdown finished");
}

static void
deletePidFile() {
	TRACE_POINT();
	string pidFile = agentsOptions->get("core_pid_file", false);
	if (!pidFile.empty()) {
		syscalls::unlink(pidFile.c_str());
	}
}

static int
runCore() {
	TRACE_POINT();
	P_NOTICE("Starting " SHORT_PROGRAM_NAME " core...");

	try {
		UPDATE_TRACE_POINT();
		initializePrivilegedWorkingObjects();
		initializeSingleAppMode();
		setUlimits();
		startListening();
		createPidFile();
		lowerPrivilege();
		initializeCurl();
		initializeNonPrivilegedWorkingObjects();
		initializeSecurityUpdateChecker();
		prestartWebApps();

		UPDATE_TRACE_POINT();
		reportInitializationInfo();
		mainLoop();

		UPDATE_TRACE_POINT();
		cleanup();
	} catch (const tracable_exception &e) {
		// We intentionally don't call cleanup() in
		// order to avoid various destructor assertions.
		P_CRITICAL("ERROR: " << e.what() << "\n" << e.backtrace());
		deletePidFile();
		return 1;
	} catch (const std::runtime_error &e) {
		P_CRITICAL("ERROR: " << e.what());
		deletePidFile();
		return 1;
	}

	return 0;
}


/***** Entry point and command line argument parsing *****/

static void
parseOptions(int argc, const char *argv[], VariantMap &options) {
	OptionParser p(coreUsage);
	int i = 2;

	while (i < argc) {
		if (parseCoreOption(argc, argv, i, options)) {
			continue;
		} else if (p.isFlag(argv[i], 'h', "--help")) {
			coreUsage();
			exit(0);
		} else {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s core --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		}
	}
}

static void
preinitialize(VariantMap &options) {
	// Set log_level here so that initializeAgent() calls setLogLevel()
	// and setLogFile() with the right value.
	if (options.has("core_log_level")) {
		options.setInt("log_level", options.getInt("core_log_level"));
	}
	if (options.has("core_log_file")) {
		options.set("log_file", options.get("core_log_file"));
	}
	if (options.has("core_file_descriptor_log_file")) {
		options.set("file_descriptor_log_file", options.get("core_file_descriptor_log_file"));
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
	options.setDefault("integration_mode", "standalone");
	if (options.get("integration_mode") == "standalone" && !options.has("standalone_engine")) {
		options.set("standalone_engine", "builtin");
	}
	options.setDefaultStrSet("core_addresses", defaultAddress);
	options.setDefaultInt("socket_backlog", DEFAULT_SOCKET_BACKLOG);
	options.setDefaultBool("multi_app", false);
	options.setDefault("environment", DEFAULT_APP_ENV);
	options.setDefault("spawn_method", DEFAULT_SPAWN_METHOD);
	options.setDefaultBool("load_shell_envvars", false);
	options.setDefaultBool("abort_websockets_on_process_shutdown", true);
	options.setDefaultInt("force_max_concurrent_requests_per_process", -1);
	options.setDefault("concurrency_model", DEFAULT_CONCURRENCY_MODEL);
	options.setDefaultInt("app_thread_count", DEFAULT_APP_THREAD_COUNT);
	options.setDefaultInt("max_pool_size", DEFAULT_MAX_POOL_SIZE);
	options.setDefaultInt("pool_idle_time", DEFAULT_POOL_IDLE_TIME);
	options.setDefaultInt("min_instances", 1);
	options.setDefaultInt("max_preloader_idle_time", DEFAULT_MAX_PRELOADER_IDLE_TIME);
	options.setDefaultUint("max_request_queue_size", DEFAULT_MAX_REQUEST_QUEUE_SIZE);
	options.setDefaultUint("stat_throttle_rate", DEFAULT_STAT_THROTTLE_RATE);
	options.setDefault("server_software", SERVER_TOKEN_NAME "/" PASSENGER_VERSION);
	options.setDefaultBool("show_version_in_header", true);
	options.setDefaultBool("sticky_sessions", false);
	options.setDefault("sticky_sessions_cookie_name", DEFAULT_STICKY_SESSIONS_COOKIE_NAME);
	options.setDefaultBool("turbocaching", true);
	options.setDefault("data_buffer_dir", getSystemTempDir());
	options.setDefaultUint("file_buffer_threshold", DEFAULT_FILE_BUFFERED_CHANNEL_THRESHOLD);
	options.setDefaultInt("response_buffer_high_watermark", DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK);
	options.setDefaultBool("selfchecks", false);
	options.setDefaultBool("core_graceful_exit", true);
	options.setDefaultInt("core_threads", boost::thread::hardware_concurrency());
	options.setDefaultBool("core_cpu_affine", false);
	options.setDefault("friendly_error_pages", "auto");
	options.setDefaultBool("rolling_restarts", false);
	options.setDefaultBool("resist_deployment_errors", false);

	string firstAddress = options.getStrSet("core_addresses")[0];
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
	if (options.has("memory_limit")) {
		#ifndef PASSENGER_IS_ENTERPRISE
			fprintf(stderr, "ERROR: the --memory-limit option is only supported in "
				PROGRAM_NAME " Enterprise.\nYou are currently using the open source "
				PROGRAM_NAME ". Buy " PROGRAM_NAME " Enterprise here: https://www.phusionpassenger.com/enterprise\n");
			ok = false;
		#endif
	}
	if (options.has("max_requests")) {
		if (options.getInt("max_requests", false, 0) < 0) {
			fprintf(stderr, "ERROR: the value passed to --max-requests must be at least 0.\n");
			ok = false;
		}
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
	if (Core::Controller::parseBenchmarkMode(options.get("benchmark_mode", false))
		== Core::Controller::BM_UNKNOWN)
	{
		fprintf(stderr, "ERROR: '%s' is not a valid mode for --benchmark.\n",
			options.get("benchmark_mode", false).c_str());
		ok = false;
	}
	if (options.getInt("core_threads") < 1) {
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
coreMain(int argc, char *argv[]) {
	int ret;

	agentsOptions = new VariantMap();
	*agentsOptions = initializeAgent(argc, &argv, SHORT_PROGRAM_NAME " core", parseOptions,
		preinitialize, 2);
	setAgentsOptionsDefaults();
	sanityCheckOptions();

	restoreOomScore(agentsOptions);

	ret = runCore();
	shutdownAgent(agentsOptions);
	return ret;
}
