/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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

#include <boost/function.hpp>
#include <oxt/system_calls.hpp>
#include <string>
#include <vector>

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "FileDescriptor.h"
#include "MessageChannel.h"
#include "MessageClient.h"
#include "Base64.h"
#include "ServerInstanceDir.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

/**
 * Utility class for starting various Phusion Passenger agents through the watchdog.
 */
class AgentsStarter {
public:
	enum Type {
		APACHE,
		NGINX
	};
	
private:
	/** The watchdog's PID. Equals 0 if the watchdog hasn't been started yet
	 * or if detach() is called. */
	pid_t pid;
	
	Type type;
	
	/** The watchdog's feedback file descriptor. Only valid if pid != 0. */
	FileDescriptor feedbackFd;
	
	/**
	 * The helper server's request socket filename. This socket only exists
	 * for the Nginx helper server, and it's for serving SCGI requests.
	 *
	 * Only valid if pid != 0.
	 */
	string requestSocketFilename;
	
	/**
	 * A password for connecting to the request socket. Only valid if pid != 0.
	 */
	string requestSocketPassword;
	
	/**
	 * The helper server's message server socket filename, on which e.g. the
	 * application pool server is listening. Only valid if pid != 0.
	 *
	 * The application pool server is available through the account "_web_server".
	 */
	string messageSocketFilename;
	
	/**
	 * A password for the message server socket. The associated username is "_web_server".
	 *
	 * Only valid if pid != 0.
	 */
	string messageSocketPassword;
	
	string loggingSocketFilename;
	string loggingSocketPassword;
	
	/**
	 * The server instance dir of the helper server. Only valid if pid != 0.
	 */
	ServerInstanceDirPtr serverInstanceDir;
	
	/**
	 * The generation dir of the helper server. Only valid if pid != 0.
	 */
	ServerInstanceDir::GenerationPtr generation;
	
	static void
	killAndWait(pid_t pid) {
		this_thread::disable_syscall_interruption dsi;
		syscalls::kill(pid, SIGKILL);
		syscalls::waitpid(pid, NULL, 0);
	}
	
