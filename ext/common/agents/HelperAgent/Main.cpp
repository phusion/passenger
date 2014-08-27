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

#include <agents/HelperAgent/RequestHandler2.h>
#include <agents/HelperAgent/AgentOptions.h>

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
#include <MultiLibeio.cpp>
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

static VariantMap *agentOptions;


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

static struct ev_loop *eioLoop;
static ev_idle eioRepeatWatcher;
static ev_async eioReadyWatcher;

/**
 * A representation of the Server responsible for handling Client instances.
 *
 * @see Client
 */
class Server {
private:
	static const int MESSAGE_SERVER_THREAD_STACK_SIZE = 128 * 1024;
	static const int EVENT_LOOP_THREAD_STACK_SIZE = 256 * 1024;

	FileDescriptor feedbackFd;
	const AgentOptions &options;

	BackgroundEventLoop poolLoop;
	BackgroundEventLoop requestLoop;

	FileDescriptor requestSocket;
	ServerInstanceDir serverInstanceDir;
	ServerInstanceDir::GenerationPtr generation;
	UnionStation::CorePtr unionStationCore;
	RandomGeneratorPtr randomGenerator;
	SpawnerConfigPtr spawnerConfig;
	SpawnerFactoryPtr spawnerFactory;
	PoolPtr pool;
	ev::sig sigquitWatcher;
	AccountsDatabasePtr accountsDatabase;
	MessageServerPtr messageServer;
	ResourceLocator resourceLocator;
	ServerKit::Context *context;
	boost::shared_ptr<RequestHandler> requestHandler;
	boost::shared_ptr<oxt::thread> prestarterThread;
	boost::shared_ptr<oxt::thread> messageServerThread;
	boost::shared_ptr<oxt::thread> eventLoopThread;
	EventFd exitEvent;

	static void
	eioRepeat(EV_P_ ev_idle *w, int revents) {
		if (eio_poll() != -1) {
			ev_idle_stop(EV_A_ w);
		}
	}

	/* eio has some results, process them */
	static void
	eioReady(EV_P_ ev_async *w, int revents) {
		if (eio_poll() == -1) {
	 		ev_idle_start(EV_A_ &eioRepeatWatcher);
	 	}
	}

	/* wake up the event loop */
	static void
	eioWantPoll() {
		ev_async_send(eioLoop, &eioReadyWatcher);
	}

	/**
	 * Starts listening for client connections on this server's request socket.
	 *
	 * @throws SystemException Something went wrong while trying to create and bind to the Unix socket.
	 * @throws RuntimeException Something went wrong.
	 */
	void startListening() {
		this_thread::disable_syscall_interruption dsi;
		requestSocket = createUnixServer(getRequestSocketFilename().c_str());

		int ret, e;
		do {
			ret = chmod(getRequestSocketFilename().c_str(), S_ISVTX |
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IWGRP | S_IXGRP |
				S_IROTH | S_IWOTH | S_IXOTH);
		} while (ret == -1 && errno == EINTR);

		setNonBlocking(requestSocket);

		if (!options.requestSocketLink.empty()) {
			struct stat buf;

			// If this is a symlink then we'll want to check the file the symlink
			// points to, so we use stat() instead of lstat().
			ret = syscalls::stat(options.requestSocketLink.c_str(), &buf);
			if (ret == 0 || (ret == -1 && errno == ENOENT)) {
				if (ret == -1 || buf.st_mode & S_IFSOCK) {
					if (syscalls::unlink(options.requestSocketLink.c_str()) == -1) {
						e = errno;
						throw FileSystemException("Cannot delete existing socket file '" +
							options.requestSocketLink + "'", e, options.requestSocketLink);
					}
				} else {
					throw RuntimeException("File '" + options.requestSocketLink +
						"' already exists and is not a Unix domain socket");
				}
			} else if (ret == -1 && errno != ENOENT) {
				e = errno;
				throw FileSystemException("Cannot stat() file '" + options.requestSocketLink + "'",
					e,
					options.requestSocketLink);
			}

			do {
				ret = symlink(getRequestSocketFilename().c_str(),
					options.requestSocketLink.c_str());
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				e = errno;
				throw FileSystemException("Cannot create a symlink '" +
					options.requestSocketLink +
					"' to '" + getRequestSocketFilename() + "'",
					e,
					options.requestSocketLink);
			}
		}
	}

