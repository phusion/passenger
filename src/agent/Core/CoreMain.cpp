/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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

#include <boost/config.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/foreach.hpp>
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

#include <Shared/Fundamentals/Initialization.h>
#include <Shared/ApiServerUtils.h>
#include <Constants.h>
#include <LoggingKit/Context.h>
#include <ConfigKit/SubComponentUtils.h>
#include <ServerKit/Server.h>
#include <ServerKit/AcceptLoadBalancer.h>
#include <AppTypeDetector/Detector.h>
#include <IOTools/MessageSerialization.h>
#include <FileDescriptor.h>
#include <ResourceLocator.h>
#include <BackgroundEventLoop.cpp>
#include <FileTools/FileManip.h>
#include <FileTools/PathSecurityCheck.h>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/Timer.h>
#include <IOTools/MessageIO.h>
#include <Core/OptionParser.h>
#include <Core/Controller.h>
#include <Core/ApiServer.h>
#include <Core/Config.h>
#include <Core/ConfigChange.h>
#include <Core/ApplicationPool/Pool.h>
#include <Core/SecurityUpdateChecker.h>
#include <Core/TelemetryCollector.h>
#include <Core/AdminPanelConnector.h>

using namespace boost;
using namespace oxt;
using namespace Passenger;
using namespace Passenger::Agent::Fundamentals;
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
		string controllerSecureHeadersPassword;

		boost::mutex configSyncher;

		ResourceLocator resourceLocator;
		RandomGeneratorPtr randomGenerator;
		SpawningKit::Context::Schema spawningKitContextSchema;
		SpawningKit::ContextPtr spawningKitContext;
		ApplicationPool2::ContextPtr appPoolContext;
		PoolPtr appPool;
		Json::Value singleAppModeConfig;

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
		TelemetryCollector *telemetryCollector;
		AdminPanelConnector *adminPanelConnector;
		oxt::thread *adminPanelConnectorThread;

		WorkingObjects()
			: exitEvent(__FILE__, __LINE__, "WorkingObjects: exitEvent"),
			  allClientsDisconnectedEvent(__FILE__, __LINE__, "WorkingObjects: allClientsDisconnectedEvent"),
			  terminationCount(0),
			  shutdownCounter(0),
			  prestarterThread(NULL),
			  securityUpdateChecker(NULL),
			  telemetryCollector(NULL),
			  adminPanelConnector(NULL),
			  adminPanelConnectorThread(NULL)
			  /*******************/
		{
			for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
				serverFds[i] = -1;
				apiServerFds[i] = -1;
			}
		}

		~WorkingObjects() {
			delete prestarterThread;
			delete adminPanelConnectorThread;
			delete adminPanelConnector;
			delete securityUpdateChecker;
			delete telemetryCollector;

			/*******************/
			/*******************/

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

static WrapperRegistry::Registry *coreWrapperRegistry;
static Schema *coreSchema;
static ConfigKit::Store *coreConfig;
static WorkingObjects *workingObjects;

#include <Core/ConfigChange.cpp>


/***** Core stuff *****/

static void waitForExitEvent();
static void cleanup();
static void deletePidFile();
static void abortLongRunningConnections(const ApplicationPool2::ProcessPtr &process);
static void serverShutdownFinished();
static void controllerShutdownFinished(Controller *controller);
static void apiServerShutdownFinished(Core::ApiServer::ApiServer *server);
static void printInfoInThread();

static void
initializePrivilegedWorkingObjects() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects = new WorkingObjects();

	Json::Value password = coreConfig->get("controller_secure_headers_password");
	if (password.isString()) {
		wo->controllerSecureHeadersPassword = password.asString();
	} else if (password.isObject()) {
		wo->controllerSecureHeadersPassword = strip(unsafeReadFile(password["path"].asString()));
	}
}

