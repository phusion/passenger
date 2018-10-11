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
// Include ev++.h early to avoid macro clash on EV_ERROR.
#include <ev++.h>

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
#include <algorithm>

#if !defined(sun) && !defined(__sun)
	#define HAVE_FLOCK
#endif

#ifdef __linux__
	#include <sys/prctl.h>
#endif
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#ifdef HAVE_FLOCK
	#include <sys/file.h>
#endif
#include <sys/resource.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <jsoncpp/json.h>
#include <Shared/Fundamentals/Initialization.h>
#include <Shared/ApiServerUtils.h>
#include <Core/OptionParser.h>
#include <Watchdog/Config.h>
#include <Watchdog/ApiServer.h>
#include <JsonTools/Autocast.h>
#include <Constants.h>
#include <InstanceDirectory.h>
#include <FileDescriptor.h>
#include <FileTools/PathSecurityCheck.h>
#include <SystemTools/UserDatabase.h>
#include <SystemTools/ContainerHelpers.h>
#include <RandomGenerator.h>
#include <BackgroundEventLoop.h>
#include <LoggingKit/LoggingKit.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <Hooks.h>
#include <IOTools/IOUtils.h>
#include <IOTools/MessageIO.h>
#include <Utils.h>
#include <Utils/Timer.h>
#include <Utils/ScopeGuard.h>
#include <StrIntTools/StrIntUtils.h>
#include <Utils/OptionParsing.h>
#include <Utils/VariantMap.h>

using namespace std;
using namespace boost;
using namespace oxt;
using namespace Passenger;
using namespace Passenger::Agent::Fundamentals;


enum OomFileType {
	OOM_ADJ,
	OOM_SCORE_ADJ
};

#define REQUEST_SOCKET_PASSWORD_SIZE     64

class InstanceDirToucher;
class AgentWatcher;


/***** Working objects *****/

namespace Passenger {
namespace Watchdog {
	struct WorkingObjects {
		RandomGenerator randomGenerator;
		EventFd errorEvent;
		EventFd exitEvent;
		uid_t defaultUid;
		gid_t defaultGid;
		InstanceDirectoryPtr instanceDir;
		int startupReportFile;
		int lockFile;
		vector<string> cleanupPidfiles;
		bool pidsCleanedUp;
		bool pidFileCleanedUp;
		string corePidFile;
		string fdPassingPassword;
		Json::Value extraConfigToPassToSubAgents;
		Json::Value controllerAddresses;
		Json::Value coreApiServerAddresses;
		Json::Value coreApiServerAuthorizations;
		Json::Value watchdogApiServerAddresses;
		Json::Value watchdogApiServerAuthorizations;

		int apiServerFds[SERVER_KIT_MAX_SERVER_ENDPOINTS];
		BackgroundEventLoop *bgloop;
		ServerKit::Context *serverKitContext;
		ServerKit::Schema serverKitSchema;
		ApiServer::ApiServer *apiServer;

		WorkingObjects()
			: errorEvent(__FILE__, __LINE__, "WorkingObjects: errorEvent"),
			  exitEvent(__FILE__, __LINE__, "WorkingObjects: exitEvent"),
			  startupReportFile(-1),
			  pidsCleanedUp(false),
			  pidFileCleanedUp(false),
			  extraConfigToPassToSubAgents(Json::objectValue),
			  controllerAddresses(Json::arrayValue),
			  coreApiServerAddresses(Json::arrayValue),
			  coreApiServerAuthorizations(Json::arrayValue),
			  watchdogApiServerAddresses(Json::arrayValue),
			  watchdogApiServerAuthorizations(Json::arrayValue),
			  bgloop(NULL),
			  serverKitContext(NULL),
			  apiServer(NULL)
		{
			for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
				apiServerFds[i] = -1;
			}
		}
	};

	typedef boost::shared_ptr<WorkingObjects> WorkingObjectsPtr;
} // namespace Watchdog
} // namespace Passenger

using namespace Passenger::Watchdog;

static WrapperRegistry::Registry *watchdogWrapperRegistry;
static Schema *watchdogSchema;
static ConfigKit::Store *watchdogConfig;
static WorkingObjects *workingObjects;

static void cleanup(const WorkingObjectsPtr &wo);

#include "AgentWatcher.cpp"
#include "InstanceDirToucher.cpp"
#include "CoreWatcher.cpp"


/***** Functions *****/

#if !BOOST_OS_MACOS

struct WatchdogOomAdjustResult {
	struct Message {
		LoggingKit::Level level;
		string text;
	};

	string oldScore;
	// LoggingKit has not been initialized yet when setOomScoreNeverKill()
	// is called, so we store the messages here and print them after
	// LoggingKit initialization.
	vector<Message> messages;
};

static FILE *
openOomAdjFileGetType(const char *mode, OomFileType &type, string &path) {
	FILE *f = fopen("/proc/self/oom_score_adj", mode);
	if (f == NULL) {
		f = fopen("/proc/self/oom_adj", mode);
		if (f == NULL) {
			return NULL;
		} else {
			type = OOM_ADJ;
			path = "/proc/self/oom_adj";
			return f;
		}
	} else {
		type = OOM_SCORE_ADJ;
		path = "/proc/self/oom_score_adj";
		return f;
	}
}

/**
 * Set the current process's OOM score to "never kill".
 */