	/**
	 * Lowers this process's privilege to that of <em>username</em> and <em>groupname</em>.
	 */
	void lowerPrivilege(const string &username, const string &groupname) {
		struct passwd *userEntry;
		gid_t gid;
		int e;

		userEntry = getpwnam(username.c_str());
		if (userEntry == NULL) {
			throw NonExistentUserException(string("Unable to lower Passenger "
				"HelperAgent's privilege to that of user '") + username +
				"': user does not exist.");
		}
		gid = lookupGid(groupname);
		if (gid == (gid_t) -1) {
			throw NonExistentGroupException(string("Unable to lower Passenger "
				"HelperAgent's privilege to that of user '") + username +
				"': user does not exist.");
		}

		if (initgroups(username.c_str(), userEntry->pw_gid) != 0) {
			e = errno;
			throw SystemException(string("Unable to lower Passenger HelperAgent's "
				"privilege to that of user '") + username +
				"': cannot set supplementary groups for this user", e);
		}
		if (setgid(gid) != 0) {
			e = errno;
			throw SystemException(string("Unable to lower Passenger HelperAgent's "
				"privilege to that of user '") + username +
				"': cannot set group ID", e);
		}
		if (setuid(userEntry->pw_uid) != 0) {
			e = errno;
			throw SystemException(string("Unable to lower Passenger HelperAgent's "
				"privilege to that of user '") + username +
				"': cannot set user ID", e);
		}

		setenv("HOME", userEntry->pw_dir, 1);
	}

	void onSigquit(ev::sig &signal, int revents) {
		//requestHandler->inspect(cerr);
		cerr.flush();
		cerr << "\n" << pool->inspect();
		cerr.flush();
		cerr << "\n" << oxt::thread::all_backtraces();
		cerr.flush();
	}

	void installDiagnosticsDumper() {
		::installDiagnosticsDumper(dumpDiagnosticsOnCrash, this);
	}

	void uninstallDiagnosticsDumper() {
		::installDiagnosticsDumper(NULL, NULL);
	}

