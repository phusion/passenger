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
#ifndef _PASSENGER_AGENTS_STARTER_HPP_
#define _PASSENGER_AGENTS_STARTER_HPP_


#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
	extern "C" {
#endif

typedef enum {
	AS_APACHE,
	AS_NGINX
} PP_AgentsStarterType;

typedef void PP_AgentsStarter;
typedef void PP_VariantMap;
typedef void (*PP_AfterForkCallback)(void *);

PP_VariantMap *pp_variant_map_new();
void pp_variant_map_set(PP_VariantMap *m,
	const char *name,
	const char *value,
	unsigned int value_len);
void pp_variant_map_set2(PP_VariantMap *m,
	const char *name,
	unsigned int name_len,
	const char *value,
	unsigned int value_len);
void pp_variant_map_set_int(PP_VariantMap *m,
	const char *name,
	int value);
void pp_variant_map_set_bool(PP_VariantMap *m,
	const char *name,
	int value);
void pp_variant_map_set_strset(PP_VariantMap *m,
	const char *name,
	const char **strs,
	unsigned int count);
void pp_variant_map_free(PP_VariantMap *m);

PP_AgentsStarter *pp_agents_starter_new(PP_AgentsStarterType type,
	char **error_message);
int pp_agents_starter_start(PP_AgentsStarter *as,
	const char *passengerRoot,
	PP_VariantMap *params,
	const PP_AfterForkCallback afterFork,
	void *callbackArgument,
	char **errorMessage);
const char *pp_agents_starter_get_request_socket_filename(PP_AgentsStarter *as, unsigned int *size);
const char *pp_agents_starter_get_request_socket_password(PP_AgentsStarter *as, unsigned int *size);
const char *pp_agents_starter_get_server_instance_dir(PP_AgentsStarter *as);
const char *pp_agents_starter_get_generation_dir(PP_AgentsStarter *as);
pid_t       pp_agents_starter_get_pid(PP_AgentsStarter *as);
void        pp_agents_starter_detach(PP_AgentsStarter *as);
void        pp_agents_starter_free(PP_AgentsStarter *as);

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

#include <Constants.h>
#include <FileDescriptor.h>
#include <MessageClient.h>
#include <ServerInstanceDir.h>
#include <Exceptions.h>
#include <ResourceLocator.h>
#include <Logging.h>
#include <Utils.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/Timer.h>
#include <Utils/ScopeGuard.h>
#include <Utils/VariantMap.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

/**
 * Utility class for starting various Phusion Passenger agents through the watchdog.
 */
class AgentsStarter {
private:
	PP_AgentsStarterType type;

	/** The watchdog's PID. Equals 0 if the watchdog hasn't been started yet
	 * or if detach() is called. */
	pid_t pid;

	/******* Information about the started services. Only valid when pid != 0. *******/

	 /** The watchdog's feedback file descriptor. */
	FileDescriptor feedbackFd;

	/** The helper agent's request socket filename and its password. This socket
	 * is for serving SCGI requests. */
	string requestSocketFilename;
	string requestSocketPassword;

	/** The socket on which the helper agent listens for administration commands,
	 * and the corresponding password for the "web_server" account, which has the
	 * authorization to shutdown the helper agent.
	 */
	string helperAgentAdminSocketAddress;
	string helperAgentExitPassword;

	/** The logging agent's socket address and its password. */
	string loggingSocketAddress;
	string loggingSocketPassword;

	/** The server instance dir and generation dir of the agents. */
	ServerInstanceDirPtr serverInstanceDir;
	/** The generation dir of the agents. */
	ServerInstanceDir::GenerationPtr generation;

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
				fprintf(stderr, "Passenger AgentsStarter: dup2() failed: %s (%d)\n",
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
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		int ret, status;

		/* Upon noticing that something went wrong, the watchdog
		 * or its subprocesses might still be writing out an error
		 * report, so we wait a while before killing the watchdog.
		 */
		ret = timedWaitPid(pid, &status, 5000);
		if (ret == 0) {
			/* Looks like the watchdog didn't crash and is still running. */
			throw RuntimeException(
				"Unable to start the Phusion Passenger watchdog: "
				"it froze during startup and reported an unknown error");
		} else if (ret != -1 && WIFSIGNALED(status)) {
			/* Looks like a crash which caused a signal. */
			pid = -1;
			throw RuntimeException(
				"Unable to start the Phusion Passenger watchdog: "
				"it seems to have been killed with signal " +
				getSignalName(WTERMSIG(status)) + " during startup");
		} else if (ret == -1) {
			/* Looks like it exited for a different reason and has no exit code. */
			pid = -1;
			throw RuntimeException(
				"Unable to start the Phusion Passenger watchdog: "
				"it seems to have crashed during startup for an unknown reason");
		} else {
			/* Looks like it exited for a different reason, but has an exit code. */
			pid = -1;
			throw RuntimeException(
				"Unable to start the Phusion Passenger watchdog: "
				"it seems to have crashed during startup for an unknown reason, "
				"with exit code " + toString(WEXITSTATUS(status)));
		}
	}

	static void killProcessGroupAndWait(pid_t *pid, unsigned long long timeout = 0) {
		if (*pid != -1 && (timeout == 0 || timedWaitPid(*pid, NULL, timeout) <= 0)) {
			this_thread::disable_syscall_interruption dsi;
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
		Timer timer;
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

	/**
	 * Gracefully shutdown an agent process by sending an exit command to its socket.
	 * Returns whether the agent has successfully processed the exit command.
	 * Any exceptions are caught and will cause false to be returned.
	 */
	bool gracefullyShutdownAgent(const string &address, const string &username,
		const string &password)
	{
		try {
			MessageClient client;
			vector<string> args;

			client.connect(address, username, password);
			client.write("exit", NULL);
			return client.read(args) && args[0] == "Passed security" &&
			       client.read(args) && args[0] == "exit command received";
		} catch (const SystemException &) {
		} catch (const IOException &) {
		} catch (const SecurityException &) {
		}
		return false;
	}

public:
	/**
	 * Construct a AgentsStarter object. The watchdog and the agents
	 * aren't started yet until you call start().
	 *
	 * @param type Whether one wants to start the Apache or the Nginx helper agent.
	 */
	AgentsStarter(PP_AgentsStarterType type) {
		this->type = type;
		pid = 0;
	}

	~AgentsStarter() {
		if (pid != 0) {
			this_thread::disable_syscall_interruption dsi;
			bool cleanShutdown = gracefullyShutdownAgent(helperAgentAdminSocketAddress,
				"_web_server", helperAgentExitPassword);
			cleanShutdown = cleanShutdown &&
				gracefullyShutdownAgent(loggingSocketAddress,
					"logging", loggingSocketPassword);

			/* Send a message down the feedback fd to tell the watchdog
			 * Whether this is a clean shutdown. Closing the fd without
			 * sending anything also indicates an unclean shutdown,
			 * but we send a byte anyway in case there are other processes
			 * who have the fd open.
			 */
			if (cleanShutdown) {
				syscalls::write(feedbackFd, "c", 1);
			} else {
				syscalls::write(feedbackFd, "u", 1);
			}

			/* If we failed to send an exit command to one of the agents then we have
			 * to forcefully kill all agents now because otherwise one of them might
			 * never exit. We do this by closing the feedback fd without sending a
			 * random byte, to indicate that this is an abnormal shutdown. The watchdog
			 * will then kill all agents.
			 */

			feedbackFd.close();
			syscalls::waitpid(pid, NULL, 0);
		}
	}

	/**
	 * Returns the type as was passed to the constructor.
	 */
	PP_AgentsStarterType getType() const {
		return type;
	}

	/**
	 * Returns the watchdog's PID. Equals 0 if the watchdog hasn't been started yet
	 * or if detach() is called.
	 */
	pid_t getPid() const {
		return pid;
	}

	// The 'const string &' here is on purpose. The C getter functions
	// return the string pointer directly.
	const string &getRequestSocketFilename() const {
		return requestSocketFilename;
	}

	const string &getRequestSocketPassword() const {
		return requestSocketPassword;
	}

	string getHelperAgentAdminSocketFilename() const {
		return parseUnixSocketAddress(helperAgentAdminSocketAddress);
	}

	string getHelperAgentExitPassword() const {
		return helperAgentExitPassword;
	}

	string getLoggingSocketAddress() const {
		return loggingSocketAddress;
	}

	string getLoggingSocketPassword() const {
		return loggingSocketPassword;
	}

	ServerInstanceDirPtr getServerInstanceDir() const {
		return serverInstanceDir;
	}

	ServerInstanceDir::GenerationPtr getGeneration() const {
		return generation;
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
		const VariantMap &extraParams = VariantMap(),
		const boost::function<void ()> &afterFork = boost::function<void ()>())
	{
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		ResourceLocator locator(passengerRoot);
		string watchdogFilename = locator.getAgentsDir() + "/PassengerWatchdog";
		SocketPair fds;
		int e;
		pid_t pid;

		VariantMap params;
		params
			.set    ("web_server_type", type == AS_APACHE ? "apache" : "nginx")
			.setPid ("web_server_pid",  getpid())
			.set    ("web_server_passenger_version", PASSENGER_VERSION)
			.set    ("passenger_root",  passengerRoot)
			.setInt ("log_level",       getLogLevel())
			.set    ("temp_dir",        getSystemTempDir());
		extraParams.addTo(params);

		fds = createUnixSocketPair();
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
			syscalls::close(fds[0]);
			installFeedbackFd(fds[1]);
			closeAllFileDescriptors(FEEDBACK_FD);

			if (afterFork) {
				afterFork();
			}

			execl(watchdogFilename.c_str(), "PassengerWatchdog", (char *) 0);
			e = errno;
			try {
				writeArrayMessage(FEEDBACK_FD,
					"exec error",
					toString(e).c_str(),
					NULL);
				_exit(1);
			} catch (...) {
				fprintf(stderr, "Passenger AgentsStarter: could not execute %s: %s (%d)\n",
					watchdogFilename.c_str(), strerror(e), e);
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

			ScopeGuard guard(boost::bind(&AgentsStarter::killProcessGroupAndWait, &pid, 0));
			fds[1].close();


			/****** Send arguments to watchdog through the feedback channel ******/

			UPDATE_TRACE_POINT();
			/* Here we don't care about EPIPE and ECONNRESET errors. The watchdog
			 * could have sent an error message over the feedback fd without
			 * reading the arguments. We'll notice that later.
			 */
			try {
				params.writeToFd(feedbackFd);
			} catch (const SystemException &e) {
				if (e.code() != EPIPE && e.code() != ECONNRESET) {
					inspectWatchdogCrashReason(pid);
				}
			}


			/****** Read agents information report ******/

			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			UPDATE_TRACE_POINT();

			try {
				result = readArrayMessage(feedbackFd, args);
			} catch (const SystemException &ex) {
				if (ex.code() == ECONNRESET) {
					inspectWatchdogCrashReason(pid);
				} else {
					killProcessGroupAndWait(&pid, 5000);
					guard.clear();
					throw SystemException("Unable to start the Phusion Passenger watchdog: "
						"unable to read its startup information report",
						ex.code());
				}
			}
			if (!result) {
				UPDATE_TRACE_POINT();
				inspectWatchdogCrashReason(pid);
			}

			if (args[0] == "Agents information") {
				if ((args.size() - 1) % 2 != 0) {
					throw RuntimeException("Unable to start the Phusion Passenger watchdog "
						"because it sent an invalid startup information report (the number "
						"of items is not an even number)");
				}

				VariantMap info;
				for (unsigned i = 1; i < args.size(); i += 2) {
					const string &key = args[i];
					const string &value = args[i + 1];
					info.set(key, value);
				}

				this->pid        = pid;
				this->feedbackFd = feedbackFd;
				requestSocketFilename   = info.get("request_socket_filename");
				requestSocketPassword   = info.get("request_socket_password");
				helperAgentAdminSocketAddress = info.get("helper_agent_admin_socket_address");
				helperAgentExitPassword       = info.get("helper_agent_exit_password");
				serverInstanceDir = boost::make_shared<ServerInstanceDir>(info.get("server_instance_dir"), false);
				generation        = serverInstanceDir->getGeneration(info.getInt("generation"));
				loggingSocketAddress  = info.get("logging_socket_address");
				loggingSocketPassword = info.get("logging_socket_password");
				guard.clear();
			} else if (args[0] == "Watchdog startup error") {
				killProcessGroupAndWait(&pid, 5000);
				guard.clear();
				throw RuntimeException("Unable to start the Phusion Passenger watchdog "
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
					string passengerRootConfig;
					string docURL;
					if (type == AS_APACHE) {
						passengerRootConfig = "PassengerRoot";
						docURL = APACHE2_DOC_URL "#PassengerRoot";
					} else {
						passengerRootConfig = "passenger_root";
						docURL = NGINX_DOC_URL "#PassengerRoot";
					}
					throw RuntimeException("Unable to start the Phusion Passenger watchdog "
						"because its executable (" + watchdogFilename + ") does "
						"not exist. This probably means that your Phusion Passenger "
						"installation is broken or incomplete, or that your '" +
						passengerRootConfig + "' directive is set to the wrong value. "
						"Please reinstall Phusion Passenger or fix your '" +
						passengerRootConfig + "' directive, whichever is applicable. "
						"To learn how to fix '" + passengerRootConfig + "', please read " +
						docURL);
				} else {
					throw SystemException("Unable to start the Phusion Passenger watchdog (" +
						watchdogFilename + ")", e);
				}
			} else {
				UPDATE_TRACE_POINT();
				killProcessGroupAndWait(&pid, 5000);
				guard.clear();
				throw RuntimeException("The Phusion Passenger watchdog sent an unknown feedback message '" + args[0] + "'");
			}
		}
	}

	/**
	 * Close any file descriptors that this object has, and make it so that the destructor
	 * doesn't try to shut down the agents.
	 *
	 * @post getPid() == 0
	 */
	void detach() {
		feedbackFd.close();
		pid = 0;
	}
};

} // namespace Passenger

#endif /* __cplusplus */

#endif /* _PASSENGER_AGENTS_STARTER_HPP_ */
