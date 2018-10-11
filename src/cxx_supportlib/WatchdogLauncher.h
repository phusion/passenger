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
#ifndef _PASSENGER_WATCHDOG_LAUNCHER_HPsg
#define _PASSENGER_WATCHDOG_LAUNCHER_HPsg


#include <sys/types.h>
#include <unistd.h>
#include "JsonTools/CBindings.h"

#ifdef __cplusplus
	extern "C" {
#endif

typedef enum {
	IM_APACHE,
	IM_NGINX,
	IM_STANDALONE
} PsgIntegrationMode;

typedef void PsgWatchdogLauncher;
typedef void (*PsgAfterForkCallback)(void *, void *);


PsgWatchdogLauncher *psg_watchdog_launcher_new(PsgIntegrationMode mode,
	char **error_message);
int psg_watchdog_launcher_start(PsgWatchdogLauncher *launcher,
	const char *passengerRoot,
	PsgJsonValue *config,
	const PsgAfterForkCallback afterFork,
	void *callbackArgument,
	char **errorMessage);
const char *psg_watchdog_launcher_get_core_address(PsgWatchdogLauncher *launcher, unsigned int *size);
const char *psg_watchdog_launcher_get_core_password(PsgWatchdogLauncher *launcher, unsigned int *size);
const char *psg_watchdog_launcher_get_instance_dir(PsgWatchdogLauncher *launcher, unsigned int *size);
pid_t       psg_watchdog_launcher_get_pid(PsgWatchdogLauncher *launcher);
void        psg_watchdog_launcher_detach(PsgWatchdogLauncher *launcher);
void        psg_watchdog_launcher_free(PsgWatchdogLauncher *launcher);

#ifdef __cplusplus
	} /* extern "C" */
#endif


#ifdef __cplusplus

#include <boost/function.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <string>
#include <vector>
#include <set>

#include <signal.h>

#include <jsoncpp/json.h>

#include <Constants.h>
#include <FileDescriptor.h>
#include <Exceptions.h>
#include <ResourceLocator.h>
#include <LoggingKit/LoggingKit.h>
#include <LoggingKit/Context.h>
#include <ProcessManagement/Utils.h>
#include <Utils.h>
#include <IOTools/IOUtils.h>
#include <IOTools/MessageIO.h>
#include <Utils/Timer.h>
#include <Utils/ScopeGuard.h>
#include <Utils/ClassUtils.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

/**
 * Utility class for starting various the Passenger watchdog.
 */
class WatchdogLauncher {
	/**
	 * Whether the starter process is Apache, Nginx or
	 * Passenger Standalone.
	 */
	P_RO_PROPERTY(private, PsgIntegrationMode, IntegrationMode);

	/**
	 * The watchdog's PID. Equals 0 if the watchdog hasn't been started yet
	 * or if `detach()` is called.
	 */
	P_RO_PROPERTY(private, pid_t, Pid);

	// Note: the use of `CONST_REF` in the properties below is intentional.
	// The C getter functions return the string pointer directly.

	/**
	 * The address on which the Passenger core listens for HTTP requests,
	 * and the corresponding password.
	 *
	 * Only valid when `getPid() != 0`.
	 */
	P_RO_PROPERTY_CONST_REF(private, string, CoreAddress);
	P_RO_PROPERTY_CONST_REF(private, string, CorePassword);

	/**
	 * The path to the instance directory that the Watchdog has created.
	 *
	 * Only valid when `getPid() != 0`.
	 */
	P_RO_PROPERTY_CONST_REF(private, string, InstanceDir);

private:
	/** The watchdog's feedback file descriptor. */
	FileDescriptor feedbackFd;

	/**
	 * Safely dup2() the given file descriptor to 3 (FEEDBACK_FD).
	 */
	void installFeedbackFd(const FileDescriptor &fd) {
		if (fd != FEEDBACK_FD && syscalls::dup2(fd, FEEDBACK_FD) == -1) {
			int e = errno;
			try {
				writeArrayMessage(fd,
					"system error",
					"dup2() failed",
					toString(e).c_str(),
					NULL);
				_exit(1);
			} catch (...) {
				fprintf(stderr,
					"Passenger WatchdogLauncher: dup2() failed: %s (%d)\n",
					strerror(e), e);
				fflush(stderr);
				_exit(1);
			}
		}
	}

