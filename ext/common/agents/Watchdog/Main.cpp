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
#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <string>

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
#include <Constants.h>
#include <ServerInstanceDir.h>
#include <FileDescriptor.h>
#include <RandomGenerator.h>
#include <Logging.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <ResourceLocator.h>
#include <Utils.h>
#include <Utils/Base64.h>
#include <Utils/Timer.h>
#include <Utils/ScopeGuard.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
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
static bool hasEnvOption(const char *name, bool defaultValue = false);
static void setOomScore(const StaticString &score);


/** The options that were passed to AgentsStarter. */
static VariantMap agentsOptions;
static string  tempDir;
static bool    userSwitching;
static string  defaultUser;
static string  defaultGroup;
static uid_t   webServerWorkerUid;
static gid_t   webServerWorkerGid;
static string  serializedPrestartURLs;

/***** Working objects *****/
static string oldOomScore;
static RandomGenerator *randomGenerator;
static EventFd *errorEvent;
static ResourceLocator *resourceLocator;
static ServerInstanceDirPtr serverInstanceDir;
static ServerInstanceDir::GenerationPtr generation;
static ServerInstanceDirToucher *serverInstanceDirToucher;
static string loggingAgentAddress;
static string loggingAgentPassword;

#include "AgentWatcher.cpp"
#include "HelperAgentWatcher.cpp"
#include "LoggingAgentWatcher.cpp"
#include "ServerInstanceDirToucher.cpp"
static vector<AgentWatcherPtr> watchers;