	static void dumpDiagnosticsOnCrash(void *userData) {
		Server *self = (Server *) userData;

		cerr << "### Request handler state\n";
		//self->requestHandler->inspect(cerr);
		cerr << "\n";
		cerr.flush();

		cerr << "### Pool state (simple)\n";
		// Do not lock, the crash may occur within the pool.
		Pool::InspectOptions options;
		options.verbose = true;
		cerr << self->pool->inspect(options, false);
		cerr << "\n";
		cerr.flush();

		cerr << "### Pool state (XML)\n";
		cerr << self->pool->toXml(true, false);
		cerr << "\n\n";
		cerr.flush();

		cerr << "### Backtraces\n";
		cerr << oxt::thread::all_backtraces();
		cerr.flush();
	}

public:
	Server(FileDescriptor feedbackFd, const AgentOptions &_options)
		: options(_options),
		  requestLoop(true),
		  serverInstanceDir(_options.serverInstanceDir, false),
		  resourceLocator(options.passengerRoot)
	{
		TRACE_POINT();
		this->feedbackFd = feedbackFd;

		UPDATE_TRACE_POINT();
		generation = serverInstanceDir.getGeneration(options.generationNumber);
		startListening();
		accountsDatabase = boost::make_shared<AccountsDatabase>();
		accountsDatabase->add("_passenger-status", options.adminToolStatusPassword, false,
			Account::INSPECT_BASIC_INFO | Account::INSPECT_SENSITIVE_INFO |
			Account::INSPECT_BACKTRACES | Account::INSPECT_REQUESTS |
			Account::DETACH | Account::RESTART);
		accountsDatabase->add("_web_server", options.exitPassword, false, Account::EXIT);
		messageServer = boost::make_shared<MessageServer>(
			parseUnixSocketAddress(options.adminSocketAddress), accountsDatabase);

		createFile(generation->getPath() + "/helper_agent.pid",
			toString(getpid()), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		if (geteuid() == 0 && !options.userSwitching) {
			lowerPrivilege(options.defaultUser, options.defaultGroup);
		}

		UPDATE_TRACE_POINT();
		randomGenerator = boost::make_shared<RandomGenerator>();
		// Check whether /dev/urandom is actually random.
		// https://code.google.com/p/phusion-passenger/issues/detail?id=516
		if (randomGenerator->generateByteString(16) == randomGenerator->generateByteString(16)) {
			throw RuntimeException("Your random number device, /dev/urandom, appears to be broken. "
				"It doesn't seem to be returning random data. Please fix this.");
		}

		UPDATE_TRACE_POINT();
		unionStationCore = boost::make_shared<UnionStation::Core>(options.loggingAgentAddress,
			"logging", options.loggingAgentPassword);
		spawnerConfig = boost::make_shared<SpawnerConfig>();
		spawnerConfig->resourceLocator = &resourceLocator;
		spawnerConfig->unionStationCore = unionStationCore;
		spawnerConfig->randomGenerator = randomGenerator;
		spawnerConfig->agentsOptions = &options;
		spawnerConfig->finalize();
		spawnerFactory = boost::make_shared<SpawnerFactory>(spawnerConfig);
		pool = boost::make_shared<Pool>(spawnerFactory, &options);
		pool->initialize();
		pool->setMax(options.maxPoolSize);
		pool->setMaxIdleTime(options.poolIdleTime * 1000000);

		context = new ServerKit::Context(requestLoop.safe);

		requestHandler = boost::make_shared<RequestHandler>(context, agentOptions);
		requestHandler->resourceLocator = &resourceLocator;
		requestHandler->appPool = pool;
		requestHandler->initialize();
		requestHandler->createSpareClients();
		// TODOs
		//requestHandler->listen(requestSocket);
		requestHandler->listen(createTcpServer("127.0.0.1", 3000));

		messageServer->addHandler(boost::make_shared<RemoteController>(requestHandler, pool));
		messageServer->addHandler(ptr(new ExitHandler(exitEvent)));

		eioLoop = requestLoop.loop;
		ev_idle_init(&eioRepeatWatcher, eioRepeat);
		ev_async_init(&eioReadyWatcher, eioReady);
		ev_async_start(requestLoop.loop, &eioReadyWatcher);
		eio_init(eioWantPoll, 0);

		sigquitWatcher.set(requestLoop.loop);
		sigquitWatcher.set(SIGQUIT);
		sigquitWatcher.set<Server, &Server::onSigquit>(this);
		sigquitWatcher.start();

		UPDATE_TRACE_POINT();
		writeArrayMessage(feedbackFd,
			"initialized",
			getRequestSocketFilename().c_str(),
			messageServer->getSocketFilename().c_str(),
			NULL);

		boost::function<void ()> func = boost::bind(prestartWebApps,
			resourceLocator,
			options.defaultRubyCommand,
			options.prestartUrls
		);
		prestarterThread = ptr(new oxt::thread(
			boost::bind(runAndPrintExceptions, func, true)
		));
	}

	~Server() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;

		P_DEBUG("Shutting down helper agent...");
		prestarterThread->interrupt_and_join();
		if (messageServerThread != NULL) {
			messageServerThread->interrupt_and_join();
		}

		messageServer.reset();
		P_DEBUG("Destroying application pool...");
		pool->destroy();
		uninstallDiagnosticsDumper();
		pool.reset();
		poolLoop.stop();
		requestLoop.stop();
		requestHandler.reset();

		if (!options.requestSocketLink.empty()) {
			char path[PATH_MAX + 1];
			ssize_t ret;
			bool shouldUnlink;

			ret = readlink(options.requestSocketLink.c_str(), path, PATH_MAX);
			if (ret != -1) {
				path[ret] = '\0';
				// Only unlink if a new Flying Passenger instance hasn't overwritten the
				// symlink.
				// https://code.google.com/p/phusion-passenger/issues/detail?id=939
				shouldUnlink = getRequestSocketFilename() == path;
			} else {
				shouldUnlink = true;
			}

			if (shouldUnlink) {
				syscalls::unlink(options.requestSocketLink.c_str());
			}
		}

		P_TRACE(2, "All threads have been shut down.");
	}