	/**
	 * Call this if the watchdog seems to have crashed. This function will try
	 * to determine whether the watchdog is still running, whether it crashed
	 * with a signal, etc. If it has detected that the watchdog is no longer running
	 * then it will set `pid` to -1.
	 */
	void inspectWatchdogCrashReason(pid_t &pid) {
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		int ret, status;

		/* Upon noticing that something went wrong, the watchdog
		 * or its subprocesses might still be writing out an error
		 * report, so we wait a while before killing the watchdog.
		 */
		ret = timedWaitPid(pid, &status, 5000);
		if (ret == 0) {
			/* Looks like the watchdog didn't crash and is still running. */
			throw RuntimeException(
				"Unable to start the " PROGRAM_NAME " watchdog: "
				"it froze during startup and reported an unknown error");
		} else if (ret != -1 && WIFSIGNALED(status)) {
			/* Looks like a crash which caused a signal. */
			pid = -1;
			throw RuntimeException(
				"Unable to start the " PROGRAM_NAME " watchdog: "
				"it seems to have been killed with signal " +
				getSignalName(WTERMSIG(status)) + " during startup");
		} else if (ret == -1) {
			/* Looks like it exited for a different reason and has no exit code. */
			pid = -1;
			throw RuntimeException(
				"Unable to start the " PROGRAM_NAME " watchdog: "
				"it seems to have crashed during startup for an unknown reason");
		} else {
			/* Looks like it exited for a different reason, but has an exit code. */
			pid = -1;
			throw RuntimeException(
				"Unable to start the " PROGRAM_NAME " watchdog: "
				"it seems to have crashed during startup for an unknown reason, "
				"with exit code " + toString(WEXITSTATUS(status)));
		}
	}

	void throwEnrichedWatchdogFailReason(const ResourceLocator &locator, const string &simpleReason) {
		if (mIntegrationMode == IM_STANDALONE) {
			throw RuntimeException("Unable to start " PROGRAM_NAME ": " + simpleReason +
				". This probably means that your " SHORT_PROGRAM_NAME
				" installation is broken or incomplete. Please try reinstalling " SHORT_PROGRAM_NAME);
		} else {
			string passengerRootConfig;
			string docURL;

			if (mIntegrationMode == IM_APACHE) {
				passengerRootConfig = "PassengerRoot";
				docURL = "https://www.phusionpassenger.com/library/config/apache/reference/#passengerroot";
			} else {
				passengerRootConfig = "passenger_root";
				docURL = "https://www.phusionpassenger.com/library/config/nginx/reference/#passenger_root";
			}

			string message = "Unable to start " PROGRAM_NAME ": " + simpleReason +
				". There may be different causes for this:\n\n"
				" - Your '" + passengerRootConfig + "' setting is set to the wrong value."
					" Please see " + docURL + " to learn how to fix the value.\n";
			if (!locator.getBuildSystemDir().empty()) {
				message.append(" - The " AGENT_EXE " binary is not compiled."
					" Please run this command to compile it: "
					+ locator.getBinDir() + "/passenger-config compile-agent\n");
			}
			message.append(" - Your " SHORT_PROGRAM_NAME " installation is broken or incomplete."
				" Please reinstall " SHORT_PROGRAM_NAME ".");
			throw RuntimeException(message);
		}
	}

	static void killProcessGroupAndWait(pid_t *pid, unsigned long long timeout = 0) {
		if (*pid != -1 && (timeout == 0 || timedWaitPid(*pid, NULL, timeout) <= 0)) {
			boost::this_thread::disable_syscall_interruption dsi;
			syscalls::killpg(*pid, SIGKILL);
			syscalls::waitpid(*pid, NULL, 0);
			*pid = -1;
		}
	}

	/**
	 * Behaves like `waitpid(pid, status, WNOHANG)`, but waits at most
	 * `timeout` miliseconds for the process to exit.
	 */
	static int timedWaitPid(pid_t pid, int *status, unsigned long long timeout) {
		Timer<SystemTime::GRAN_10MSEC> timer;
		int ret;

		do {
			ret = syscalls::waitpid(pid, status, WNOHANG);
			if (ret > 0 || ret == -1) {
				return ret;
			} else {
				syscalls::usleep(10000);
			}
		} while (timer.elapsed() < timeout);
		return 0; // timed out
	}

public:
	/**
	 * Construct a WatchdogLauncher object. The watchdog won't be started
	 * until you call `start()`.
	 */
	WatchdogLauncher(PsgIntegrationMode _integrationMode)
		: mIntegrationMode(_integrationMode),
		  mPid(0)
		{ }