static bool
hasEnvOption(const char *name, bool defaultValue) {
	const char *value = getenv(name);
	if (value != NULL) {
		if (*value != '\0') {
			return strcmp(value, "yes") == 0
				|| strcmp(value, "y") == 0
				|| strcmp(value, "1") == 0
				|| strcmp(value, "on") == 0
				|| strcmp(value, "true") == 0;
		} else {
			return defaultValue;
		}
	} else {
		return defaultValue;
	}
}

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
		fwrite(score.data(), 1, score.size(), f);
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
waitForStarterProcessOrWatchers(vector<AgentWatcherPtr> &watchers) {
	fd_set fds;
	int max, ret;
	char x;
	
	FD_ZERO(&fds);
	FD_SET(FEEDBACK_FD, &fds);
	FD_SET(errorEvent->fd(), &fds);
	
	if (FEEDBACK_FD > errorEvent->fd()) {
		max = FEEDBACK_FD;
	} else {
		max = errorEvent->fd();
	}
	
	ret = syscalls::select(max + 1, &fds, NULL, NULL, NULL);
	if (ret == -1) {
		int e = errno;
		P_ERROR("select() failed: " << strerror(e));
		return false;
	}
	
	if (FD_ISSET(errorEvent->fd(), &fds)) {
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

static void
cleanupAgentsInBackground(vector<AgentWatcherPtr> &watchers, char *argv[]) {
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	pid_t pid;
	int e;

	pid = fork();
	if (pid == 0) {
		// Child
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
		for (it = watchers.begin(); it != watchers.end(); it++) {
			(*it)->forceShutdown();
		}
		
		// Now clean up the server instance directory.
		delete generation.get();
		delete serverInstanceDir.get();
		
		_exit(0);
		
	} else if (pid == -1) {
		// Error
		e = errno;
		throw SystemException("fork() failed", e);
		
	} else {
		// Parent
		
		// Let child process handle cleanup.
		serverInstanceDir->detach();
		generation->detach();
	}
}

static void
forceAllAgentsShutdown(vector<AgentWatcherPtr> &watchers) {
	vector<AgentWatcherPtr>::iterator it;
	
	for (it = watchers.begin(); it != watchers.end(); it++) {
		(*it)->forceShutdown();
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
	
	struct group *groupEntry = getgrgid(userEntry->pw_gid);
	if (groupEntry == NULL) {
		throw ConfigurationException(
			string("The option PassengerDefaultUser is set to '" +
			defaultUser + "', but its primary group doesn't exist. "
			"In other words, your system's user account database "
			"is broken. Please fix it."));
	}
	
	return groupEntry->gr_name;
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
	
	agentsOptions = initializeAgent(argc, argv, "PassengerWatchdog");
}

static void
initializeOptions() {
	TRACE_POINT();
	agentsOptions
		.setDefaultInt ("log_level", DEFAULT_LOG_LEVEL)
		.setDefault    ("temp_dir", getSystemTempDir())

		.setDefaultBool("user_switching", true)
		.setDefault    ("default_user", DEFAULT_WEB_APP_USER)
		.setDefaultUid ("web_server_worker_uid", getuid())
		.setDefaultGid ("web_server_worker_gid", getgid())
		.setDefault    ("default_ruby", DEFAULT_RUBY)
		.setDefault    ("default_python", DEFAULT_PYTHON)
		.setDefaultInt ("max_pool_size", DEFAULT_MAX_POOL_SIZE)
		.setDefaultInt ("max_instances_per_app", DEFAULT_MAX_INSTANCES_PER_APP)
		.setDefaultInt ("pool_idle_time", DEFAULT_POOL_IDLE_TIME);

	// Check for required options
	UPDATE_TRACE_POINT();
	agentsOptions.get("passenger_root");
	agentsOptions.getPid("web_server_pid");

	// Fetch optional options
	UPDATE_TRACE_POINT();
	tempDir       = agentsOptions.get("temp_dir");
	userSwitching = agentsOptions.getBool("user_switching");
	defaultUser   = agentsOptions.get("default_user");
	if (!agentsOptions.has("default_group")) {
		agentsOptions.set("default_group", inferDefaultGroup(defaultUser));
	}
	defaultGroup       = agentsOptions.get("default_group");
	webServerWorkerUid = agentsOptions.getUid("web_server_worker_uid");
	webServerWorkerGid = agentsOptions.getGid("web_server_worker_gid");

	P_INFO("Options: " << agentsOptions.inspect());
}

static void
initializeWorkingObjects() {
	TRACE_POINT();
	randomGenerator = new RandomGenerator();
	errorEvent = new EventFd();
	resourceLocator = new ResourceLocator(agentsOptions.get("passenger_root"));

	UPDATE_TRACE_POINT();
	// Must not used make_shared() here because Watchdog.cpp
	// deletes the raw pointer in cleanupAgentsInBackground().
	if (agentsOptions.get("server_instance_dir", false).empty()) {
		/* We embed the super structure version in the server instance directory name
		 * because it's possible to upgrade Phusion Passenger without changing the
		 * web server's PID. This way each incompatible upgrade will use its own
		 * server instance directory.
		 */
		string path = tempDir +
			"/passenger." +
			toString(SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION) + "." +
			toString(SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION) + "." +
			toString<unsigned long long>(agentsOptions.getPid("web_server_pid"));
		serverInstanceDir.reset(new ServerInstanceDir(path));
	} else {
		serverInstanceDir.reset(new ServerInstanceDir(agentsOptions.get("server_instance_dir")));
		agentsOptions.set("server_instance_dir", serverInstanceDir->getPath());
	}
	generation = serverInstanceDir->newGeneration(userSwitching, defaultUser,
		defaultGroup, webServerWorkerUid, webServerWorkerGid);
	agentsOptions.set("server_instance_dir", serverInstanceDir->getPath());
	agentsOptions.setInt("generation_number", generation->getNumber());

	UPDATE_TRACE_POINT();
	serverInstanceDirToucher = new ServerInstanceDirToucher();

	UPDATE_TRACE_POINT();
	loggingAgentAddress  = "unix:" + generation->getPath() + "/logging";
	loggingAgentPassword = randomGenerator->generateAsciiString(64);
}

static void
initializeAgentWatchers() {
	TRACE_POINT();
	watchers.push_back(make_shared<HelperAgentWatcher>(*resourceLocator));
	watchers.push_back(make_shared<LoggingAgentWatcher>(*resourceLocator));
}

static void
startAgents() {
	TRACE_POINT();
	foreach (AgentWatcherPtr watcher, watchers) {
		try {
			watcher->start();
		} catch (const std::exception &e) {
			writeArrayMessage(FEEDBACK_FD,
				"Watchdog startup error",
				e.what(),
				NULL);
			forceAllAgentsShutdown(watchers);
			exit(1);
		}
		// Allow other exceptions to propagate and crash the watchdog.
	}
}

static void
startWatchingAgents() {
	foreach (AgentWatcherPtr watcher, watchers) {
		try {
			watcher->startWatching();
		} catch (const std::exception &e) {
			writeArrayMessage(FEEDBACK_FD,
				"Watchdog startup error",
				e.what(),
				NULL);
			forceAllAgentsShutdown(watchers);
			exit(1);
		}
		// Allow other exceptions to propagate and crash the watchdog.
	}
}

static void
reportAgentsInformation() {
	TRACE_POINT();
	VariantMap report;

	report
		.set("server_instance_dir", serverInstanceDir->getPath())
		.setInt("generation", generation->getNumber());

	foreach (AgentWatcherPtr watcher, watchers) {
		watcher->reportAgentsInformation(report);
	}

	report.writeToFd(FEEDBACK_FD, "Agents information");
}

int
main(int argc, char *argv[]) {
	initializeBareEssentials(argc, argv);
	P_DEBUG("Starting Watchdog...");
	
	try {
		TRACE_POINT();
		initializeOptions();
		initializeWorkingObjects();
		initializeAgentWatchers();
	} catch (const std::exception &e) {
		writeArrayMessage(FEEDBACK_FD,
			"Watchdog startup error",
			e.what(),
			NULL);
		return 1;
	}
	// Allow other exceptions to propagate and crash the watchdog.

	try {
		TRACE_POINT();
		startAgents();
		startWatchingAgents();
		reportAgentsInformation();
		P_INFO("All Phusion Passenger agents started!");
		
		UPDATE_TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		bool exitGracefully = waitForStarterProcessOrWatchers(watchers);
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
		AgentWatcher::stopWatching(watchers);
		if (exitGracefully) {
			UPDATE_TRACE_POINT();
			cleanupAgentsInBackground(watchers, argv);
			return 0;
		} else {
			UPDATE_TRACE_POINT();
			forceAllAgentsShutdown(watchers);
			return 1;
		}
	} catch (const tracable_exception &e) {
		P_ERROR(e.what() << "\n" << e.backtrace());
		return 1;
	}
}