	void mainLoop() {
		TRACE_POINT();
		boost::function<void ()> func;

		func = boost::bind(&MessageServer::mainLoop, messageServer.get());
		messageServerThread = ptr(new oxt::thread(
			boost::bind(runAndPrintExceptions, func, true),
			"MessageServer thread", MESSAGE_SERVER_THREAD_STACK_SIZE
		));

		poolLoop.start("Pool event loop", 0);
		requestLoop.start("Request event loop", 0);


		/* Wait until the watchdog closes the feedback fd (meaning it
		 * was killed) or until we receive an exit message.
		 */
		this_thread::disable_syscall_interruption dsi;
		fd_set fds;
		int largestFd;

		FD_ZERO(&fds);
		// TODO:
		//FD_SET(feedbackFd, &fds);
		FD_SET(exitEvent.fd(), &fds);
		largestFd = (feedbackFd > exitEvent.fd()) ? (int) feedbackFd : exitEvent.fd();
		UPDATE_TRACE_POINT();
		installDiagnosticsDumper();
		if (syscalls::select(largestFd + 1, &fds, NULL, NULL, NULL) == -1) {
			int e = errno;
			uninstallDiagnosticsDumper();
			throw SystemException("select() failed", e);
		}

		if (FD_ISSET(feedbackFd, &fds)) {
			/* If the watchdog has been killed then we'll kill all descendant
			 * processes and exit. There's no point in keeping this helper
			 * server running because we can't detect when the web server exits,
			 * and because this helper agent doesn't own the server instance
			 * directory. As soon as passenger-status is run, the server
			 * instance directory will be cleaned up, making this helper agent
			 * inaccessible.
			 */
			P_DEBUG("Watchdog seems to be killed; forcing shutdown of all subprocesses");
			// We send a SIGTERM first to allow processes to gracefully shut down.
			syscalls::killpg(getpgrp(), SIGTERM);
			usleep(500000);
			syscalls::killpg(getpgrp(), SIGKILL);
			_exit(2); // In case killpg() fails.
		} else {
			/* We received an exit command. We want to exit 5 seconds after
			 * all clients have disconnected have become inactive.
			 */
			P_DEBUG("Received command to exit gracefully. "
				"Waiting until 5 seconds after all clients have disconnected...");
			pool->prepareForShutdown();
			// TODO
			/* requestHandler->resetInactivityTime();
			while (requestHandler->inactivityTime() < 5000) {
				syscalls::usleep(250000);
			} */
			P_DEBUG("It's now 5 seconds after all clients have disconnected. "
				"Proceeding with graceful exit.");
		}
	}

	string getRequestSocketFilename() const {
		return options.requestSocketFilename;
	}
};

int
oldMain(int argc, char *argv[]) {
	TRACE_POINT();

	AgentOptionsPtr options;
	try {
		options = boost::make_shared<AgentOptions>(
			initializeAgent(argc, &argv, "PassengerHelperAgent"));
	} catch (const VariantMap::MissingKeyException &e) {
		fprintf(stderr, "Option required: %s\n", e.getKey().c_str());
		return 1;
	}
	if (options->testBinary) {
		printf("PASS\n");
		exit(0);
	}

	P_DEBUG("Starting PassengerHelperAgent...");

	try {
		UPDATE_TRACE_POINT();
		Server server(FileDescriptor(FEEDBACK_FD), *options);
		P_WARN("PassengerHelperAgent online, listening at unix:" <<
			server.getRequestSocketFilename());

		UPDATE_TRACE_POINT();
		server.mainLoop();
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}

	P_TRACE(2, "Helper agent exiting with code 0.");
	return 0;
}

/***** Structures, constants, global variables and forward declarations *****/

#define DEFAULT_LISTEN_ADDRESS "tcp://127.0.0.1:3000"