static WatchdogOomAdjustResult
setOomScoreNeverKill() {
	WatchdogOomAdjustResult result;
	FILE *f;
	string path;
	OomFileType type;
	int e;

	if (geteuid() != 0) {
		WatchdogOomAdjustResult::Message msg;
		msg.level = LoggingKit::DEBUG;
		msg.text = "Not adjusting Watchdog's OOM score because not running with root privileges";
		result.messages.push_back(msg);
		return result;
	}

	f = openOomAdjFileGetType("r", type, path);
	if (f == NULL) {
		e = errno;
		P_ERROR("Error adjusting Watchdog's OOM score: error opening both"
			" /proc/self/oom_score_adj and /proc/self/oom_adj for reading: " <<
			strerror(e) << " (errno=" << e << ")");
		return result;
	}
	// mark if this is a legacy score so we won't try to write it as OOM_SCORE_ADJ
	if (type == OOM_ADJ) {
		result.oldScore.append("l");
	}
	char buf[1024];
	size_t bytesRead;
	while (true) {
		bytesRead = fread(buf, 1, sizeof(buf), f);
		if (bytesRead == 0 && feof(f)) {
			break;
		} else if (bytesRead == 0 && ferror(f)) {
			P_ERROR("Error adjusting Watchdog's OOM score: error reading " << path);
			fclose(f);
			result.oldScore.clear();
			return result;
		} else {
			result.oldScore.append(buf, bytesRead);
		}
	}
	fclose(f);

	f = fopen(path.c_str(), "w");
	if (f == NULL) {
		e = errno;
		WatchdogOomAdjustResult::Message msg;
		msg.level = LoggingKit::ERROR;
		msg.text = "Error adjusting Watchdog's OOM score: error opening "
			+ path + " for writing: " + strerror(e) + " (errno="
			+ toString(e) + ")";
		result.messages.push_back(msg);
		result.oldScore.clear();
		return result;
	}
	if (type == OOM_SCORE_ADJ) {
		fprintf(f, "-1000\n");
	} else {
		assert(type == OOM_ADJ);
		fprintf(f, "-17\n");
	}

	e = fflush(f);
	if (e != 0) {
		e = errno;
		WatchdogOomAdjustResult::Message msg;
		if (autoDetectInContainer()) {
			msg.level = LoggingKit::INFO;
			msg.text = "Running in container, so couldn't adjust Watchdog's"
				" OOM score through " + path;
		} else {
			msg.level = LoggingKit::ERROR;
			msg.text = "Error adjusting Watchdog's OOM score: error writing to "
				+ path + ": " + strerror(e) + " (errno=" + toString(e) + ")";
		}
		result.messages.push_back(msg);
	}
	e = fclose(f);
	if (e == EOF) {
		e = errno;
		WatchdogOomAdjustResult::Message msg;
		msg.level = LoggingKit::ERROR;
		msg.text = "Error adjusting Watchdog's OOM score: error closing "
			+ path + ": " + strerror(e) + " (errno=" + toString(e) + ")";
		result.messages.push_back(msg);
	}

	return result;
}

static void
printOomAdjustResultMessages(const WatchdogOomAdjustResult &result) {
	vector<WatchdogOomAdjustResult::Message>::const_iterator it;

	for (it = result.messages.begin(); it != result.messages.end(); it++) {
		P_LOG(LoggingKit::context, it->level, __FILE__, __LINE__, it->text);
	}
}

#endif

static void
terminationHandler(int signo) {
	ssize_t ret = write(workingObjects->exitEvent.writerFd(), "x", 1);
	(void) ret; // Don't care about the result.
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
	TRACE_POINT();
	fd_set fds;
	int max = -1, ret;
	char x;

	wo->bgloop->start("Main event loop", 0);

	struct sigaction action;
	action.sa_handler = terminationHandler;
	action.sa_flags = SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);

	FD_ZERO(&fds);
	if (feedbackFdAvailable()) {
		FD_SET(FEEDBACK_FD, &fds);
		max = std::max(max, FEEDBACK_FD);
	}
	FD_SET(wo->errorEvent.fd(), &fds);
	max = std::max(max, wo->errorEvent.fd());
	FD_SET(wo->exitEvent.fd(), &fds);
	max = std::max(max, wo->exitEvent.fd());

	UPDATE_TRACE_POINT();
	ret = syscalls::select(max + 1, &fds, NULL, NULL, NULL);
	if (ret == -1) {
		int e = errno;
		P_ERROR("select() failed: " << strerror(e));
		return false;
	}

	action.sa_handler = SIG_DFL;
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);

	P_DEBUG("Stopping API server");
	wo->bgloop->stop();
	for (unsigned int i = 0; i < SERVER_KIT_MAX_SERVER_ENDPOINTS; i++) {
		if (wo->apiServerFds[i] != -1) {
			syscalls::close(wo->apiServerFds[i]);
		}
	}

	if (FD_ISSET(wo->errorEvent.fd(), &fds)) {
		UPDATE_TRACE_POINT();
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
	} else if (FD_ISSET(wo->exitEvent.fd(), &fds)) {
		return true;
	} else {
		UPDATE_TRACE_POINT();
		assert(feedbackFdAvailable());
		ret = syscalls::read(FEEDBACK_FD, &x, 1);
		return ret == 1 && x == 'c';
	}
}

string
relative(string filename){
	string dir = filename.substr(filename.find_last_of('/')+1);
	return dir;
}

static vector<pid_t>
readCleanupPids(const WorkingObjectsPtr &wo) {
	vector<pid_t> result;

	foreach (string filename, wo->cleanupPidfiles) {
		FILE *f = fopen(relative(filename).c_str(), "r");
		if (f != NULL) {
			char buf[33];
			size_t ret;

			ret = fread(buf, 1, 32, f);
			fclose(f);
			if (ret > 0) {
				buf[ret] = '\0';
				result.push_back(atoi(buf));
			} else {
				P_WARN("Cannot read cleanup PID file " << relative(filename).c_str() << " (" << filename << ")");
			}
		} else {
			P_WARN("Cannot open cleanup PID file " << relative(filename).c_str() << " (" << filename << ")");
		}
	}

	return result;
}

