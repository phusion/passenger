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
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <string>
#include <utility>
#include <vector>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <agents/Base.h>
#include <agents/HelperAgent/OptionParser.h>
#include <Constants.h>
#include <InstanceDirectory.h>
#include <ServerInstanceDir.h>
#include <FileDescriptor.h>
#include <RandomGenerator.h>
#include <Logging.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <Hooks.h>
#include <ResourceLocator.h>
#include <Utils.h>
#include <Utils/Base64.h>
#include <Utils/Timer.h>
#include <Utils/ScopeGuard.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/OptionParsing.h>
#include <Utils/VariantMap.h>

using namespace std;
using namespace boost;
using namespace oxt;
using namespace Passenger;


enum OomFileType {
	OOM_ADJ,
	OOM_SCORE_ADJ
};

#define REQUEST_SOCKET_PASSWORD_SIZE     64

class ServerInstanceDirToucher;
class AgentWatcher;
static void setOomScore(const StaticString &score);


/***** Agent options *****/

static VariantMap *agentsOptions;

/***** Working objects *****/

struct WorkingObjects {
	RandomGenerator randomGenerator;
	EventFd errorEvent;
	ResourceLocatorPtr resourceLocator;
	uid_t defaultUid;
	gid_t defaultGid;
	InstanceDirectoryPtr instanceDir;
	vector<string> cleanupPidfiles;
	string loggingAgentAddress;
	string loggingAgentPassword;
	string loggingAgentAdminAddress;
	string adminToolStatusPassword;
	string adminToolManipulationPassword;
};

typedef boost::shared_ptr<WorkingObjects> WorkingObjectsPtr;

static string oldOomScore;

#include "AgentWatcher.cpp"
#include "ServerInstanceDirToucher.cpp"
#include "HelperAgentWatcher.cpp"
#include "LoggingAgentWatcher.cpp"


/***** Functions *****/

static FILE *
openOomAdjFile(const char *mode, OomFileType &type) {
	FILE *f = fopen("/proc/self/oom_score_adj", mode);
	if (f == NULL) {
		f = fopen("/proc/self/oom_adj", mode);
		if (f == NULL) {
			return NULL;
		} else {
			type = OOM_ADJ;
			return f;
		}
	} else {
		type = OOM_SCORE_ADJ;
		return f;
	}
}

/**
 * Linux-only way to change OOM killer configuration for
 * current process. Requires root privileges, which we
 * should have.
 */
static void
setOomScore(const StaticString &score) {
	if (score.empty()) {
		return;
	}

	FILE *f;
	OomFileType type;

	f = openOomAdjFile("w", type);
	if (f != NULL) {
		size_t ret = fwrite(score.data(), 1, score.size(), f);
		// We can't do anything about failures, so ignore compiler
		// warnings about not doing anything with the result.
		(void) ret;
		fclose(f);
	}
}

/**
 * Set the current process's OOM score to "never kill".
 */
static string
setOomScoreNeverKill() {
	string oldScore;
	FILE *f;
	OomFileType type;

	f = openOomAdjFile("r", type);
	if (f == NULL) {
		return "";
	}
	char buf[1024];
	size_t bytesRead;
	while (true) {
		bytesRead = fread(buf, 1, sizeof(buf), f);
		if (bytesRead == 0 && feof(f)) {
			break;
		} else if (bytesRead == 0 && ferror(f)) {
			fclose(f);
			return "";
		} else {
			oldScore.append(buf, bytesRead);
		}
	}
	fclose(f);

	f = openOomAdjFile("w", type);
	if (f == NULL) {
		return "";
	}
	if (type == OOM_SCORE_ADJ) {
		fprintf(f, "-1000\n");
	} else {
		assert(type == OOM_ADJ);
		fprintf(f, "-17\n");
	}
	fclose(f);

	return oldScore;
}

/**
 * Wait until the starter process has exited or sent us an exit command,
 * or until one of the watcher threads encounter an error. If a thread
 * encountered an error then the error message will be printed.
 *
 * Returns whether this watchdog should exit gracefully, which is only the
 * case if the web server sent us an exit command and no thread encountered
 * an error.
 */