struct WorkingObjects {
	int serverFds[ServerKit::Server<>::MAX_ENDPOINTS];
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

static void usage();


/***** Server stuff *****/

static void
initializePrivilegedWorkingObjects() {
	TRACE_POINT();
	workingObjects = new WorkingObjects();
}

static void
initializeSingleAppMode(WorkingObjects *wo) {
	TRACE_POINT();
	VariantMap &options = *agentOptions;

	if (options.getBool("multi_app")) {
		P_NOTICE("PassengerAgent server running in multi-application mode.");
		return;
	}

	if (!options.has("app_type")) {
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
	vector<string> addresses = agentOptions->getStrSet("server_listen_addresses");

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

	cerr << "### Backtraces\n";
	cerr << "\n" << oxt::thread::all_backtraces();
	cerr << "\n";
	cerr.flush();
}

static void
initializeNonPrivilegedWorkingObjects(WorkingObjects *wo) {
	TRACE_POINT();
	VariantMap &options = *agentOptions;
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
	wo->spawnerConfig->agentsOptions = agentOptions;
	wo->spawnerConfig->randomGenerator = wo->randomGenerator;
	wo->spawnerConfig->finalize();

	UPDATE_TRACE_POINT();
	wo->spawnerFactory = boost::make_shared<SpawnerFactory>(wo->spawnerConfig);

	wo->bgloop = new BackgroundEventLoop(true, true);
	ev_signal_init(&wo->sigquitWatcher, onSigquit, SIGQUIT);
	ev_signal_start(wo->bgloop->loop, &wo->sigquitWatcher);

	wo->appPool = boost::make_shared<Pool>(wo->spawnerFactory, agentOptions);
	wo->appPool->initialize();
	wo->appPool->setMax(options.getInt("max_pool_size"));
	wo->appPool->setMaxIdleTime(options.getInt("pool_idle_time") * 1000000);

	UPDATE_TRACE_POINT();
	wo->serverKitContext = new ServerKit::Context(wo->bgloop->safe);
	wo->requestHandler = new RequestHandler(wo->serverKitContext, agentOptions);
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
	vector<string> addresses = agentOptions->getStrSet("server_listen_addresses");
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
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}

	P_TRACE(2, "PassengerAgent server exiting with code 0.");
	return 0;
}


/***** Entry point and command line argument parsing *****/

static bool
isFlag(const char *arg, char shortFlagName, const char *longFlagName) {
	return strcmp(arg, longFlagName) == 0
		|| (shortFlagName != '\0' && arg[0] == '-'
			&& arg[1] == shortFlagName && arg[2] == '\0');
}

static bool
isValueFlag(int argc, int i, const char *arg, char shortFlagName, const char *longFlagName) {
	if (isFlag(arg, shortFlagName, longFlagName)) {
		if (argc >= i + 2) {
			return true;
		} else {
			fprintf(stderr, "ERROR: extra argument required for %s\n", arg);
			usage();
			exit(1);
			return false; // Never reached
		}
	} else {
		return false;
	}
}

static void
usage() {
	printf("Usage: PassengerAgent server <OPTIONS...> [APP DIRECTORY]\n");
	printf("Runs the " PROGRAM_NAME " standalone HTTP server.\n");
	printf("\n");
	printf("The server starts in single-app mode, unless --multi-app is specified. When\n");
	printf("in single-app mode, it serves the app at the current working directory, or the\n");
	printf("app specified by APP DIRECTORY.\n");
	printf("\n");
	printf("Required options:\n");
	printf("  --passenger-root PATH     The location to the " PROGRAM_NAME " source\n");
	printf("                            directory\n");
	printf("\n");
	printf("Socket options (optional):\n");
	printf("  -l,  --listen ADDRESS     Listen on the given address. The address must be\n");
	printf("                            formatted as tcp://IP:PORT for TCP sockets, or\n");
	printf("                            unix:PATH for Unix domain sockets. You can specify\n");
	printf("                            this option multiple times (up to %u times) to\n",
		ServerKit::Server<>::MAX_ENDPOINTS);
	printf("                            listen on multiple addresses. Default:\n");
	printf("                            " DEFAULT_LISTEN_ADDRESS "\n");
	printf("\n");
	printf("Application serving options (optional):\n");
	printf("      --app-type TYPE       The type of application you want to serve\n");
	printf("                            (single-app mode only)\n");
	printf("      --startup-file PATH   The path of the app's startup file, relative to\n");
	printf("                            the app root directory (single-app mode only)\n");
	printf("\n");
	printf("      --multi-app           Enable multi-app mode\n");
	printf("\n");
	printf("Process management options (optional):\n");
	printf("      --max-pool-size N     Maximum number of application processes.\n");
	printf("                            Default: %d\n", DEFAULT_MAX_POOL_SIZE);
	printf("\n");
	printf("Other options (optional):\n");
	printf("      --log-level LEVEL     Logging level. Default: %d\n", DEFAULT_LOG_LEVEL);
	printf("  -h, --help                Show this help\n");
}

static void
parseOptions(int argc, const char *argv[], VariantMap &options) {
	int i = 2;

	while (i < argc) {
		if (isValueFlag(argc, i, argv[i], '\0', "--passenger-root")) {
			options.set("passenger_root", argv[i + 1]);
			i += 2;
		} else if (isValueFlag(argc, i, argv[i], 'l', "--listen")) {
			if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
				vector<string> addresses = options.getStrSet("listen", false);
				if (addresses.size() == ServerKit::Server<>::MAX_ENDPOINTS) {
					fprintf(stderr, "ERROR: you may specify up to %u --listen addresses.\n",
						ServerKit::Server<>::MAX_ENDPOINTS);
					exit(1);
				}
				addresses.push_back(argv[i + 1]);
				options.setStrSet("server_listen_addresses", addresses);
				i += 2;
			} else {
				fprintf(stderr, "ERROR: invalid address format for --listen. The address "
					"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
					"for Unix domain sockets.\n");
				exit(1);
			}
		} else if (isValueFlag(argc, i, argv[i], '\0', "--max-pool-size")) {
			options.setInt("max_pool_size", atoi(argv[i + 1]));
			i += 2;
		} else if (isValueFlag(argc, i, argv[i], '\0', "--app-type")) {
			options.set("app_type", argv[i + 1]);
			i += 2;
		} else if (isValueFlag(argc, i, argv[i], '\0', "--startup-file")) {
			options.set("startup_file", argv[i + 1]);
			i += 2;
		} else if (isFlag(argv[i], '\0', "--multi-app")) {
			options.setBool("multi_app", true);
			i++;
		} else if (isValueFlag(argc, i, argv[i], '\0', "--log-level")) {
			options.setInt("log_level", atoi(argv[i + 1]));
			i += 2;
		} else if (isFlag(argv[i], 'h', "--help")) {
			usage();
			exit(0);
		} else if (startsWith(argv[i], "-")) {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s server --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		} else {
			if (!options.has("app_root")) {
				options.set("app_root", argv[i]);
				i++;
			} else {
				fprintf(stderr, "ERROR: you may not pass multiple application directories. "
					"Please type '%s server --help' for usage.\n", argv[0]);
				exit(1);
			}
		}
	}
}

static void
setAgentOptionsDefaults() {
	VariantMap &options = *agentOptions;
	set<string> defaultListenAddress;
	defaultListenAddress.insert(DEFAULT_LISTEN_ADDRESS);

	options.setDefaultStrSet("server_listen_addresses", defaultListenAddress);
	options.setDefaultBool("multi_app", false);
	options.setDefaultInt("max_pool_size", DEFAULT_MAX_POOL_SIZE);
	options.setDefaultInt("pool_idle_time", DEFAULT_POOL_IDLE_TIME);

	options.setDefault("default_ruby", DEFAULT_RUBY);
	if (!options.getBool("multi_app") && !options.has("app_root")) {
		char *pwd = getcwd(NULL, 0);
		options.set("app_root", pwd);
		free(pwd);
	}
}

static void
sanityCheckOptions() {
	VariantMap &options = *agentOptions;
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
	agentOptions = new VariantMap();
	*agentOptions = initializeAgent(argc, &argv, "PassengerHelperAgent", parseOptions, 2);
	setAgentOptionsDefaults();
	sanityCheckOptions();
	return runServer();
}