	~WatchdogLauncher() {
		if (mPid != 0) {
			boost::this_thread::disable_syscall_interruption dsi;

			/* Send a message down the feedback fd to tell the watchdog
			 * that we're shutting down cleanly. Closing the fd without
			 * sending anything indicates an unclean shutdown.
			 */
			syscalls::write(feedbackFd, "c", 1);
			feedbackFd.close();
			syscalls::waitpid(mPid, NULL, 0);
		}
	}

	const char *getIntegrationModeString() const {
		switch (mIntegrationMode) {
		case IM_APACHE:
			return "apache";
		case IM_NGINX:
			return "nginx";
		case IM_STANDALONE:
			return "standalone";
		default:
			return "unknown";
		}
	}

	/**
	 * Start the agents through the watchdog.
	 *
	 * @throws SystemException Something went wrong.
	 * @throws IOException Something went wrong while communicating with one
	 *                     of the agents during its initialization phase.
	 * @throws RuntimeException Something went wrong.
	 */
	void start(const string &passengerRoot,
		const Json::Value &extraConfig = Json::Value(),
		const boost::function<void ()> &afterFork = boost::function<void ()>())
	{
		TRACE_POINT();
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		ResourceLocator locator(passengerRoot);

		string agentFilename;
		try {
			agentFilename = locator.findSupportBinary(AGENT_EXE);
		} catch (const Passenger::RuntimeException &e) {
			throwEnrichedWatchdogFailReason(locator, e.what());
		}
		SocketPair fds;
		int e;
		pid_t pid;
		Json::Value::const_iterator it;

		Json::Value config;
		config["web_server_control_process_pid"] = getpid();
		config["integration_mode"] = getIntegrationModeString();
		config["passenger_root"] = passengerRoot;
		config["log_level"] = (int) LoggingKit::getLevel();

		for (it = extraConfig.begin(); it != extraConfig.end(); it++) {
			config[it.name()] = *it;
		}

		fds = createUnixSocketPair(__FILE__, __LINE__);
		pid = syscalls::fork();
		if (pid == 0) {
			// Child

			/* Become the session leader so that Apache can't kill the
			 * watchdog with killpg() during shutdown, so that a
			 * Ctrl-C only affects the web server, and so that
			 * we can kill all of our subprocesses in a single killpg().
			 */
			setsid();

			/* We don't know how the web server or the environment affect
			 * signal handlers and the signal mask, so reset this stuff
			 * just in case. Also, we reset the signal handlers before
			 * closing all file descriptors, in order to prevent bugs
			 * like this: https://github.com/phusion/passenger/pull/97
			 */
			resetSignalHandlersAndMask();

			// Make sure the feedback fd is 3 and close all file descriptors
			// except stdin, stdout, stderr and 3.
			close(fds[0]);
			installFeedbackFd(fds[1]);

			setenv("PASSENGER_USE_FEEDBACK_FD", "true", 1);

			if (afterFork) {
				afterFork();
			}

			closeAllFileDescriptors(FEEDBACK_FD);

			execl(agentFilename.c_str(), AGENT_EXE, "watchdog",
				// Some extra space to allow the child process to change its process title.
				"                                                ",
				(char *) 0);
			e = errno;
			try {
				writeArrayMessage(FEEDBACK_FD,
					"exec error",
					toString(e).c_str(),
					NULL);
				_exit(1);
			} catch (...) {
				fprintf(stderr, "Passenger WatchdogLauncher: could not execute %s: %s (%d)\n",
					agentFilename.c_str(), strerror(e), e);
				fflush(stderr);
				_exit(1);
			}
		} else if (pid == -1) {
			// Error
			e = errno;
			throw SystemException("Cannot fork a new process", e);
		} else {
			// Parent
			UPDATE_TRACE_POINT();
			FileDescriptor feedbackFd = fds[0];
			vector<string> args;
			bool result = false;

			ScopeGuard guard(boost::bind(&WatchdogLauncher::killProcessGroupAndWait, &pid, 0));
			fds[1].close();
			P_LOG_FILE_DESCRIPTOR_PURPOSE(feedbackFd, "WatchdogLauncher: feedback FD");


			/****** Send arguments to watchdog through the feedback channel ******/

			UPDATE_TRACE_POINT();
			/* Here we don't care about EPIPE and ECONNRESET errors. The watchdog
			 * could have sent an error message over the feedback fd without
			 * reading the arguments. We'll notice that later.
			 */
			try {
				writeScalarMessage(feedbackFd, config.toStyledString());
			} catch (const SystemException &e) {
				if (e.code() != EPIPE && e.code() != ECONNRESET) {
					inspectWatchdogCrashReason(pid);
				}
			}


			/****** Read agents information report ******/

			boost::this_thread::restore_interruption ri(di);
			boost::this_thread::restore_syscall_interruption rsi(dsi);
			UPDATE_TRACE_POINT();

			try {
				result = readArrayMessage(feedbackFd, args);
			} catch (const SystemException &ex) {
				if (ex.code() == ECONNRESET) {
					inspectWatchdogCrashReason(pid);
				} else {
					killProcessGroupAndWait(&pid, 5000);
					guard.clear();
					throw SystemException("Unable to start the " PROGRAM_NAME " watchdog: "
						"unable to read its startup information report",
						ex.code());
				}
			}
			if (!result) {
				UPDATE_TRACE_POINT();
				inspectWatchdogCrashReason(pid);
			}

			if (args[0] == "Agents information") {
				UPDATE_TRACE_POINT();

				if (args.size() != 1) {
					throw RuntimeException("Unable to start the " PROGRAM_NAME " watchdog: "
						"it belongs to an incompatible version of " SHORT_PROGRAM_NAME
						". Please fully upgrade " SHORT_PROGRAM_NAME ".");
				}

				string jsonData;
				try {
					result = readScalarMessage(feedbackFd, jsonData);
				} catch (const SystemException &ex) {
					if (ex.code() == ECONNRESET) {
						inspectWatchdogCrashReason(pid);
					} else {
						killProcessGroupAndWait(&pid, 5000);
						guard.clear();
						throw SystemException("Unable to start the " PROGRAM_NAME " watchdog: "
							"unable to read its startup information report",
							ex.code());
					}
				}
				if (!result) {
					UPDATE_TRACE_POINT();
					inspectWatchdogCrashReason(pid);
				}

				Json::Value doc;
				Json::Reader reader;
				if (!reader.parse(jsonData, doc)) {
					throw RuntimeException("Unable to start the " PROGRAM_NAME " watchdog: "
						"unable to parse its startup information report as valid JSON: "
						+ reader.getFormattedErrorMessages() + "\n"
						"Raw data: \"" + cEscapeString(jsonData) + "\"");
				}

				mPid               = pid;
				this->feedbackFd   = feedbackFd;
				mCoreAddress       = doc["core_address"].asString();
				mCorePassword      = doc["core_password"].asString();
				mInstanceDir       = doc["instance_dir"].asString();
				guard.clear();
			} else if (args[0] == "Watchdog startup error") {
				killProcessGroupAndWait(&pid, 5000);
				guard.clear();
				throw RuntimeException("Unable to start the " PROGRAM_NAME " watchdog "
					"because it encountered the following error during startup: " +
					args[1]);
			} else if (args[0] == "system error") {
				killProcessGroupAndWait(&pid, 5000);
				guard.clear();
				throw SystemException(args[1], atoi(args[2]));
			} else if (args[0] == "exec error") {
				e = atoi(args[1]);
				killProcessGroupAndWait(&pid, 5000);
				guard.clear();
				if (e == ENOENT) {
					throwEnrichedWatchdogFailReason(locator, "Executable " + agentFilename + " not found.");
				} else {
					throw SystemException("Unable to start the " PROGRAM_NAME " watchdog (" +
						agentFilename + ")", e);
				}
			} else {
				UPDATE_TRACE_POINT();
				killProcessGroupAndWait(&pid, 5000);
				guard.clear();
				throw RuntimeException("The " PROGRAM_NAME " watchdog sent an unknown feedback message '"
					+ args[0] + "'");
			}
		}
	}

	/**
	 * Close any file descriptors that this object has, and make it so that
	 * the destructor doesn't try to shut down the watchdog.
	 *
	 * @post getPid() == 0
	 */
	void detach() {
		feedbackFd.close();
		mPid = 0;
	}
};

} // namespace Passenger

#endif /* __cplusplus */

#endif /* _PASSENGER_WATCHDOG_LAUNCHER_HPsg */