static void
initializeSingleAppMode() {
	TRACE_POINT();

	if (coreConfig->get("multi_app").asBool()) {
		P_NOTICE(SHORT_PROGRAM_NAME " core running in multi-application mode.");
		return;
	}

	WorkingObjects *wo = workingObjects;
	string appType, startupFile;
	string appRoot = coreConfig->get("single_app_mode_app_root").asString();

	if (coreConfig->get("single_app_mode_app_type").isNull()) {
		P_DEBUG("Autodetecting application type...");
		AppTypeDetector::Detector detector(*coreWrapperRegistry, NULL, 0);
		AppTypeDetector::Detector::Result result = detector.checkAppRoot(appRoot);
		if (result.isNull()) {
			fprintf(stderr, "ERROR: unable to autodetect what kind of application "
				"lives in %s. Please specify information about the app using "
				"--app-type and --startup-file, or specify a correct location to "
				"the application you want to serve.\n"
				"Type '" SHORT_PROGRAM_NAME " core --help' for more information.\n",
				appRoot.c_str());
			exit(1);
		}

		appType = result.wrapperRegistryEntry->language;
	} else {
		appType = coreConfig->get("single_app_mode_app_type").asString();
	}

	if (coreConfig->get("single_app_mode_startup_file").isNull()) {
		const WrapperRegistry::Entry &entry = coreWrapperRegistry->lookup(appType);
		if (entry.defaultStartupFiles.empty()) {
			startupFile = appRoot + "/";
		} else {
			startupFile = appRoot + "/" + entry.defaultStartupFiles[0];
		}
	} else {
		startupFile = coreConfig->get("single_app_mode_startup_file").asString();
	}
	if (!fileExists(startupFile)) {
		fprintf(stderr, "ERROR: unable to find expected startup file %s."
			" Please specify its correct path with --startup-file.\n",
			startupFile.c_str());
		exit(1);
	}

	wo->singleAppModeConfig["app_root"] = appRoot;
	wo->singleAppModeConfig["app_type"] = appType;
	wo->singleAppModeConfig["startup_file"] = startupFile;

	P_NOTICE(SHORT_PROGRAM_NAME " core running in single-application mode.");
	P_NOTICE("Serving app     : " << appRoot);
	P_NOTICE("App type        : " << appType);
	P_NOTICE("App startup file: " << startupFile);
}

static void
setUlimits() {
	TRACE_POINT();
	unsigned int number = coreConfig->get("file_descriptor_ulimit").asUInt();
	if (number != 0) {
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
	const Json::Value addresses = coreConfig->get("controller_addresses");
	const Json::Value apiAddresses = coreConfig->get("api_server_addresses");
	Json::Value::const_iterator it;
	unsigned int i;

	#ifdef USE_SELINUX
		// Set SELinux context on the first socket that we create
		// so that the web server can access it.
		setSelinuxSocketContext();
	#endif

	for (it = addresses.begin(), i = 0; it != addresses.end(); it++, i++) {
		wo->serverFds[i] = createServer(it->asString(),
			coreConfig->get("controller_socket_backlog").asUInt(), true,
			__FILE__, __LINE__);
		#ifdef USE_SELINUX
			resetSelinuxSocketContext();
			if (i == 0 && getSocketAddressType(it->asString()) == SAT_UNIX) {
				// setSelinuxSocketContext() sets the context of the
				// socket file descriptor but not the file on the filesystem.
				// So we relabel the socket file here.
				selinuxRelabelFile(parseUnixSocketAddress(it->asString()),
					"passenger_instance_httpd_socket_t");
			}
		#endif
		P_LOG_FILE_DESCRIPTOR_PURPOSE(wo->serverFds[i],
			"Server address: " << it->asString());
		if (getSocketAddressType(it->asString()) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(it->asString()));
		}
	}
	for (it = apiAddresses.begin(), i = 0; it != apiAddresses.end(); it++, i++) {
		wo->apiServerFds[i] = createServer(it->asString(), 0, true,
			__FILE__, __LINE__);
		P_LOG_FILE_DESCRIPTOR_PURPOSE(wo->apiServerFds[i],
			"ApiServer address: " << it->asString());
		if (getSocketAddressType(it->asString()) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(it->asString()));
		}
	}
}