	/**
	 * Gracefully shutdown an agent process by sending an exit command to its socket.
	 * Returns whether the agent has successfully processed the exit command.
	 * Any exceptions are caught and will cause false to be returned.
	 */
	bool gracefullyShutdownAgent(const string &socketFilename, const string &username,
	                             const string &password
	) {
		try {
			MessageClient client;
			vector<string> args;
			
			client.connect(socketFilename, username, password);
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
	 * Construct a AgentsStarter object. The watchdog and the helper server
	 * aren't started yet until you call start().
	 *
	 * @param type Whether one wants to start the Apache or the Nginx helper server.
	 */
	AgentsStarter(Type type) {
		pid = 0;
		this->type = type;
	}
	
	~AgentsStarter() {
		if (pid != 0) {
			this_thread::disable_syscall_interruption dsi;
			bool cleanShutdown =
				   gracefullyShutdownAgent(messageSocketFilename,
					"_web_server", messageSocketPassword)
				&& gracefullyShutdownAgent(loggingSocketFilename,
					"logging", loggingSocketPassword);
			
			if (cleanShutdown) {
				// Send a single random byte to tell the watchdog that this
				// is a normal shutdown.
				syscalls::write(feedbackFd, "x", 1);
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
	Type getType() const {
		return type;
	}
	
	/**
	 * Returns the watchdog's PID. Equals 0 if the watchdog hasn't been started yet
	 * or if detach() is called.
	 */
	pid_t getPid() const {
		return pid;
	}
	
	/**
	 * The helper server's request socket filename, on which it's listening
	 * for SCGI requests.
	 *
	 * @pre getPid() != 0 && getType() == NGINX
	 */
	string getRequestSocketFilename() const {
		return requestSocketFilename;
	}
	
	/**
	 * Returns the password for connecting to the request socket.
	 *
	 * @pre getPid() != 0 && getType() == NGINX
	 */
	string getRequestSocketPassword() const {
		return requestSocketPassword;
	}
	
	string getMessageSocketFilename() const {
		return messageSocketFilename;
	}
	
	string getMessageSocketPassword() const {
		return messageSocketPassword;
	}
	
	string getLoggingSocketFilename() const {
		return loggingSocketFilename;
	}
	
	string getLoggingSocketPassword() const {
		return loggingSocketPassword;
	}
	
	/**
	 * Returns the server instance dir of the helper server.
	 *
	 * @pre getPid() != 0
	 */
	ServerInstanceDirPtr getServerInstanceDir() const {
		return serverInstanceDir;
	}
	
	/**
	 * Returns the generation dir of the helper server.
	 *
	 * @pre getPid() != 0
	 */
	ServerInstanceDir::GenerationPtr getGeneration() const {
		return generation;
	}
	
	/**
	 * Start the helper server through the watchdog, with the given parameters.
	 *
	 * @throws SystemException Something went wrong.
	 * @throws IOException Something went wrong while communicating with the
	 *                     helper server during its initialization phase.
	 * @throws RuntimeException Something went wrong.
	 */
	void start(unsigned int logLevel,
	           pid_t webServerPid, const string &tempDir,
	           bool userSwitching, const string &defaultUser, uid_t workerUid, gid_t workerGid,
	           const string &passengerRoot, const string &rubyCommand,
	           unsigned int maxPoolSize, unsigned int maxInstancesPerApp,
	           unsigned int poolIdleTime,
	           const string &monitoringLogDir,
	           const function<void ()> &afterFork = function<void ()>())
	{
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		int fds[2], e, ret;
		pid_t pid;
		string theTempDir;
		string watchdogFilename;
		
		watchdogFilename = passengerRoot + "/ext/common/PassengerWatchdog";
		if (tempDir.empty()) {
			theTempDir = getSystemTempDir();
		}
		
		if (syscalls::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			int e = errno;
			throw SystemException("Cannot create a Unix socket pair", e);
		}
		
		pid = syscalls::fork();
		if (pid == 0) {
			// Child
			long max_fds, i;
			
			// Make sure the feedback fd is 3 and close all file descriptors
			// except stdin, stdout, stderr and 3.
			syscalls::close(fds[0]);
			
			if (fds[1] != 3) {
				if (syscalls::dup2(fds[1], 3) == -1) {
					e = errno;
					try {
						MessageChannel(fds[1]).write("system error",
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
			max_fds = sysconf(_SC_OPEN_MAX);
			for (i = 4; i < max_fds; i++) {
				if (i != fds[1]) {
					syscalls::close(i);
				}
			}
			
			if (afterFork) {
				afterFork();
			}
			
			execl(watchdogFilename.c_str(),
				"PassengerWatchdog",
				type == APACHE ? "apache" : "nginx",
				toString(logLevel).c_str(),
				"3",  // feedback fd
				toString(webServerPid).c_str(),
				theTempDir.c_str(),
				userSwitching ? "true" : "false",
				defaultUser.c_str(),
				toString(workerUid).c_str(),
				toString(workerGid).c_str(),
				passengerRoot.c_str(),
				rubyCommand.c_str(),
				toString(maxPoolSize).c_str(),
				toString(maxInstancesPerApp).c_str(),
				toString(poolIdleTime).c_str(),
				monitoringLogDir.c_str(),
				(char *) 0);
			e = errno;
			try {
				MessageChannel(3).write("exec error", toString(e).c_str(), NULL);
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
			syscalls::close(fds[0]);
			syscalls::close(fds[1]);
			throw SystemException("Cannot fork a new process", e);
		} else {
			// Parent
			UPDATE_TRACE_POINT();
			FileDescriptor feedbackFd(fds[0]);
			MessageChannel feedbackChannel(fds[0]);
			vector<string> args;
			bool allAgentsStarted;
			int status;
			
			ServerInstanceDirPtr serverInstanceDir;
			ServerInstanceDir::GenerationPtr generation;
			
			syscalls::close(fds[1]);
			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			
			/****** Read basic startup information ******/
			
			try {
				if (!feedbackChannel.read(args)) {
					this_thread::disable_interruption di2;
					this_thread::disable_syscall_interruption dsi2;
					
					/* The feedback fd was closed for an unknown reason.
					 * Did the watchdog crash?
					 */
					ret = syscalls::waitpid(pid, &status, WNOHANG);
					if (ret == 0) {
						/* Doesn't look like it; it seems it's still running.
						 * We can't do anything without proper feedback so kill
						 * the watchdog and throw an exception.
						 */
						killAndWait(pid);
						throw RuntimeException("Unable to start the Phusion Passenger watchdog: "
							"an unknown error occurred during its startup");
					} else if (ret != -1 && WIFSIGNALED(status)) {
						/* Looks like a crash which caused a signal. */
						throw RuntimeException("Unable to start the Phusion Passenger watchdog: "
							"it seems to have been killed with signal " +
							getSignalName(WTERMSIG(status)) + " during startup");
					} else {
						/* Looks like it exited after detecting an error. */
						throw RuntimeException("Unable to start the Phusion Passenger watchdog: "
							"it seems to have crashed during startup for an unknown reason");
					}
				}
			} catch (const SystemException &ex) {
				killAndWait(pid);
				throw SystemException("Unable to start the Phusion Passenger watchdog: "
					"unable to read its startup information",
					ex.code());
			} catch (const RuntimeException &) {
				// Watchdog has already exited, no need to kill it.
				throw;
			} catch (...) {
				killAndWait(pid);
				throw;
			}
			
			if (args[0] == "Basic startup info") {
				if (args.size() == 3) {
					serverInstanceDir.reset(new ServerInstanceDir(args[1], false));
					generation = serverInstanceDir->getGeneration(atoi(args[2]));
				} else {
					killAndWait(pid);
					throw IOException("Unable to start the Phusion Passenger watchdog: "
						"it returned an invalid basic startup information message");
				}
			} else if (args[0] == "Watchdog startup error") {
				syscalls::waitpid(pid, NULL, 0);
				throw RuntimeException("Unable to start the Phusion Passenger watchdog "
					"because it encountered the following error during startup: " +
					args[1]);
			} else if (args[0] == "system error") {
				killAndWait(pid);
				throw SystemException(args[1], atoi(args[2]));
			} else if (args[0] == "exec error") {
				killAndWait(pid);
				throw SystemException("Unable to start the Phusion Passenger watchdog (" +
					watchdogFilename + ")", atoi(args[1]));
			}
			
			/****** Read agents startup information ******/
			
			UPDATE_TRACE_POINT();
			allAgentsStarted = false;
			
			while (!allAgentsStarted) {
				try {
					UPDATE_TRACE_POINT();
					if (!feedbackChannel.read(args)) {
						this_thread::disable_interruption di2;
						this_thread::disable_syscall_interruption dsi2;
						
						/* The feedback fd was closed for an unknown reason.
						 * Did the watchdog crash?
						 */
						ret = syscalls::waitpid(pid, &status, WNOHANG);
						if (ret == 0) {
							/* Doesn't look like it; it seems it's still running.
							 * We can't do anything without proper feedback so kill
							 * the watchdo and throw an exception.
							 */
							killAndWait(pid);
							throw RuntimeException("Unable to start the Phusion Passenger watchdog: "
								"an unknown error occurred during its startup");
						} else if (ret != -1 && WIFSIGNALED(status)) {
							/* Looks like a crash which caused a signal. */
							throw RuntimeException("Unable to start the Phusion Passenger watchdog: "
								"it seems to have been killed with signal " +
								getSignalName(WTERMSIG(status)) + " during startup");
						} else {
							/* Looks like it exited after detecting an error. */
							throw RuntimeException("Unable to start the Phusion Passenger watchdog: "
								"it seems to have crashed during startup for an unknown reason");
						}
					}
				} catch (const SystemException &ex) {
					killAndWait(pid);
					throw SystemException("Unable to start the Phusion Passenger watchdog: "
						"unable to read all agent startup information",
						ex.code());
				} catch (const RuntimeException &) {
					// Watchdog has already exited, no need to kill it.
					throw;
				} catch (...) {
					killAndWait(pid);
					throw;
				}
				
				if (args[0] == "HelperServer info") {
					UPDATE_TRACE_POINT();
					if (args.size() == 5) {
						this->pid = pid;
						this->feedbackFd = feedbackFd;
						requestSocketFilename   = args[1];
						requestSocketPassword   = Base64::decode(args[2]);
						messageSocketFilename   = args[3];
						messageSocketPassword   = Base64::decode(args[4]);
						this->serverInstanceDir = serverInstanceDir;
						this->generation        = generation;
					} else {
						killAndWait(pid);
						throw IOException("Unable to start the Phusion Passenger watchdog: "
							"it returned an invalid initialization feedback message");
					}
				} else if (args[0] == "LoggingServer info") {
					UPDATE_TRACE_POINT();
					if (args.size() == 2) {
						loggingSocketFilename = generation->getPath() + "/logging.socket";
						loggingSocketPassword = Base64::decode(args[1]);
					} else {
						killAndWait(pid);
						throw IOException("Unable to start the Phusion Passenger watchdog: "
							"it returned an invalid initialization feedback message");
					}
				} else if (args[0] == "All agents started") {
					allAgentsStarted = true;
				} else {
					UPDATE_TRACE_POINT();
					killAndWait(pid);
					throw RuntimeException("One of the Passenger agents sent an unknown feedback message '" + args[0] + "'");
				}
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

#endif /* _PASSENGER_AGENTS_STARTER_HPP_ */