static bool
waitForStarterProcessOrWatchers(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers) {
	fd_set fds;
	int max, ret;
	char x;

	FD_ZERO(&fds);
	FD_SET(FEEDBACK_FD, &fds);
	FD_SET(wo->errorEvent.fd(), &fds);

	if (FEEDBACK_FD > wo->errorEvent.fd()) {
		max = FEEDBACK_FD;
	} else {
		max = wo->errorEvent.fd();
	}

	ret = syscalls::select(max + 1, &fds, NULL, NULL, NULL);
	if (ret == -1) {
		int e = errno;
		P_ERROR("select() failed: " << strerror(e));
		return false;
	}

	if (FD_ISSET(wo->errorEvent.fd(), &fds)) {
		vector<AgentWatcherPtr>::const_iterator it;
		string message, backtrace, watcherName;

		for (it = watchers.begin(); it != watchers.end() && message.empty(); it++) {
			message   = (*it)->getErrorMessage();
			backtrace = (*it)->getErrorBacktrace();
			watcherName = (*it)->name();
		}

		if (!message.empty() && backtrace.empty()) {
			P_ERROR("Error in " << watcherName << " watcher:\n  " << message);
		} else if (!message.empty() && !backtrace.empty()) {
			P_ERROR("Error in " << watcherName << " watcher:\n  " <<
				message << "\n" << backtrace);
		}
		return false;
	} else {
		ret = syscalls::read(FEEDBACK_FD, &x, 1);
		return ret == 1 && x == 'c';
	}
}

static vector<pid_t>
readCleanupPids(const WorkingObjectsPtr &wo) {
	vector<pid_t> result;

	foreach (string filename, wo->cleanupPidfiles) {
		FILE *f = fopen(filename.c_str(), "r");
		if (f != NULL) {
			char buf[33];
			size_t ret;

			ret = fread(buf, 1, 32, f);
			if (ret > 0) {
				buf[ret] = '\0';
				result.push_back(atoi(buf));
			} else {
				P_WARN("Cannot read cleanup PID file " << filename);
			}
		} else {
			P_WARN("Cannot open cleanup PID file " << filename);
		}
	}

	return result;
}

static void
killCleanupPids(const vector<pid_t> &cleanupPids) {
	foreach (pid_t pid, cleanupPids) {
		P_DEBUG("Sending SIGTERM to cleanup PID " << pid);
		kill(pid, SIGTERM);
	}
}

static void
killCleanupPids(const WorkingObjectsPtr &wo) {
	killCleanupPids(readCleanupPids(wo));
}

static void
cleanupAgentsInBackground(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers, char *argv[]) {
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	vector<pid_t> cleanupPids;
	pid_t pid;
	int e;

	cleanupPids = readCleanupPids(wo);

	pid = fork();
	if (pid == 0) {
		// Child
		try {
			vector<AgentWatcherPtr>::const_iterator it;
			Timer timer(false);
			fd_set fds, fds2;
			int max, agentProcessesDone;
			unsigned long long deadline = 30000; // miliseconds

			#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(sun)
				// Change process title.
				strcpy(argv[0], "PassengerWatchdog (cleaning up...)");
			#endif

			// Wait until all agent processes have exited. The starter
			// process is responsible for telling the individual agents
			// to exit.

			max = 0;
			FD_ZERO(&fds);
			for (it = watchers.begin(); it != watchers.end(); it++) {
				FD_SET((*it)->getFeedbackFd(), &fds);
				if ((*it)->getFeedbackFd() > max) {
					max = (*it)->getFeedbackFd();
				}
			}

			timer.start();
			agentProcessesDone = 0;
			while (agentProcessesDone != -1
			    && agentProcessesDone < (int) watchers.size()
			    && timer.elapsed() < deadline)
			{
				struct timeval timeout;

				#ifdef FD_COPY
					FD_COPY(&fds, &fds2);
				#else
					FD_ZERO(&fds2);
					for (it = watchers.begin(); it != watchers.end(); it++) {
						FD_SET((*it)->getFeedbackFd(), &fds2);
					}
				#endif

				timeout.tv_sec = 0;
				timeout.tv_usec = 10000;
				agentProcessesDone = syscalls::select(max + 1, &fds2, NULL, NULL, &timeout);
				if (agentProcessesDone > 0 && timer.elapsed() < deadline) {
					usleep(10000);
				}
			}

			if (agentProcessesDone == -1 || timer.elapsed() >= deadline) {
				// An error occurred or we've waited long enough. Kill all the
				// processes.
				P_WARN("Some Phusion Passenger agent processes did not exit " <<
					"in time, forcefully shutting down all.");
			} else {
				P_DEBUG("All Phusion Passenger agent processes have exited. Forcing all subprocesses to shut down.");
			}
			P_DEBUG("Sending SIGTERM");
			for (it = watchers.begin(); it != watchers.end(); it++) {
				(*it)->signalShutdown();
			}
			usleep(1000000);
			P_DEBUG("Sending SIGKILL");
			for (it = watchers.begin(); it != watchers.end(); it++) {
				(*it)->forceShutdown();
			}

			// Now clean up the instance directory.
			wo->instanceDir->destroy();

			// Notify given PIDs about our shutdown.
			killCleanupPids(cleanupPids);

			strcpy(argv[0], "PassengerWatchdog (cleaning up 6...)");
			_exit(0);
		} catch (const std::exception &e) {
			P_CRITICAL("An exception occurred during cleaning up: " << e.what());
			_exit(1);
		} catch (...) {
			P_CRITICAL("An unknown exception occurred during cleaning up");
			_exit(1);
		}

	} else if (pid == -1) {
		// Error
		e = errno;
		throw SystemException("fork() failed", e);

	} else {
		// Parent

		// Let child process handle cleanup.
		wo->instanceDir->detach();
	}
}