static void
createPidFile() {
	TRACE_POINT();
	Json::Value pidFile = coreConfig->get("pid_file");
	if (!pidFile.isNull()) {
		char pidStr[32];

		snprintf(pidStr, sizeof(pidStr), "%lld", (long long) getpid());

		int fd = syscalls::open(pidFile.asCString(),
			O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd == -1) {
			int e = errno;
			throw FileSystemException("Cannot create PID file "
				+ pidFile.asString(), e, pidFile.asString());
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
	*result = controller->inspectConfig().toStyledString();
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
dumpOxtBacktracesOnCrash(void *userData) {
	cerr << oxt::thread::all_backtraces();
	cerr.flush();
}

static void
dumpControllerStatesOnCrash(void *userData) {
	WorkingObjects *wo = workingObjects;
	unsigned int i;

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		cerr << "####### Controller state (thread " << (i + 1) << ") #######\n";
		cerr << two->controller->inspectStateAsJson();
		cerr << "\n\n";
		cerr.flush();
	}
}

static void
dumpControllerConfigsOnCrash(void *userData) {
	WorkingObjects *wo = workingObjects;
	unsigned int i;

	for (i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		cerr << "####### Controller config (thread " << (i + 1) << ") #######\n";
		cerr << two->controller->inspectConfig();
		cerr << "\n\n";
		cerr.flush();
	}
}

static void
dumpPoolStateOnCrash(void *userData) {
	WorkingObjects *wo = workingObjects;

	cerr << "####### Pool state (simple) #######\n";
	// Do not lock, the crash may occur within the pool.
	Pool::InspectOptions options(Pool::InspectOptions::makeAuthorized());
	options.verbose = true;
	cerr << wo->appPool->inspect(options, false);
	cerr << "\n\n";
	cerr.flush();

	cerr << "####### Pool state (XML) #######\n";
	Pool::ToXmlOptions options2(Pool::ToXmlOptions::makeAuthorized());
	options2.secrets = true;
	cerr << wo->appPool->toXml(options2, false);
	cerr << "\n\n";
	cerr.flush();
}

static void
dumpMbufStatsOnCrash(void *userData) {
	WorkingObjects *wo = workingObjects;
	cerr << "nfree_mbuf_blockq  : " <<
		wo->threadWorkingObjects[0].serverKitContext->mbuf_pool.nfree_mbuf_blockq << "\n";
	cerr << "nactive_mbuf_blockq: " <<
		wo->threadWorkingObjects[0].serverKitContext->mbuf_pool.nactive_mbuf_blockq << "\n";
	cerr << "mbuf_block_chunk_size: " <<
		wo->threadWorkingObjects[0].serverKitContext->mbuf_pool.mbuf_block_chunk_size << "\n";
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
	WorkingObjects *wo = workingObjects;

	const Json::Value addresses = coreConfig->get("controller_addresses");
	const Json::Value apiAddresses = coreConfig->get("api_server_addresses");

	setenv("SERVER_SOFTWARE", coreConfig->get("server_software").asCString(), 1);

	wo->resourceLocator = ResourceLocator(coreConfig->get("passenger_root").asString());

	wo->randomGenerator = boost::make_shared<RandomGenerator>();
	// Check whether /dev/urandom is actually random.
	// https://code.google.com/p/phusion-passenger/issues/detail?id=516
	if (wo->randomGenerator->generateByteString(16) == wo->randomGenerator->generateByteString(16)) {
		throw RuntimeException("Your random number device, /dev/urandom, appears to be broken. "
			"It doesn't seem to be returning random data. Please fix this.");
	}

	UPDATE_TRACE_POINT();
	wo->spawningKitContext = boost::make_shared<SpawningKit::Context>(
		wo->spawningKitContextSchema);
	wo->spawningKitContext->resourceLocator = &wo->resourceLocator;
	wo->spawningKitContext->wrapperRegistry = coreWrapperRegistry;
	wo->spawningKitContext->randomGenerator = wo->randomGenerator;
	wo->spawningKitContext->integrationMode = coreConfig->get("integration_mode").asString();
	wo->spawningKitContext->instanceDir = coreConfig->get("instance_dir").asString();
	if (!wo->spawningKitContext->instanceDir.empty()) {
		wo->spawningKitContext->instanceDir = absolutizePath(
			wo->spawningKitContext->instanceDir);
	}
	wo->spawningKitContext->finalize();

	UPDATE_TRACE_POINT();
	wo->appPoolContext = boost::make_shared<ApplicationPool2::Context>();
	wo->appPoolContext->spawningKitFactory = boost::make_shared<SpawningKit::Factory>(
		wo->spawningKitContext.get());
	wo->appPoolContext->agentConfig = coreConfig->inspectEffectiveValues();
	wo->appPoolContext->finalize();
	wo->appPool = boost::make_shared<Pool>(wo->appPoolContext.get());
	wo->appPool->initialize();
	wo->appPool->setMax(coreConfig->get("max_pool_size").asInt());
	wo->appPool->setMaxIdleTime(coreConfig->get("pool_idle_time").asInt() * 1000000ULL);
	wo->appPool->enableSelfChecking(coreConfig->get("pool_selfchecks").asBool());
	wo->appPool->abortLongRunningConnectionsCallback = abortLongRunningConnections;

	UPDATE_TRACE_POINT();
	unsigned int nthreads = coreConfig->get("controller_threads").asUInt();
	BackgroundEventLoop *firstLoop = NULL; // Avoid compiler warning
	wo->threadWorkingObjects.reserve(nthreads);
	for (unsigned int i = 0; i < nthreads; i++) {
		UPDATE_TRACE_POINT();
		ThreadWorkingObjects two;

		Json::Value contextConfig = coreConfig->inspectEffectiveValues();
		contextConfig["secure_mode_password"] = wo->controllerSecureHeadersPassword;

		Json::Value controllerConfig = coreConfig->inspectEffectiveValues();
		controllerConfig["thread_number"] = i + 1;

		if (i == 0) {
			two.bgloop = firstLoop = new BackgroundEventLoop(true, true);
		} else {
			two.bgloop = new BackgroundEventLoop(true, true);
		}

		UPDATE_TRACE_POINT();
		two.serverKitContext = new ServerKit::Context(
			coreSchema->controllerServerKit.schema,
			contextConfig,
			coreSchema->controllerServerKit.translator);
		two.serverKitContext->libev = two.bgloop->safe;
		two.serverKitContext->libuv = two.bgloop->libuv_loop;
		two.serverKitContext->initialize();

		UPDATE_TRACE_POINT();
		two.controller = new Core::Controller(two.serverKitContext,
			coreSchema->controller.schema,
			controllerConfig,
			coreSchema->controller.translator,
			&coreSchema->controllerSingleAppMode.schema,
			&wo->singleAppModeConfig,
			coreSchema->controllerSingleAppMode.translator);
		two.controller->resourceLocator = &wo->resourceLocator;
		two.controller->wrapperRegistry = coreWrapperRegistry;
		two.controller->appPool = wo->appPool;
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

		Json::Value contextConfig = coreConfig->inspectEffectiveValues();

		awo->bgloop = new BackgroundEventLoop(true, true);
		awo->serverKitContext = new ServerKit::Context(
			coreSchema->apiServerKit.schema,
			contextConfig,
			coreSchema->apiServerKit.translator);
		awo->serverKitContext->libev = awo->bgloop->safe;
		awo->serverKitContext->libuv = awo->bgloop->libuv_loop;
		awo->serverKitContext->initialize();

		UPDATE_TRACE_POINT();
		awo->apiServer = new Core::ApiServer::ApiServer(awo->serverKitContext,
			coreSchema->apiServer.schema, coreConfig->inspectEffectiveValues(),
			coreSchema->apiServer.translator);
		awo->apiServer->controllers.reserve(wo->threadWorkingObjects.size());
		for (unsigned int i = 0; i < wo->threadWorkingObjects.size(); i++) {
			awo->apiServer->controllers.push_back(
				wo->threadWorkingObjects[i].controller);
		}
		awo->apiServer->appPool = wo->appPool;
		awo->apiServer->exitEvent = &wo->exitEvent;
		awo->apiServer->shutdownFinishCallback = apiServerShutdownFinished;
		awo->apiServer->initialize();

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
	Json::Value config = coreConfig->inspectEffectiveValues();

	// nginx / apache / standalone
	string serverIdentifier = coreConfig->get("integration_mode").asString();
	// nginx / builtin
	if (!coreConfig->get("standalone_engine").isNull()) {
		serverIdentifier.append(" ");
		serverIdentifier.append(coreConfig->get("standalone_engine").asString());
	}
	if (coreConfig->get("server_software").asString().find(FLYING_PASSENGER_NAME) != string::npos) {
		serverIdentifier.append(" flying");
	}
	config["server_identifier"] = serverIdentifier;

	SecurityUpdateChecker *checker = new SecurityUpdateChecker(
		coreSchema->securityUpdateChecker.schema,
		config,
		coreSchema->securityUpdateChecker.translator);
	workingObjects->securityUpdateChecker = checker;
	checker->resourceLocator = &workingObjects->resourceLocator;
	checker->initialize();
	checker->start();
}

static void
initializeTelemetryCollector() {
	return; // disable for now
	TRACE_POINT();
	WorkingObjects &wo = *workingObjects;

	Json::Value config = coreConfig->inspectEffectiveValues();
	TelemetryCollector *collector = new TelemetryCollector(
		coreSchema->telemetryCollector.schema,
		coreConfig->inspectEffectiveValues(),
		coreSchema->telemetryCollector.translator);
	wo.telemetryCollector = collector;
	for (unsigned int i = 0; i < wo.threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo.threadWorkingObjects[i];
		collector->controllers.push_back(two->controller);
	}
	collector->initialize();
	collector->start();
	wo.shutdownCounter.fetch_add(1, boost::memory_order_relaxed);
}

static void
runAdminPanelConnector(AdminPanelConnector *connector) {
	connector->run();
	P_DEBUG("Admin panel connector shutdown finished");
	serverShutdownFinished();
}

static void
initializeAdminPanelConnector() {
	TRACE_POINT();
	WorkingObjects &wo = *workingObjects;

	if (coreConfig->get("admin_panel_url").isNull()) {
		return;
	}

	Json::Value config = coreConfig->inspectEffectiveValues();
	config["log_prefix"] = "AdminPanelConnector: ";
	config["ruby"] = config["default_ruby"];

	P_NOTICE("Initialize connection with " << PROGRAM_NAME " admin panel at "
		<< config["admin_panel_url"].asString());
	AdminPanelConnector *connector = new Core::AdminPanelConnector(
		coreSchema->adminPanelConnector.schema, config,
		coreSchema->adminPanelConnector.translator);
	connector->resourceLocator = &wo.resourceLocator;
	connector->appPool = wo.appPool;
	connector->configGetter = inspectConfig;
	for (unsigned int i = 0; i < wo.threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo.threadWorkingObjects[i];
		connector->controllers.push_back(two->controller);
	}
	connector->initialize();
	wo.shutdownCounter.fetch_add(1, boost::memory_order_relaxed);
	wo.adminPanelConnector = connector;
	wo.adminPanelConnectorThread = new oxt::thread(
		boost::bind(runAdminPanelConnector, connector),
		"Admin panel connector main loop", 128 * 1024);
}

static void
prestartWebApps() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	vector<string> prestartURLs;
	const Json::Value jPrestartURLs = coreConfig->get("prestart_urls");
	Json::Value::const_iterator it, end = jPrestartURLs.end();

	prestartURLs.reserve(jPrestartURLs.size());
	for (it = jPrestartURLs.begin(); it != end; it++) {
		prestartURLs.push_back(it->asString());
	}

	boost::function<void ()> func = boost::bind(prestartWebApps,
		wo->resourceLocator,
		coreConfig->get("default_ruby").asString(),
		prestartURLs
	);
	wo->prestarterThread = new oxt::thread(
		boost::bind(runAndPrintExceptions, func, true)
	);
}

/*
 * Emit a warning (log) if the Passenger root dir (and/or its parents) can be modified by non-root users
 * while Passenger was run as root (because non-root users can then tamper with something running as root).
 * It's just a convenience warning, so check failures are only logged at the debug level.
 *
 * N.B. we limit our checking to use cases that can easily (gotcha) lead to this vulnerable setup, such as
 * installing Passenger via gem or tarball in a user dir, and then running it as root (for example by installing
 * it as nginx or apache module). We do not check the entire installation file/dir structure for whether users have
 * changed owner or access rights.
 */
static void
warnIfPassengerRootVulnerable() {
	TRACE_POINT();

	if (geteuid() != 0) {
		return; // Passenger is not root, so no escalation.
	}

	string root = workingObjects->resourceLocator.getInstallSpec();
	vector<string> errors, checkErrors;
	if (isPathProbablySecureForRootUse(root, errors, checkErrors)) {
		if (!checkErrors.empty()) {
			string message = "WARNING: unable to perform privilege escalation vulnerability detection:\n";
			foreach (string line, checkErrors) {
				message.append("\n - " + line);
			}
			P_WARN(message);
		}
	} else {
		string message = "WARNING: potential privilege escalation vulnerability detected. " \
			PROGRAM_NAME " is running as root, and part(s) of the " SHORT_PROGRAM_NAME
			" root path (" + root + ") can be changed by non-root user(s):\n";
		foreach (string line, errors) {
			message.append("\n - " + line);
		}
		foreach (string line, checkErrors) {
			message.append("\n - " + line);
		}
		message.append("\n\nPlease either fix up the permissions for the insecure paths, or install "
			SHORT_PROGRAM_NAME " in a different location that can only be modified by root.");
		P_WARN(message);
	}
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
		const Json::Value addresses = coreConfig->get("controller_addresses");
		const Json::Value apiAddresses = coreConfig->get("api_server_addresses");
		Json::Value::const_iterator it;

		P_NOTICE(SHORT_PROGRAM_NAME " core online, PID " << getpid() <<
			", listening on " << addresses.size() << " socket(s):");
		for (it = addresses.begin(); it != addresses.end(); it++) {
			string address = it->asString();
			if (startsWith(address, "tcp://")) {
				address.erase(0, sizeof("tcp://") - 1);
				address.insert(0, "http://");
				address.append("/");
			}
			P_NOTICE(" * " << address);
		}

		if (!apiAddresses.empty()) {
			P_NOTICE("API server listening on " << apiAddresses.size() << " socket(s):");
			for (it = apiAddresses.begin(); it != apiAddresses.end(); it++) {
				string address = it->asString();
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
initializeAbortHandlerCustomerDiagnostics() {
	if (!Agent::Fundamentals::abortHandlerInstalled()) {
		return;
	}

	Agent::Fundamentals::AbortHandlerConfig::DiagnosticsDumper *diagnosticsDumpers
		= &Agent::Fundamentals::context->abortHandlerConfig.diagnosticsDumpers[0];

	diagnosticsDumpers[0].name = "OXT backtraces";
	diagnosticsDumpers[0].logFileName = "backtrace_oxt.log";
	diagnosticsDumpers[0].func = dumpOxtBacktracesOnCrash;

	diagnosticsDumpers[1].name = "controller states";
	diagnosticsDumpers[1].logFileName = "controller_states.log";
	diagnosticsDumpers[1].func = dumpControllerStatesOnCrash;

	diagnosticsDumpers[2].name = "controller configs";
	diagnosticsDumpers[2].logFileName = "controller_configs.log";
	diagnosticsDumpers[2].func = dumpControllerConfigsOnCrash;

	diagnosticsDumpers[3].name = "pool state";
	diagnosticsDumpers[3].logFileName = "pool.log";
	diagnosticsDumpers[3].func = dumpPoolStateOnCrash;

	diagnosticsDumpers[4].name = "mbuf statistics";
	diagnosticsDumpers[4].logFileName = "mbufs.log";
	diagnosticsDumpers[4].func = dumpMbufStatsOnCrash;

	Agent::Fundamentals::abortHandlerConfigChanged();
}

static void
uninstallAbortHandlerCustomDiagnostics() {
	if (!Agent::Fundamentals::abortHandlerInstalled()) {
		return;
	}

	for (unsigned int i = 0; i < Agent::Fundamentals::AbortHandlerConfig::MAX_DIAGNOSTICS_DUMPERS; i++) {
		Agent::Fundamentals::context->abortHandlerConfig.diagnosticsDumpers[i].func = NULL;
	}
	Agent::Fundamentals::abortHandlerConfigChanged();
}

static void
mainLoop() {
	TRACE_POINT();
	WorkingObjects *wo = workingObjects;
	#ifdef SUPPORTS_PER_THREAD_CPU_AFFINITY
		unsigned int maxCpus = boost::thread::hardware_concurrency();
		bool cpuAffine = coreConfig->get("controller_cpu_affine").asBool()
			&& maxCpus <= CPU_SETSIZE;
	#endif

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
	P_DEBUG("Shutdown counter = " << (i - 1));
	if (i == 1) {
		boost::atomic_thread_fence(boost::memory_order_acquire);
		workingObjects->allClientsDisconnectedEvent.notify();
	}
}

static void
controllerShutdownFinished(Core::Controller *controller) {
	P_DEBUG("Controller " << controller->getThreadNumber() << " shutdown finished");
	serverShutdownFinished();
}

static void
apiServerShutdownFinished(Core::ApiServer::ApiServer *server) {
	P_DEBUG("API server shutdown finished");
	serverShutdownFinished();
}

static void
telemetryCollectorAsyncShutdownThreadMain() {
	WorkingObjects *wo = workingObjects;
	wo->telemetryCollector->stop();
	serverShutdownFinished();
}

static void
asyncShutdownTelemetryCollector() {
	oxt::thread(telemetryCollectorAsyncShutdownThreadMain,
		"Telemetry collector shutdown",
		512 * 1024);
}

/* Wait until the watchdog closes the feedback fd (meaning it
 * was killed) or until we receive an exit message.
 */
static void
waitForExitEvent() {
	boost::this_thread::disable_syscall_interruption dsi;
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
		uninstallAbortHandlerCustomDiagnostics();
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
		if (wo->telemetryCollector != NULL) {
			asyncShutdownTelemetryCollector();
		}
		if (wo->adminPanelConnector != NULL) {
			wo->adminPanelConnector->asyncShutdown();
		}

		UPDATE_TRACE_POINT();
		FD_ZERO(&fds);
		FD_SET(wo->allClientsDisconnectedEvent.fd(), &fds);
		if (syscalls::select(wo->allClientsDisconnectedEvent.fd() + 1,
			&fds, NULL, NULL, NULL) == -1)
		{
			int e = errno;
			uninstallAbortHandlerCustomDiagnostics();
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

	uninstallAbortHandlerCustomDiagnostics();

	for (unsigned i = 0; i < wo->threadWorkingObjects.size(); i++) {
		ThreadWorkingObjects *two = &wo->threadWorkingObjects[i];
		two->bgloop->stop();
	}
	if (wo->apiWorkingObjects.apiServer != NULL) {
		wo->apiWorkingObjects.bgloop->stop();
	}
	if (wo->telemetryCollector != NULL
	&& !coreConfig->get("telemetry_collector_disabled").asBool())
	{
		wo->telemetryCollector->runOneCycle(true);
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
	Json::Value pidFile = coreConfig->get("pid_file");
	if (!pidFile.isNull()) {
		syscalls::unlink(pidFile.asCString());
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
		initializeTelemetryCollector();
		initializeAdminPanelConnector();
		prestartWebApps();

		UPDATE_TRACE_POINT();
		warnIfPassengerRootVulnerable();
		reportInitializationInfo();
		initializeAbortHandlerCustomerDiagnostics();
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
parseOptions(int argc, const char *argv[], ConfigKit::Store &config) {
	OptionParser p(coreUsage);
	Json::Value updates(Json::objectValue);
	int i = 2;

	while (i < argc) {
		if (parseCoreOption(argc, argv, i, updates)) {
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

	if (!updates.empty()) {
		vector<ConfigKit::Error> errors;
		if (!config.update(updates, errors)) {
			P_BUG("Unable to set initial configuration: " <<
				ConfigKit::toString(errors) << "\n"
				"Raw initial configuration: " << updates.toStyledString());
		}
	}
}

static void
loggingKitPreInitFunc(Json::Value &loggingKitInitialConfig) {
	loggingKitInitialConfig = manipulateLoggingKitConfig(*coreConfig,
		loggingKitInitialConfig);
}

int
coreMain(int argc, char *argv[]) {
	int ret;

	coreWrapperRegistry = new WrapperRegistry::Registry();
	coreWrapperRegistry->finalize();
	coreSchema = new Schema(coreWrapperRegistry);
	coreConfig = new ConfigKit::Store(*coreSchema);
	initializeAgent(argc, &argv, SHORT_PROGRAM_NAME " core",
		*coreConfig, coreSchema->loggingKit.translator,
		parseOptions, loggingKitPreInitFunc, 2);

#if !BOOST_OS_MACOS
	restoreOomScore(coreConfig->get("oom_score").asString());
#endif

	ret = runCore();
	shutdownAgent(coreSchema, coreConfig);
	delete coreWrapperRegistry;
	return ret;
}