static void
killCleanupPids(const vector<pid_t> &cleanupPids) {
	foreach (pid_t pid, cleanupPids) {
		P_DEBUG("Sending SIGTERM to cleanup PID " << pid);
		if(kill(pid, SIGTERM) == -1){
			int e = errno;
			P_WARN("Failed to send SIGTERM to " << pid << ", error: " << e << " " << strerror(e));
		}
	}
}

static void
killCleanupPids(const WorkingObjectsPtr &wo) {
	if (!wo->pidsCleanedUp) {
		killCleanupPids(readCleanupPids(wo));
		wo->pidsCleanedUp = true;
	}
}

static void
deletePidFile(const WorkingObjectsPtr &wo) {
	Json::Value pidFile = watchdogConfig->get("watchdog_pid_file");
	if (!pidFile.isNull() && !wo->pidFileCleanedUp && watchdogConfig->get("watchdog_pid_file_autodelete").asBool()) {
		syscalls::unlink(pidFile.asCString());
		wo->pidFileCleanedUp = true;
	}
}

static void
cleanupAgentsInBackground(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers, char *argv[]) {
	boost::this_thread::disable_interruption di;
	boost::this_thread::disable_syscall_interruption dsi;
	pid_t pid;
	int e;

	pid = fork();
	if (pid == 0) {
		// Child
		try {
			vector<AgentWatcherPtr>::const_iterator it;
			Timer<SystemTime::GRAN_10MSEC> timer(false);
			fd_set fds, fds2;
			int max, agentProcessesDone;
			unsigned long long deadline = 30000; // miliseconds

			#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(sun)
				// Change process title.
				strcpy(argv[0], "PassengerWatchdog (cleaning up...)");
			#endif

			P_DEBUG("Sending SIGTERM to all agent processes");
			for (it = watchers.begin(); it != watchers.end(); it++) {
				(*it)->signalShutdown();
			}

			max = 0;
			FD_ZERO(&fds);
			for (it = watchers.begin(); it != watchers.end(); it++) {
				FD_SET((*it)->getFeedbackFd(), &fds);
				if ((*it)->getFeedbackFd() > max) {
					max = (*it)->getFeedbackFd();
				}
			}

			P_DEBUG("Waiting until all agent processes have exited...");
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
				P_WARN("Some " PROGRAM_NAME " agent processes did not exit " <<
					"in time, forcefully shutting down all.");
			} else {
				P_DEBUG("All " PROGRAM_NAME " agent processes have exited. Forcing all subprocesses to shut down.");
			}
			P_DEBUG("Sending SIGKILL to all agent processes");
			for (it = watchers.begin(); it != watchers.end(); it++) {
				(*it)->forceShutdown();
			}

			cleanup(wo);

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

	P_DEBUG("Sending SIGTERM to all agent processes");
	for (it = watchers.begin(); it != watchers.end(); it++) {
		(*it)->signalShutdown();
	}
	usleep(1000000);
	P_DEBUG("Sending SIGKILL to all agent processes");
	for (it = watchers.begin(); it != watchers.end(); it++) {
		(*it)->forceShutdown();
	}
}

static void
runHookScriptAndThrowOnError(const char *name) {
	TRACE_POINT();
	HookScriptOptions options;

	options.name = name;
	options.spec = watchdogConfig->get(string("hook_") + name).asString();
	options.agentConfig = watchdogConfig->inspectEffectiveValues();

	if (!runHookScripts(options)) {
		throw RuntimeException(string("Hook script ") + name + " failed");
	}
}


static void
usage() {
	printf("Usage: " AGENT_EXE " watchdog <OPTIONS...>\n");
	printf("Runs the " PROGRAM_NAME " watchdog.\n\n");
	printf("The watchdog runs and supervises various " PROGRAM_NAME " agent processes,\n");
	printf("at this moment only the core (there was also a process called UstRouter but\n");
	printf("it no longer exists). Arguments marked with \"[A]\", e.g.\n");
	printf("--passenger-root and --log-level, are automatically passed to all supervised\n");
	printf("agents, unless you explicitly override them by passing extra arguments to a\n");
	printf("supervised agent specifically. You can pass arguments to a supervised agent by\n");
	printf("wrapping those arguments between --BC/--EC and --BU/--EU.\n");
	printf("\n");
	printf("  Example 1: pass some arguments to the core.\n\n");
	printf("  " SHORT_PROGRAM_NAME " watchdog --passenger-root /opt/passenger \\\n");
	printf("    --BC --listen tcp://127.0.0.1:4000 /webapps/foo\n");
	printf("\n");
	printf("  Example 2: pass some arguments to the core, and some others to the\n");
	printf("  UstRouter. The watchdog itself and the core will use logging\n");
	printf("  level 3, while the UstRouter will use logging level 1.\n\n");
	printf("  " SHORT_PROGRAM_NAME " watchdog --passenger-root /opt/passenger \\\n");
	printf("    --BC --listen tcp://127.0.0.1:4000 /webapps/foo --EC \\\n");
	printf("    --BU --log-level 1 --EU \\\n");
	printf("    --log-level 3\n");
	printf("\n");
	printf("Required options:\n");
	printf("       --passenger-root PATH  The location to the " PROGRAM_NAME " source\n");
	printf("                              directory [A]\n");
	printf("\n");
	printf("Argument passing options (optional):\n");
	printf("  --BC, --begin-core-args   Signals the beginning of arguments to pass to the\n");
	printf("                            Passenger core\n");
	printf("  --EC, --end-core-args     Signals the end of arguments to pass to the\n");
	printf("                            Passenger core\n");
	printf("  --BU, --begin-ust-router-args\n");
	printf("                            Signals the beginning of arguments to pass to the\n");
	printf("                            UstRouter\n");
	printf("  --EU, --end-ust-router-args\n");
	printf("                              Signals the end of arguments to pass to the\n");
	printf("                            UstRouter\n");
	printf("\n");
	printf("Other options (optional):\n");
	printf("      --api-listen ADDRESS  Listen on the given address for API commands.\n");
	printf("                            The address must be formatted as tcp://IP:PORT for\n");
	printf("                            TCP sockets, or unix:PATH for Unix domain sockets.\n");
	printf("                            You can specify this option multiple times (up to\n");
	printf("                            %u times) to listen on multiple addresses.\n",
		SERVER_KIT_MAX_SERVER_ENDPOINTS - 1);
	printf("      --authorize [LEVEL]:USERNAME:PASSWORDFILE\n");
	printf("                            Enables authentication on the API server, through\n");
	printf("                            the given API account. LEVEL indicates the\n");
	printf("                            privilege level (see below). PASSWORDFILE must\n");
	printf("                            point to a file containing the password\n");
	printf("\n");
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
	printf("      --daemonize             Daemonize into the background\n");
	printf("      --user NAME             Lower privilege to the given user\n");
	printf("      --pid-file PATH         Store the watchdog's PID in the given file. The\n");
	printf("                              file is deleted on exit\n");
	printf("      --no-delete-pid-file    Do not delete PID file on exit\n");
	printf("      --log-file PATH         Log to the given file.\n");
	printf("      --log-level LEVEL       Logging level. [A] Default: %d\n", DEFAULT_LOG_LEVEL);
	printf("      --report-file PATH      Upon successful initialization, report instance\n");
	printf("                              information to the given file, in JSON format\n");
	printf("      --cleanup-pidfile PATH  Upon shutdown, kill the process specified by\n");
	printf("                              the given PID file\n");
	printf("\n");
	printf("      --ctl NAME=VALUE        Set custom internal option\n");
	printf("\n");
	printf("  -h, --help                  Show this help\n");
	printf("\n");
	printf("[A] = Automatically passed to supervised agents\n");
	printf("\n");
	printf("API account privilege levels (ordered from most to least privileges):\n");
	printf("  readonly    Read-only access\n");
	printf("  full        Full access (default)\n");
}

static void
parseOptions(int argc, const char *argv[], ConfigKit::Store &config) {
	OptionParser p(usage);
	Json::Value updates(Json::objectValue);
	int i = 2;

	while (i < argc) {
		if (p.isValueFlag(argc, i, argv[i], '\0', "--passenger-root")) {
			updates["passenger_root"] = argv[i + 1];
			i += 2;
		} else if (p.isFlag(argv[i], '\0', "--BC")
			|| p.isFlag(argv[i], '\0', "--begin-core-args"))
		{
			i++;
			while (i < argc) {
				if (p.isFlag(argv[i], '\0', "--EC")
				 || p.isFlag(argv[i], '\0', "--end-core-args"))
				{
					i++;
					break;
				} else if (p.isFlag(argv[i], '\0', "--BU")
				 || p.isFlag(argv[i], '\0', "--begin-ust-router-args"))
				{
					break;
				} else {
					Json::Value coreUpdates(Json::objectValue);
					if (!parseCoreOption(argc, argv, i, coreUpdates)) {
						fprintf(stderr, "ERROR: unrecognized core argument %s. Please "
							"type '%s core --help' for usage.\n", argv[i], argv[0]);
						exit(1);
					}

					Json::Value::iterator it, end = coreUpdates.end();
					for (it = coreUpdates.begin(); it != end; it++) {
						string translatedName = watchdogSchema->core.translator.
							reverseTranslateOne(it.name());
						updates[translatedName] = *it;
					}
				}
			}
		} else if (p.isFlag(argv[i], '\0', "--BU")
			|| p.isFlag(argv[i], '\0', "--begin-ust-router-args"))
		{
			i++;
			while (i < argc) {
				if (p.isFlag(argv[i], '\0', "--EU")
				 || p.isFlag(argv[i], '\0', "--end-ust-router-args"))
				{
					i++;
					break;
				} else if (p.isFlag(argv[i], '\0', "--BC")
				 || p.isFlag(argv[i], '\0', "--begin-core-args"))
				{
					break;
				} else {
					fprintf(stderr, "ERROR: unrecognized UstRouter argument %s. Please "
						"type '%s ust-router --help' for usage.\n", argv[i], argv[0]);
					exit(1);
				}
			}
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--api-listen")) {
			if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
				Json::Value &addresses = updates["watchdog_api_server_addresses"];
				if (addresses.size() == SERVER_KIT_MAX_SERVER_ENDPOINTS - 1) {
					fprintf(stderr, "ERROR: you may specify up to %u --api-listen addresses.\n",
						SERVER_KIT_MAX_SERVER_ENDPOINTS - 1);
					exit(1);
				}
				addresses.append(argv[i + 1]);
				i += 2;
			} else {
				fprintf(stderr, "ERROR: invalid address format for --api-listen. The address "
					"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
					"for Unix domain sockets.\n");
				exit(1);
			}
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--authorize")) {
			vector<string> args;
			split(argv[i + 1], ':', args);
			if (args.size() < 2 || args.size() > 3) {
				fprintf(stderr, "ERROR: invalid format for --authorize. The syntax "
					"is \"[LEVEL:]USERNAME:PASSWORDFILE\".\n");
				exit(1);
			}

			updates["watchdog_api_server_authorizations"].append(argv[i + 1]);
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--instance-registry-dir")) {
			updates["instance_registry_dir"] = argv[i + 1];
			i += 2;
		} else if (p.isFlag(argv[i], '\0', "--no-user-switching")) {
			updates["user_switching"] = false;
			i++;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--default-user")) {
			updates["default_user"] = argv[i + 1];
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--default-group")) {
			updates["default_group"] = argv[i + 1];
			i += 2;
		} else if (p.isFlag(argv[i], '\0', "--daemonize")) {
			updates["daemonize"] = true;
			i++;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--user")) {
			updates["user"] = argv[i + 1];
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--pid-file")) {
			updates["watchdog_pid_file"] = argv[i + 1];
			i += 2;
		} else if (p.isFlag(argv[i], '\0', "--no-delete-pid-file")) {
			updates["watchdog_pid_file_autodelete"] = false;
			i++;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-level")) {
			updates["log_level"] = argv[i + 1];
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--report-file")) {
			updates["startup_report_file"] = argv[i + 1];
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--cleanup-pidfile")) {
			updates["pidfiles_to_delete_on_exit"].append(argv[i + 1]);
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-file")) {
			updates["log_target"] = argv[i + 1];
			i += 2;
		} else if (p.isValueFlag(argc, i, argv[i], '\0', "--ctl")) {
			const char *value = strchr(argv[i + 1], '=');
			if (value == NULL) {
				fprintf(stderr, "ERROR: '%s' is not a valid --ctl parameter. "
					"It must be in the form of NAME=VALUE.\n", argv[i + 1]);
				exit(1);
			}
			string name(argv[i + 1], value - argv[i + 1]);
			value++;
			if (*value == '\0') {
				fprintf(stderr, "ERROR: '%s' is not a valid --ctl parameter. "
					"The value must be non-empty.\n", argv[i + 1]);
				exit(1);
			}
			updates[name] = autocastValueToJson(value);
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
initializeBareEssentials(int argc, char *argv[], WorkingObjectsPtr &wo) {
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
#if !BOOST_OS_MACOS
	WatchdogOomAdjustResult oomAdjustResult = setOomScoreNeverKill();
#endif

	watchdogWrapperRegistry = new WrapperRegistry::Registry();
	watchdogWrapperRegistry->finalize();
	watchdogSchema = new Schema(watchdogWrapperRegistry);
	watchdogConfig = new ConfigKit::Store(*watchdogSchema);
	initializeAgent(argc, &argv, SHORT_PROGRAM_NAME " watchdog",
		*watchdogConfig, watchdogSchema->core.schema.loggingKit.translator,
		parseOptions, NULL, 2);

	// Start all sub-agents with this environment variable.
	setenv("PASSENGER_USE_FEEDBACK_FD", "true", 1);

	wo = boost::make_shared<WorkingObjects>();
	workingObjects = wo.get();
#if !BOOST_OS_MACOS
	printOomAdjustResultMessages(oomAdjustResult);
	wo->extraConfigToPassToSubAgents["oom_score"] = oomAdjustResult.oldScore;
#endif
}

static void
maybeSetsid() {
	/* Become the session leader so that Apache can't kill the
	 * watchdog with killpg() during shutdown, so that a
	 * Ctrl-C only affects the web server, and so that
	 * we can kill all of our subprocesses in a single killpg().
	 *
	 * WatchdogLauncher.h already calls setsid() before exec()ing
	 * the Watchdog, but Flying Passenger does not.
	 */
	if (watchdogConfig->get("setsid").asBool()) {
		setsid();
	}
}

static void
redirectStdinToNull() {
	int fd = open("/dev/null", O_RDONLY);
	if (fd != -1) {
		dup2(fd, 0);
		close(fd);
	}
}

static void
maybeDaemonize() {
	pid_t pid;
	int e;

	if (watchdogConfig->get("daemonize").asBool()) {
		pid = fork();
		if (pid == 0) {
			setsid();
			redirectStdinToNull();
		} else if (pid == -1) {
			e = errno;
			throw SystemException("Cannot fork", e);
		} else {
			_exit(0);
		}
	}
}

static void
createPidFile() {
	TRACE_POINT();
	Json::Value pidFile = watchdogConfig->get("watchdog_pid_file");
	if (!pidFile.isNull()) {
		char pidStr[32];

		snprintf(pidStr, sizeof(pidStr), "%lld", (long long) getpid());

		int fd = syscalls::open(pidFile.asCString(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd == -1) {
			int e = errno;
			throw FileSystemException("Cannot create PID file " + pidFile.asString(),
				e, pidFile.asString());
		}

		UPDATE_TRACE_POINT();
		FdGuard guard(fd, __FILE__, __LINE__);
		writeExact(fd, pidStr, strlen(pidStr));
	}
}

static void
openStartupReportFile(const WorkingObjectsPtr &wo) {
	TRACE_POINT();
	Json::Value path = watchdogConfig->get("startup_report_file");
	if (!path.isNull()) {
		int fd = syscalls::open(path.asCString(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd == -1) {
			int e = errno;
			throw FileSystemException("Cannot open report file " + path.asString(),
				e, path.asString());
		}

		P_LOG_FILE_DESCRIPTOR_OPEN4(fd, __FILE__, __LINE__, "WorkingObjects: startupReportFile");
		wo->startupReportFile = fd;
	}
}

static void
chdirToTmpDir() {
	const Json::Value pidfiles = watchdogConfig->get("pidfiles_to_delete_on_exit");
	if (!pidfiles.empty()) {
		string str = pidfiles[0].asString();
		string dir = str.substr(0,str.find_last_of('/'));
		if (dir != "" && chdir(dir.c_str()) == -1) {
			throw RuntimeException("Cannot change working directory to " + dir);
		}
	}
}

static void
lowerPrivilege() {
	TRACE_POINT();
	string userName = watchdogConfig->get("user").asString();

	if (geteuid() == 0 && !userName.empty()) {
		OsUser osUser;
		if (!lookupSystemUserByName(userName, osUser)) {
			throw NonExistentUserException("Operating system user '" + userName
				+ "' does not exist");
		}

		gid_t gid = osUser.pwd.pw_gid;
		string groupName = lookupSystemGroupnameByGid(gid);

		if (initgroups(userName.c_str(), gid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " SHORT_PROGRAM_NAME " watchdog's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set supplementary groups", e);
		}
		if (setgid(gid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " SHORT_PROGRAM_NAME " watchdog's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set group ID to " + toString(gid), e);
		}
		if (setuid(osUser.pwd.pw_uid) != 0) {
			int e = errno;
			throw SystemException("Unable to lower " SHORT_PROGRAM_NAME " watchdog's privilege "
				"to that of user '" + userName + "' and group '" + groupName +
				"': cannot set user ID to " + toString(osUser.pwd.pw_uid), e);
		}
		#ifdef __linux__
			// When we change the uid, /proc/self/pid contents don't change owner,
			// causing us to lose access to our own /proc/self/pid files.
			// This prctl call changes those files' ownership.
			// References:
			// https://stackoverflow.com/questions/8337846/files-ownergroup-doesnt-change-at-location-proc-pid-after-setuid
			// http://man7.org/linux/man-pages/man5/proc.5.html (search for "dumpable")
			prctl(PR_SET_DUMPABLE, 1);
		#endif
		setenv("USER", osUser.pwd.pw_name, 1);
		setenv("HOME", osUser.pwd.pw_dir, 1);
		setenv("UID", toString(gid).c_str(), 1);
	}
}

static void
lookupDefaultUidGid(uid_t &uid, gid_t &gid) {
	const string defaultUser = watchdogConfig->get("default_user").asString();
	const string defaultGroup = watchdogConfig->get("default_group").asString();

	OsUser osUser;
	if (!lookupSystemUserByName(defaultUser, osUser)) {
		throw NonExistentUserException("Default user '" + defaultUser +
			"' does not exist");
	}
	uid = osUser.pwd.pw_uid;

	OsGroup osGroup;
	if (!lookupSystemGroupByName(defaultGroup, osGroup)) {
		throw NonExistentGroupException("Default group '" + defaultGroup +
			"' does not exist");
	}
	gid = osGroup.grp.gr_gid;
}

static void
warnIfInstanceDirVulnerable(const string &root) {
	TRACE_POINT();

	if (geteuid() != 0) {
		return; // Passenger is not root, so no escalation.
	}

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
			" instance directory (" + root + ") can be changed by non-root user(s):\n";
		foreach (string line, errors) {
			message.append("\n - " + line);
		}
		foreach (string line, checkErrors) {
			message.append("\n - " + line);
		}
		message.append("\n\nPlease either fix up the permissions for the insecure paths, or use" \
					   " a different location for the instance dir that can only be modified by root.");
		P_WARN(message);
	}
}

static void
initializeWorkingObjects(const WorkingObjectsPtr &wo, InstanceDirToucherPtr &instanceDirToucher,
	uid_t uidBeforeLoweringPrivilege)
{
	TRACE_POINT();
	Json::Value doc;
	Json::Value::iterator it, end;

	UPDATE_TRACE_POINT();
	lookupDefaultUidGid(wo->defaultUid, wo->defaultGid);

	doc = watchdogConfig->get("pidfiles_to_delete_on_exit");
	for (it = doc.begin(); it != doc.end(); it++) {
		wo->cleanupPidfiles.push_back(it->asString());
	}

	UPDATE_TRACE_POINT();
	InstanceDirectory::CreationOptions instanceOptions;
	instanceOptions.userSwitching = watchdogConfig->get("user_switching").asBool();
	instanceOptions.originalUid = uidBeforeLoweringPrivilege;
	instanceOptions.defaultUid = wo->defaultUid;
	instanceOptions.defaultGid = wo->defaultGid;
	instanceOptions.properties["name"] = wo->randomGenerator.generateAsciiString(8);
	instanceOptions.properties["integration_mode"] = watchdogConfig->get("integration_mode").asString();
	instanceOptions.properties["server_software"] = watchdogConfig->get("server_software").asString();
	if (watchdogConfig->get("integration_mode").asString() == "standalone") {
		instanceOptions.properties["standalone_engine"] = watchdogConfig->get("standalone_engine").asString();
	}

	// check if path is safe
	warnIfInstanceDirVulnerable(watchdogConfig->get("instance_registry_dir").asString());

	wo->instanceDir = boost::make_shared<InstanceDirectory>(instanceOptions,
		watchdogConfig->get("instance_registry_dir").asString());
	wo->extraConfigToPassToSubAgents["instance_dir"] = wo->instanceDir->getPath();
	instanceDirToucher = boost::make_shared<InstanceDirToucher>(wo);

	UPDATE_TRACE_POINT();
	string lockFilePath = wo->instanceDir->getPath() + "/lock";
	wo->lockFile = syscalls::open(lockFilePath.c_str(), O_RDONLY);
	if (wo->lockFile == -1) {
		int e = errno;
		throw FileSystemException("Cannot open " + lockFilePath + " for reading",
			e, lockFilePath);
	}
	P_LOG_FILE_DESCRIPTOR_OPEN4(wo->lockFile, __FILE__, __LINE__, "WorkingObjects: lock file");

	createFile(wo->instanceDir->getPath() + "/watchdog.pid", toString(getpid()));

	UPDATE_TRACE_POINT();
	string readOnlyAdminPassword = wo->randomGenerator.generateAsciiString(24);
	string fullAdminPassword = wo->randomGenerator.generateAsciiString(24);
	if (geteuid() == 0 && !watchdogConfig->get("user_switching").asBool()) {
		createFile(wo->instanceDir->getPath() + "/read_only_admin_password.txt",
			readOnlyAdminPassword, S_IRUSR, wo->defaultUid, wo->defaultGid);
		createFile(wo->instanceDir->getPath() + "/full_admin_password.txt",
			fullAdminPassword, S_IRUSR, wo->defaultUid, wo->defaultGid);
	} else {
		createFile(wo->instanceDir->getPath() + "/read_only_admin_password.txt",
			readOnlyAdminPassword, S_IRUSR | S_IWUSR);
		createFile(wo->instanceDir->getPath() + "/full_admin_password.txt",
			fullAdminPassword, S_IRUSR | S_IWUSR);
	}

	if (watchdogConfig->get("core_pid_file").isNull()) {
		wo->corePidFile = wo->instanceDir->getPath() + "/core.pid";
	} else {
		wo->corePidFile = watchdogConfig->get("core_pid_file").asString();
	}
	wo->fdPassingPassword = wo->randomGenerator.generateAsciiString(24);

	UPDATE_TRACE_POINT();
	wo->controllerAddresses.append("unix:" + wo->instanceDir->getPath() + "/agents.s/core");
	doc = watchdogConfig->get("controller_addresses");
	for (it = doc.begin(); it != doc.end(); it++) {
		wo->controllerAddresses.append(*it);
	}

	wo->coreApiServerAddresses.append("unix:" + wo->instanceDir->getPath() + "/agents.s/core_api");
	doc = watchdogConfig->get("core_api_server_addresses");
	for (it = doc.begin(); it != doc.end(); it++) {
		wo->coreApiServerAddresses.append(*it);
	}

	UPDATE_TRACE_POINT();
	wo->coreApiServerAuthorizations.append(
		"readonly:ro_admin:" + wo->instanceDir->getPath() +
		"/read_only_admin_password.txt");
	wo->coreApiServerAuthorizations.append(
		"full:admin:" + wo->instanceDir->getPath() +
		"/full_admin_password.txt");
	doc = watchdogConfig->get("core_api_server_authorizations");
	for (it = doc.begin(); it != doc.end(); it++) {
		wo->coreApiServerAuthorizations.append(*it);
	}
}

static void
initializeAgentWatchers(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers) {
	TRACE_POINT();
	watchers.push_back(boost::make_shared<CoreWatcher>(wo));
}

static void
makeFileWorldReadableAndWritable(const string &path) {
	int ret;

	do {
		ret = chmod(path.c_str(), parseModeString("u=rw,g=rw,o=rw"));
	} while (ret == -1 && errno == EINTR);
}

static void
initializeApiServer(const WorkingObjectsPtr &wo) {
	TRACE_POINT();
	Json::Value doc;
	Json::Value::iterator it;
	string description;

	UPDATE_TRACE_POINT();
	wo->watchdogApiServerAuthorizations.append(
		"readonly:ro_admin:" + wo->instanceDir->getPath() +
		"/read_only_admin_password.txt");
	wo->watchdogApiServerAuthorizations.append(
		"full:admin:" + wo->instanceDir->getPath() +
		"/full_admin_password.txt");
	doc = watchdogConfig->get("watchdog_api_server_authorizations");
	for (it = doc.begin(); it != doc.end(); it++) {
		wo->watchdogApiServerAuthorizations.append(*it);
	}

	UPDATE_TRACE_POINT();
	wo->watchdogApiServerAddresses.append(
		"unix:" + wo->instanceDir->getPath() +
		"/agents.s/watchdog_api");
	doc = watchdogConfig->get("watchdog_api_server_addresses");
	for (it = doc.begin(); it != doc.end(); it++) {
		wo->watchdogApiServerAddresses.append(*it);
	}

	UPDATE_TRACE_POINT();
	for (unsigned int i = 0; i < wo->watchdogApiServerAddresses.size(); i++) {
		string address = wo->watchdogApiServerAddresses[i].asString();
		P_DEBUG("API server will listen on " << address);
		wo->apiServerFds[i] = createServer(address, 0, true,
			__FILE__, __LINE__);
		if (getSocketAddressType(address) == SAT_UNIX) {
			makeFileWorldReadableAndWritable(parseUnixSocketAddress(address));
		}
	}

	UPDATE_TRACE_POINT();
	Json::Value contextConfig = watchdogConfig->inspectEffectiveValues();
	wo->bgloop = new BackgroundEventLoop(true, true);
	wo->serverKitContext = new ServerKit::Context(
		watchdogSchema->apiServerKit.schema,
		contextConfig,
		watchdogSchema->apiServerKit.translator);
	wo->serverKitContext->libev = wo->bgloop->safe;
	wo->serverKitContext->libuv = wo->bgloop->libuv_loop;
	wo->serverKitContext->initialize();

	UPDATE_TRACE_POINT();
	Json::Value apiServerConfig = watchdogConfig->inspectEffectiveValues();
	apiServerConfig["fd_passing_password"] = wo->fdPassingPassword;
	apiServerConfig["authorizations"] = wo->watchdogApiServerAuthorizations;
	wo->apiServer = new ApiServer::ApiServer(
		wo->serverKitContext,
		watchdogSchema->apiServer.schema,
		apiServerConfig,
		watchdogSchema->apiServer.translator);
	wo->apiServer->exitEvent = &wo->exitEvent;
	wo->apiServer->initialize();
	for (unsigned int i = 0; i < wo->watchdogApiServerAddresses.size(); i++) {
		wo->apiServer->listen(wo->apiServerFds[i]);
	}
}

static void
startAgents(const WorkingObjectsPtr &wo, vector<AgentWatcherPtr> &watchers) {
	TRACE_POINT();
	foreach (AgentWatcherPtr watcher, watchers) {
		P_DEBUG("Starting agent: " << watcher->name());
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
					P_CRITICAL("ERROR: " << e2->what() << "\n" << e2->backtrace());
				} else {
					P_CRITICAL("ERROR: " << e.what());
				}
			}
			forceAllAgentsShutdown(wo, watchers);
			cleanup(wo);
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
			cleanup(wo);
			exit(1);
		}
		// Allow other exceptions to propagate and crash the watchdog.
	}
}

static void
reportStartupResult(const WorkingObjectsPtr &wo, const vector<AgentWatcherPtr> &watchers) {
	TRACE_POINT();
	Json::Value report;

	report["instance_dir"] = wo->instanceDir->getPath();

	foreach (AgentWatcherPtr watcher, watchers) {
		watcher->reportAgentStartupResult(report);
	}

	if (feedbackFdAvailable()) {
		writeArrayMessage(FEEDBACK_FD, "Agents information", NULL);
		writeScalarMessage(FEEDBACK_FD, report.toStyledString());
	}

	if (wo->startupReportFile != -1) {
		string str = report.toStyledString();
		writeExact(wo->startupReportFile, str.data(), str.size());
		close(wo->startupReportFile);
		P_LOG_FILE_DESCRIPTOR_CLOSE(wo->startupReportFile);
		wo->startupReportFile = -1;
	}
}

static void
finalizeInstanceDir(const WorkingObjectsPtr &wo) {
	TRACE_POINT();
	#ifdef HAVE_FLOCK
		if (flock(wo->lockFile, LOCK_EX) == -1) {
			int e = errno;
			throw SystemException("Cannot obtain exclusive lock on the "
				"instance directory lock file", e);
		}
	#endif
	wo->instanceDir->finalizeCreation();
}

static void
cleanup(const WorkingObjectsPtr &wo) {
	TRACE_POINT();

	// We need to call destroy() explicitly because of circular references.
	if (wo->instanceDir != NULL && wo->instanceDir->isOwner()) {
		wo->instanceDir->destroy();
		wo->instanceDir.reset();
	}

	killCleanupPids(wo);
	deletePidFile(wo);
}

int
watchdogMain(int argc, char *argv[]) {
	WorkingObjectsPtr wo;

	initializeBareEssentials(argc, argv, wo);
	P_NOTICE("Starting " SHORT_PROGRAM_NAME " watchdog...");
	InstanceDirToucherPtr instanceDirToucher;
	vector<AgentWatcherPtr> watchers;
	uid_t uidBeforeLoweringPrivilege = geteuid();

	try {
		TRACE_POINT();
		maybeSetsid();
		maybeDaemonize();
		createPidFile();
		openStartupReportFile(wo);
		chdirToTmpDir();
		lowerPrivilege();
		initializeWorkingObjects(wo, instanceDirToucher, uidBeforeLoweringPrivilege);
		initializeAgentWatchers(wo, watchers);
		initializeApiServer(wo);
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
				P_CRITICAL("ERROR: " << e2->what() << "\n" << e2->backtrace());
			} else {
				P_CRITICAL("ERROR: " << e.what());
			}
		}
		if (wo != NULL) {
			cleanup(wo);
		}
		return 1;
	}
	// Allow other exceptions to propagate and crash the watchdog.

	try {
		TRACE_POINT();
		startAgents(wo, watchers);
		beginWatchingAgents(wo, watchers);
		reportStartupResult(wo, watchers);
		finalizeInstanceDir(wo);
		P_INFO("All " PROGRAM_NAME " agents started!");
		UPDATE_TRACE_POINT();
		runHookScriptAndThrowOnError("after_watchdog_initialization");

		UPDATE_TRACE_POINT();
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		bool shouldExitGracefully = waitForStarterProcessOrWatchers(wo, watchers);
		if (shouldExitGracefully) {
			/* Fork a child process which cleans up all the agent processes in
			 * the background and exit this watchdog process so that we don't block
			 * the web server.
			 */
			P_DEBUG("Web server exited gracefully; gracefully shutting down all agents...");
		} else {
			P_DEBUG("Web server did not exit gracefully, forcing shutdown of all agents...");
		}
		UPDATE_TRACE_POINT();
		runHookScriptAndThrowOnError("before_watchdog_shutdown");
		UPDATE_TRACE_POINT();
		AgentWatcher::stopWatching(watchers);
		if (shouldExitGracefully) {
			UPDATE_TRACE_POINT();
			cleanupAgentsInBackground(wo, watchers, argv);
			// Child process will call cleanup()
		} else {
			UPDATE_TRACE_POINT();
			forceAllAgentsShutdown(wo, watchers);
			cleanup(wo);
		}
		UPDATE_TRACE_POINT();
		runHookScriptAndThrowOnError("after_watchdog_shutdown");

		return shouldExitGracefully ? 0 : 1;
	} catch (const tracable_exception &e) {
		P_CRITICAL("ERROR: " << e.what() << "\n" << e.backtrace());
		cleanup(wo);
		return 1;
	}
}