static void
forceAllAgentsShutdown(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers) {
	vector<AgentWatcherPtr>::iterator it;

	for (it = watchers.begin(); it != watchers.end(); it++) {
		(*it)->signalShutdown();
	}
	usleep(1000000);
	for (it = watchers.begin(); it != watchers.end(); it++) {
		(*it)->forceShutdown();
	}
	killCleanupPids(wo);
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
runHookScriptAndThrowOnError(const char *name) {
	TRACE_POINT();
	HookScriptOptions options;

	options.name = name;
	options.spec = agentsOptions->get(string("hook_") + name, false);
	options.agentsOptions = agentsOptions;

	if (!runHookScripts(options)) {
		throw RuntimeException(string("Hook script ") + name + " failed");
	}
}


static void
usage() {
	printf("Usage: PassengerAgent watchdog <OPTIONS...>\n");
	printf("Runs the " PROGRAM_NAME " watchdog.\n\n");
	printf("The watchdog runs and supervises various " PROGRAM_NAME " agent processes,\n");
	printf("namely the HTTP server and logging server. Arguments marked with \"[A]\", e.g.\n");
	printf("--passenger-root and --log-level, are automatically passed to all supervised\n");
	printf("agents, unless you explicitly override them by passing extra arguments to a\n");
	printf("supervised agent specifically. You can pass arguments to a supervised agent by\n");
	printf("wrapping those arguments between --BS/--ES and --BL/--EL.\n");
	printf("\n");
	printf("  Example 1: pass some arguments to the HTTP server.\n\n");
	printf("  PassengerAgent watchdog --passenger-root /opt/passenger \\\n");
	printf("    --BS --listen tcp://127.0.0.1:4000 /webapps/foo\n");
	printf("\n");
	printf("  Example 2: pass some arguments to the HTTP server, and some others to the\n");
	printf("  logging server. The watchdog itself and the HTTP server will use logging\n");
	printf("  level 3, while the logging server will use logging level 1.\n\n");
	printf("  PassengerAgent watchdog --passenger-root /opt/passenger \\\n");
	printf("    --BS --listen tcp://127.0.0.1:4000 /webapps/foo --ES \\\n");
	printf("    --BL --log-level 1 --EL \\\n");
	printf("    --log-level 3\n");
	printf("\n");
	printf("Required options:\n");
	printf("       --passenger-root PATH  The location to the " PROGRAM_NAME " source\n");
	printf("                              directory [A]\n");
	printf("\n");
	printf("Argument passing options (optional):\n");
	printf("  --BS, --begin-server-args   Signals the beginning of arguments to pass to the\n");
	printf("                              HTTP server\n");
	printf("  --ES, --end-server-args     Signals the end of arguments to pass to the HTTP\n");
	printf("                              server\n");
	printf("  --BL, --begin-logging-args  Signals the beginning of arguments to pass to the\n");
	printf("                              logging server\n");
	printf("  --EL, --end-logging-args    Signals the end of arguments to pass to the\n");
	printf("                              logging server\n");
	printf("\n");
	printf("Other options (optional):\n");
	printf("      --no-register-instance   Do not register this watchdog instance\n");
	printf("      --instance-registry-dir  Directory to register instance into.\n");
	printf("                               Default: %s\n", getSystemTempDir());
	printf("\n");
	printf("      --no-user-switching     Disables user switching support [A]\n");
	printf("      --default-user NAME     Default user to start apps as, when user\n");
	printf("                              switching is enabled. Default: " DEFAULT_WEB_APP_USER "\n");
	printf("      --default-group NAME    Default group to start apps as, when user\n");
	printf("                              switching is disabled. Default: the default\n");
	printf("                              user's primary group\n");
	printf("\n");
	printf("      --log-level LEVEL       Logging level. [A] Default: %d\n", DEFAULT_LOG_LEVEL);
	printf("\n");
	printf("  -h, --help                  Show this help\n");
	printf("\n");
	printf("[A] = Automatically passed to supervised agents\n");
}

static void
parseOptions(int argc, const char *argv[], VariantMap &options) {
	OptionParser p(usage);
	int i = 2;

	while (i < argc) {
		if (p.isValueFlag(argc, i, argv[i], '\0', "--passenger-root")) {
			options.set("passenger_root", argv[i + 1]);
			i += 2;
		} else if (p.isFlag(argv[i], '\0', "--BS")
			|| p.isFlag(argv[i], '\0', "--begin-server-args"))
		{
			i++;
			while (i < argc) {
				if (p.isFlag(argv[i], '\0', "--ES")
				 || p.isFlag(argv[i], '\0', "--end-server-args"))
				{
					i++;
					break;
				} else if (!parseServerOption(argc, argv, i, options)) {
					fprintf(stderr, "ERROR: unrecognized HTTP server argument %s. Please "
						"type '%s server --help' for usage.\n", argv[i], argv[0]);
					exit(1);
				}
			}
		} else if (p.isFlag(argv[i], '\0', "--no-register-instance")) {
			options.setBool("register_instance", false);
			i++;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--instance-registry-dir")) {
			options.set("instance_registry_dir", argv[i + 1]);
			i += 2;
		} else if (p.isFlag(argv[i], '\0', "--no-user-switching")) {
			options.setBool("user_switching", false);
			i++;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--default-user")) {
			options.set("default_user", argv[i + 1]);
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--default-group")) {
			options.set("default_group", argv[i + 1]);
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-level")) {
			options.setInt("log_level", atoi(argv[i + 1]));
			i += 2;
		} else if (p.isFlag(argv[i], 'h', "--help")) {
			usage();
			exit(0);
		} else {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s watchdog --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		}
	}
}

static void
initializeBareEssentials(int argc, char *argv[]) {
	/*
	 * Some Apache installations (like on OS X) redirect stdout to /dev/null,
	 * so that only stderr is redirected to the log file. We therefore
	 * forcefully redirect stdout to stderr so that everything ends up in the
	 * same place.
	 */
	dup2(2, 1);

	/*
	 * Most operating systems overcommit memory. We *know* that this watchdog process
	 * doesn't use much memory; on OS X it uses about 200 KB of private RSS. If the
	 * watchdog is killed by the system Out-Of-Memory Killer or then it's all over:
	 * the system administrator will have to restart the web server for Phusion
	 * Passenger to be usable again. So here we disable Linux's OOM killer
	 * for this watchdog. Note that the OOM score is inherited by child processes
	 * so we need to restore it after each fork().
	 */
	oldOomScore = setOomScoreNeverKill();

	agentsOptions = new VariantMap();
	*agentsOptions = initializeAgent(argc, &argv, "PassengerWatchdog",
		parseOptions, 2);
}

static void
setAgentsOptionsDefaults() {
	VariantMap &options = *agentsOptions;

	options.setDefaultBool("register_instance", true);
	options.setDefault("instance_registry_dir", getSystemTempDir());
	options.setDefaultBool("user_switching", true);
	options.setDefault("default_user", DEFAULT_WEB_APP_USER);
	if (!options.has("default_group")) {
		options.set("default_group",
			inferDefaultGroup(options.get("default_user")));
	}
	options.setDefaultStrSet("cleanup_pidfiles", vector<string>());
}

static void
sanityCheckOptions() {
	VariantMap &options = *agentsOptions;

	if (!options.has("passenger_root")) {
		fprintf(stderr, "ERROR: please set the --passenger-root argument.\n");
		exit(1);
	}
}

static void
maybeSetsid() {
	/* Become the session leader so that Apache can't kill the
	 * watchdog with killpg() during shutdown, so that a
	 * Ctrl-C only affects the web server, and so that
	 * we can kill all of our subprocesses in a single killpg().
	 *
	 * AgentsStarter.h already calls setsid() before exec()ing
	 * the Watchdog, but Flying Passenger does not.
	 */
	if (agentsOptions->getBool("setsid", false)) {
		setsid();
	}
}

static void
lookupDefaultUidGid(uid_t &uid, gid_t &gid) {
	VariantMap &options = *agentsOptions;
	const string &defaultUser = options.get("default_user");
	const string &defaultGroup = options.get("default_group");
	struct passwd *userEntry;

	userEntry = getpwnam(defaultUser.c_str());
	if (userEntry == NULL) {
		throw NonExistentUserException("Default user '" + defaultUser +
			"' does not exist.");
	}
	uid = userEntry->pw_uid;

	gid = lookupGid(defaultGroup);
	if (gid == (gid_t) -1) {
		throw NonExistentGroupException("Default group '" + defaultGroup +
			"' does not exist.");
	}
}

static void
initializeWorkingObjects(WorkingObjectsPtr &wo, ServerInstanceDirToucherPtr &serverInstanceDirToucher) {
	TRACE_POINT();
	VariantMap &options = *agentsOptions;

	wo = boost::make_shared<WorkingObjects>();
	wo->resourceLocator = boost::make_shared<ResourceLocator>(agentsOptions->get("passenger_root"));

	UPDATE_TRACE_POINT();
	lookupDefaultUidGid(wo->defaultUid, wo->defaultGid);

	UPDATE_TRACE_POINT();
	if (options.getBool("register_instance")) {
		InstanceDirectory::CreationOptions instanceOptions;
		instanceOptions.userSwitching = options.getBool("user_switching");
		instanceOptions.defaultUid = wo->defaultUid;
		instanceOptions.defaultGid = wo->defaultGid;
		wo->instanceDir = make_shared<InstanceDirectory>(instanceOptions,
			options.get("instance_registry_dir"));
		options.set("instance_dir", wo->instanceDir->getPath());
	}

	UPDATE_TRACE_POINT();
	serverInstanceDirToucher = boost::make_shared<ServerInstanceDirToucher>(wo);
	wo->cleanupPidfiles = options.getStrSet("cleanup_pidfiles", false);

	UPDATE_TRACE_POINT();
	wo->loggingAgentAddress  = "unix:" + wo->instanceDir->getPath() + "/agents.s/logging";
	wo->loggingAgentPassword = wo->randomGenerator.generateAsciiString(64);
	wo->loggingAgentAdminAddress  = "unix:" + wo->instanceDir->getPath() + "/agents.s/logging_admin";

	UPDATE_TRACE_POINT();
	wo->adminToolStatusPassword = wo->randomGenerator.generateAsciiString(MESSAGE_SERVER_MAX_PASSWORD_SIZE);
	wo->adminToolManipulationPassword = wo->randomGenerator.generateAsciiString(MESSAGE_SERVER_MAX_PASSWORD_SIZE);
	options.set("admin_tool_status_password", wo->adminToolStatusPassword);
	options.set("admin_tool_manipulation_password", wo->adminToolManipulationPassword);
	if (geteuid() == 0 && !options.getBool("user_switching")) {
		createFile(wo->instanceDir->getPath() + "/passenger-status-password.txt",
			wo->adminToolStatusPassword, S_IRUSR, wo->defaultUid, wo->defaultGid);
		createFile(wo->instanceDir->getPath() + "/admin-manipulation-password.txt",
			wo->adminToolManipulationPassword, S_IRUSR, wo->defaultUid, wo->defaultGid);
	} else {
		createFile(wo->instanceDir->getPath() + "/passenger-status-password.txt",
			wo->adminToolStatusPassword, S_IRUSR | S_IWUSR);
		createFile(wo->instanceDir->getPath() + "/admin-manipulation-password.txt",
			wo->adminToolManipulationPassword, S_IRUSR | S_IWUSR);
	}
}

static void
initializeAgentWatchers(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers) {
	TRACE_POINT();
	watchers.push_back(make_shared<HelperAgentWatcher>(wo));
	watchers.push_back(make_shared<LoggingAgentWatcher>(wo));
}

static void
startAgents(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers) {
	TRACE_POINT();
	foreach (AgentWatcherPtr watcher, watchers) {
		try {
			watcher->start();
		} catch (const std::exception &e) {
			if (feedbackFdAvailable()) {
				writeArrayMessage(FEEDBACK_FD,
					"Watchdog startup error",
					e.what(),
					NULL);
			} else {
				const oxt::tracable_exception *e2 =
					dynamic_cast<const oxt::tracable_exception *>(&e);
				if (e2 != NULL) {
					P_CRITICAL("*** ERROR: " << e2->what() << "\n" << e2->backtrace());
				} else {
					P_CRITICAL("*** ERROR: " << e.what());
				}
			}
			forceAllAgentsShutdown(wo, watchers);
			exit(1);
		}
		// Allow other exceptions to propagate and crash the watchdog.
	}
}

static void
beginWatchingAgents(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers) {
	foreach (AgentWatcherPtr watcher, watchers) {
		try {
			watcher->beginWatching();
		} catch (const std::exception &e) {
			writeArrayMessage(FEEDBACK_FD,
				"Watchdog startup error",
				e.what(),
				NULL);
			forceAllAgentsShutdown(wo, watchers);
			exit(1);
		}
		// Allow other exceptions to propagate and crash the watchdog.
	}
}

static void
reportAgentsInformation(const WorkingObjectsPtr &wo, const vector<AgentWatcherPtr> &watchers) {
	if (feedbackFdAvailable()) {
		TRACE_POINT();
		VariantMap report;

		report.set("instance_dir", wo->instanceDir->getPath());

		foreach (AgentWatcherPtr watcher, watchers) {
			watcher->reportAgentsInformation(report);
		}

		report.writeToFd(FEEDBACK_FD, "Agents information");
	}
}

int
watchdogMain(int argc, char *argv[]) {
	initializeBareEssentials(argc, argv);
	setAgentsOptionsDefaults();
	sanityCheckOptions();
	P_INFO("Staring Watchdog with options: " << agentsOptions->inspect());
	WorkingObjectsPtr wo;
	ServerInstanceDirToucherPtr serverInstanceDirToucher;
	vector<AgentWatcherPtr> watchers;

	try {
		TRACE_POINT();
		maybeSetsid();
		initializeWorkingObjects(wo, serverInstanceDirToucher);
		initializeAgentWatchers(wo, watchers);
		UPDATE_TRACE_POINT();
		runHookScriptAndThrowOnError("before_watchdog_initialization");
	} catch (const std::exception &e) {
		if (feedbackFdAvailable()) {
			writeArrayMessage(FEEDBACK_FD,
				"Watchdog startup error",
				e.what(),
				NULL);
		} else {
			const oxt::tracable_exception *e2 =
				dynamic_cast<const oxt::tracable_exception *>(&e);
			if (e2 != NULL) {
				P_CRITICAL("*** ERROR: " << e2->what() << "\n" << e2->backtrace());
			} else {
				P_CRITICAL("*** ERROR: " << e.what());
			}
		}
		if (wo != NULL) {
			killCleanupPids(wo);
		}
		return 1;
	}
	// Allow other exceptions to propagate and crash the watchdog.

	try {
		TRACE_POINT();
		startAgents(wo, watchers);
		beginWatchingAgents(wo, watchers);
		reportAgentsInformation(wo, watchers);
		P_INFO("All Phusion Passenger agents started!");
		UPDATE_TRACE_POINT();
		runHookScriptAndThrowOnError("after_watchdog_initialization");

		UPDATE_TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		bool exitGracefully = waitForStarterProcessOrWatchers(wo, watchers);
		if (exitGracefully) {
			/* Fork a child process which cleans up all the agent processes in
			 * the background and exit this watchdog process so that we don't block
			 * the web server.
			 */
			P_DEBUG("Web server exited gracefully; gracefully shutting down all agents...");
		} else {
			P_DEBUG("Web server did not exit gracefully, forcing shutdown of all agents...");
		}
		UPDATE_TRACE_POINT();
		runHookScriptAndThrowOnError("after_watchdog_shutdown");
		UPDATE_TRACE_POINT();
		AgentWatcher::stopWatching(watchers);
		if (exitGracefully) {
			UPDATE_TRACE_POINT();
			cleanupAgentsInBackground(wo, watchers, argv);
		} else {
			UPDATE_TRACE_POINT();
			forceAllAgentsShutdown(wo, watchers);
		}
		UPDATE_TRACE_POINT();
		runHookScriptAndThrowOnError("after_watchdog_shutdown");
		return exitGracefully ? 0 : 1;
	} catch (const tracable_exception &e) {
		P_CRITICAL("*** ERROR: " << e.what() << "\n" << e.backtrace());
		return 1;
	}
}
