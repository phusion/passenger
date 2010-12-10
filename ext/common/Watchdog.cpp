/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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

#include "Constants.h"
#include "AgentBase.h"
#include "ServerInstanceDir.h"
#include "FileDescriptor.h"
#include "MessageChannel.h"
#include "Constants.h"
#include "RandomGenerator.h"
#include "Logging.h"
#include "Exceptions.h"
#include "ResourceLocator.h"
#include "Utils.h"
#include "Utils/Base64.h"
#include "Utils/Timer.h"
#include "Utils/ScopeGuard.h"
#include "Utils/IOUtils.h"
#include "Utils/VariantMap.h"

using namespace std;
using namespace boost;
using namespace oxt;
using namespace Passenger;


/** The options that were passed to AgentsStarter. */
static VariantMap     agentsOptions;
static string         webServerType;        // "apache" or "nginx"
static unsigned int   logLevel;
static pid_t   webServerPid;
static string  tempDir;
static bool    userSwitching;
static string  defaultUser;
static string  defaultGroup;
static uid_t   webServerWorkerUid;
static gid_t   webServerWorkerGid;
static string  passengerRoot;
static string  rubyCommand;
static unsigned int maxPoolSize;
static unsigned int maxInstancesPerApp;
static unsigned int poolIdleTime;
static string  serializedPrestartURLs;

static ServerInstanceDirPtr serverInstanceDir;
static ServerInstanceDir::GenerationPtr generation;
static string loggingAgentAddress;
static string loggingAgentPassword;
static RandomGenerator *randomGenerator;
static EventFd *errorEvent;

#define REQUEST_SOCKET_PASSWORD_SIZE     64


/**
 * Abstract base class for watching agent processes.
 */
class AgentWatcher {
private:
	/** The watcher thread. */
	oxt::thread *thr;
	
	void threadMain() {
		try {
			pid_t pid, ret;
			int status;
			
			while (!this_thread::interruption_requested()) {
				lock.lock();
				pid = this->pid;
				lock.unlock();
				
				// Process can be started before the watcher thread is launched.
				if (pid == 0) {
					pid = start();
				}
				ret = syscalls::waitpid(pid, &status, 0);
				
				lock.lock();
				this->pid = 0;
				lock.unlock();
				
				this_thread::disable_interruption di;
				this_thread::disable_syscall_interruption dsi;
				if (ret == -1) {
					P_WARN(name() << " crashed or killed for "
						"an unknown reason, restarting it...");
				} else if (WIFEXITED(status)) {
					if (WEXITSTATUS(status) == 0) {
						/* When the web server is gracefully exiting, it will
						 * tell one or more agents to gracefully exit with exit
						 * status 0. If we see this then it means the watchdog
						 * is gracefully shutting down too and we should stop
						 * watching.
						 */
						return;
					} else {
						P_WARN(name() << " crashed with exit status " <<
							WEXITSTATUS(status) << ", restarting it...");
					}
				} else {
					P_WARN(name() << " crashed with signal " <<
						getSignalName(WTERMSIG(status)) <<
						", restarting it...");
				}
			}
		} catch (const boost::thread_interrupted &) {
		} catch (const tracable_exception &e) {
			lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = e.what();
			threadExceptionBacktrace = e.backtrace();
			errorEvent->notify();
		} catch (const std::exception &e) {
			lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = e.what();
			errorEvent->notify();
		} catch (...) {
			lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = "Unknown error";
			errorEvent->notify();
		}
	}
	
protected:
	/** PID of the process we're watching. 0 if no process is started at this time. */
	pid_t pid;
	
	/** If the watcher thread threw an uncaught exception then its information will
	 * be stored here so that the main thread can check whether a watcher encountered
	 * an error. These are empty strings if everything is OK.
	 */
	string threadExceptionMessage;
	string threadExceptionBacktrace;
	
	/** The agent process's feedback fd. */
	FileDescriptor feedbackFd;
	
	/**
	 * Lock for protecting the exchange of data between the main thread and
	 * the watcher thread.
	 */
	mutable boost::mutex lock;
	
	/**
	 * Returns the filename of the agent process's executable. This method may be
	 * called in a forked child process and may therefore not allocate memory.
	 */
	virtual string getExeFilename() const = 0;
	
	/**
	 * This method is to exec() the agent with the right arguments.
	 * It is called from within a forked child process, so don't do any dynamic
	 * memory allocations in here. It must also not throw any exceptions.
	 * It must also preserve the value of errno after exec() is called.
	 */
	virtual void execProgram() const {
		execl(getExeFilename().c_str(),
			getExeFilename().c_str(),
			"3",  // feedback fd
			(char *) 0);
	}
	
	/**
	 * This method is to send startup arguments to the agent process through
	 * the given file descriptor, which is the agent process's feedback fd.
	 * May throw arbitrary exceptions.
	 */
	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) = 0;
	
	/**
	 * This method is to process the startup info that the agent process has
	 * sent back. May throw arbitrary exceptions.
	 */
	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) = 0;
	
	/**
	 * Kill a process with SIGKILL, and attempt to kill its children too. 
	 * Then wait until it has quit.
	 */
	static void killAndWait(pid_t pid) {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		// If the process is a process group leader then killing the
		// group will likely kill all its child processes too.
		if (syscalls::killpg(pid, SIGKILL) == -1) {
			syscalls::kill(pid, SIGKILL);
		}
		syscalls::waitpid(pid, NULL, 0);
	}
	
	/**
	 * Behaves like <tt>waitpid(pid, status, WNOHANG)</tt>, but waits at most
	 * <em>timeout</em> miliseconds for the process to exit.
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
	
public:
	AgentWatcher() {
		thr = NULL;
		pid = 0;
	}
	
	virtual ~AgentWatcher() {
		delete thr;
	}
	
	/**
	 * Send the started agent process's startup information over the given channel,
	 * to the starter process. May throw arbitrary exceptions.
	 *
	 * @pre start() has been called and succeeded.
	 */
	virtual void sendStartupInfo(MessageChannel &channel) = 0;
	
	/** Returns the name of the agent that this class is watching. */
	virtual const char *name() const = 0;
	
	/**
	 * Starts the agent process. May throw arbitrary exceptions.
	 */
	virtual pid_t start() {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		string exeFilename = getExeFilename();
		SocketPair fds;
		int e, ret;
		pid_t pid;
		
		/* Create feedback fd for this agent process. We'll send some startup
		 * arguments to this agent process through this fd, and we'll receive
		 * startup information through it as well.
		 */
		fds = createUnixSocketPair();
		
		pid = syscalls::fork();
		if (pid == 0) {
			// Child
			
			/* Make sure file descriptor FEEDBACK_FD refers to the newly created
			 * feedback fd (fds[1]) and close all other file descriptors.
			 * In this child process we don't care about the original FEEDBACK_FD
			 * (which is the Watchdog's communication channel to the agents starter.)
			 *
			 * fds[1] is guaranteed to be != FEEDBACK_FD because the watchdog
			 * is started with FEEDBACK_FD already assigned.
			 */
			syscalls::close(fds[0]);
			
			if (syscalls::dup2(fds[1], FEEDBACK_FD) == -1) {
				/* Something went wrong, report error through feedback fd. */
				e = errno;
				try {
					MessageChannel(fds[1]).write("system error before exec",
						"dup2() failed",
						toString(e).c_str(),
						NULL);
					_exit(1);
				} catch (...) {
					fprintf(stderr, "Passenger Watchdog: dup2() failed: %s (%d)\n",
						strerror(e), e);
					fflush(stderr);
					_exit(1);
				}
			}
			
			closeAllFileDescriptors(FEEDBACK_FD);
			
			/* Become the process group leader so that the watchdog can kill the
			 * agent as well as all its descendant processes. */
			setpgid(getpid(), getpid());
			
			try {
				execProgram();
			} catch (...) {
				fprintf(stderr, "PassengerWatchdog: execProgram() threw an exception\n");
				fflush(stderr);
				_exit(1);
			}
			e = errno;
			try {
				MessageChannel(FEEDBACK_FD).write("exec error",
					toString(e).c_str(), NULL);
			} catch (...) {
				fprintf(stderr, "Passenger Watchdog: could not execute %s: %s (%d)\n",
					exeFilename.c_str(), strerror(e), e);
				fflush(stderr);
			}
			_exit(1);
		} else if (pid == -1) {
			// Error
			e = errno;
			throw SystemException("Cannot fork a new process", e);
		} else {
			// Parent
			FileDescriptor feedbackFd = fds[0];
			vector<string> args;
			
			fds[1].close();
			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			ScopeGuard failGuard(boost::bind(killAndWait, pid));
			
			/* Send startup arguments. Ignore EPIPE and ECONNRESET here
			 * because the child process might have sent an feedback message
			 * without reading startup arguments.
			 */
			try {
				sendStartupArguments(pid, feedbackFd);
			} catch (const SystemException &ex) {
				if (ex.code() != EPIPE && ex.code() != ECONNRESET) {
					throw SystemException(string("Unable to start the ") + name() +
						": an error occurred while sending startup arguments",
						ex.code());
				}
			}
			
			// Now read its feedback.
			try {
				ret = MessageChannel(feedbackFd).read(args);
			} catch (const SystemException &e) {
				if (e.code() == ECONNRESET) {
					ret = false;
				} else {
					throw SystemException(string("Unable to start the ") + name() +
						": unable to read its startup information",
						e.code());
				}
			}
			if (!ret) {
				this_thread::disable_interruption di2;
				this_thread::disable_syscall_interruption dsi2;
				int status;
				
				/* The feedback fd was prematurely closed for an unknown reason.
				 * Did the agent process crash?
				 *
				 * We use timedWaitPid() here because if the process crashed
				 * because of an uncaught exception, the file descriptor
				 * might be closed before the process has printed an error
				 * message, so we give it some time to print the error
				 * before we kill it.
				 */
				ret = timedWaitPid(pid, &status, 5000);
				if (ret == 0) {
					/* Doesn't look like it; it seems it's still running.
					 * We can't do anything without proper feedback so kill
					 * the agent process and throw an exception.
					 */
					failGuard.runNow();
					throw RuntimeException(string("Unable to start the ") + name() +
						": it froze and reported an unknown error during its startup");
				} else if (ret != -1 && WIFSIGNALED(status)) {
					/* Looks like a crash which caused a signal. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have been killed with signal " +
						getSignalName(WTERMSIG(status)) + " during startup");
				} else if (ret == -1) {
					/* Looks like it exited after detecting an error. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have crashed during startup for an unknown reason");
				} else {
					/* Looks like it exited after detecting an error, but has an exit code. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have crashed during startup for an unknown reason, "
						"with exit code " + toString(WEXITSTATUS(status)));
				}
			}
			
			if (args[0] == "system error before exec") {
				throw SystemException(string("Unable to start the ") + name() +
					": " + args[1], atoi(args[2]));
			} else if (args[0] == "exec error") {
				e = atoi(args[1]);
				if (e == ENOENT) {
					throw RuntimeException(string("Unable to start the ") + name() +
						" because its executable (" + getExeFilename() + ") "
						"doesn't exist. This probably means that your "
						"Phusion Passenger installation is broken or "
						"incomplete. Please reinstall Phusion Passenger");
				} else {
					throw SystemException(string("Unable to start the ") + name() +
						" because exec(\"" + getExeFilename() + "\") failed",
						atoi(args[1]));
				}
			} else if (!processStartupInfo(pid, feedbackFd, args)) {
				throw RuntimeException(string("The ") + name() +
					" sent an unknown startup info message '" +
					args[0] + "'");
			}
			
			lock_guard<boost::mutex> l(lock);
			this->feedbackFd = feedbackFd;
			this->pid = pid;
			failGuard.clear();
			return pid;
		}
	}
	
	/**
	 * Start watching the agent process.
	 *
	 * @pre start() has been called and succeeded.
	 * @pre This watcher isn't already watching.
	 * @throws RuntimeException If a precondition failed.
	 * @throws thread_interrupted
	 * @throws thread_resource_error
	 */
	virtual void startWatching() {
		lock_guard<boost::mutex> l(lock);
		if (pid == 0) {
			throw RuntimeException("start() hasn't been called yet");
		}
		if (thr != NULL) {
			throw RuntimeException("Already started watching.");
		}
		
		/* Don't make the stack any smaller, getpwnam() on OS
		 * X needs a lot of stack space.
		 */
		thr = new oxt::thread(boost::bind(&AgentWatcher::threadMain, this),
			name(), 64 * 1024);
	}
	
	static void stopWatching(vector<AgentWatcher *> &watchers) {
		vector<AgentWatcher *>::const_iterator it;
		oxt::thread *threads[watchers.size()];
		unsigned int i = 0;
		
		for (it = watchers.begin(); it != watchers.end(); it++, i++) {
			threads[i] = (*it)->thr;
		}
		
		oxt::thread::interrupt_and_join_multiple(threads, watchers.size());
	}
	
	/**
	 * Force the agent process to shut down. Returns true if it was shut down,
	 * or false if it wasn't started.
	 */
	virtual bool forceShutdown() {
		lock_guard<boost::mutex> l(lock);
		if (pid == 0) {
			return false;
		} else {
			killAndWait(pid);
			this->pid = 0;
			return true;
		}
	}
	
	/**
	 * If the watcher thread has encountered an error, then the error message
	 * will be stored here. If the error message is empty then it means
	 * everything is still OK.
	 */
	string getErrorMessage() const {
		lock_guard<boost::mutex> l(lock);
		return threadExceptionMessage;
	}
	
	/**
	 * The error backtrace, if applicable.
	 */
	string getErrorBacktrace() const {
		lock_guard<boost::mutex> l(lock);
		return threadExceptionBacktrace;
	}
	
	/**
	 * Returns the agent process feedback fd, or -1 if the agent process
	 * hasn't been started yet. Can be used to check whether this agent process
	 * has exited without using waitpid().
	 */
	const FileDescriptor getFeedbackFd() const {
		lock_guard<boost::mutex> l(lock);
		return feedbackFd;
	}
};


class HelperAgentWatcher: public AgentWatcher {
protected:
	string requestSocketFilename;
	string messageSocketFilename;
	string helperAgentFilename;
	string requestSocketPassword;
	string messageSocketPassword;
	
	virtual const char *name() const {
		return "Phusion Passenger helper agent";
	}
	
	virtual string getExeFilename() const {
		return helperAgentFilename;
	}
	
	virtual void execProgram() const {
		execl(helperAgentFilename.c_str(), "PassengerHelperAgent", (char *) 0);
	}
	
	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) {
		VariantMap options = agentsOptions;
		options.set("request_socket_password", Base64::encode(requestSocketPassword)).
			set("message_socket_password", Base64::encode(messageSocketPassword)).
			set("logging_agent_address", loggingAgentAddress).
			set("logging_agent_password", loggingAgentPassword);
		options.writeToFd(fd);
	}
	
	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) {
		if (args[0] == "initialized") {
			requestSocketFilename = args[1];
			messageSocketFilename = args[2];
			return true;
		} else {
			return false;
		}
	}
	
public:
	HelperAgentWatcher(const ResourceLocator &resourceLocator) {
		if (agentsOptions.get("web_server_type") == "apache") {
			helperAgentFilename = resourceLocator.getAgentsDir() + "/apache2/PassengerHelperAgent";
		} else {
			helperAgentFilename = resourceLocator.getAgentsDir() + "/nginx/PassengerHelperAgent";
		}
		requestSocketPassword = randomGenerator->generateByteString(REQUEST_SOCKET_PASSWORD_SIZE);
		messageSocketPassword = randomGenerator->generateByteString(MESSAGE_SERVER_MAX_PASSWORD_SIZE);
	}
	
	virtual void sendStartupInfo(MessageChannel &channel) {
		channel.write("HelperAgent info",
			requestSocketFilename.c_str(),
			Base64::encode(requestSocketPassword).c_str(),
			messageSocketFilename.c_str(),
			Base64::encode(messageSocketPassword).c_str(),
			NULL);
	}
};


class LoggingAgentWatcher: public AgentWatcher {
protected:
	string agentFilename;
	string socketAddress;
	
	virtual const char *name() const {
		return "Phusion Passenger logging agent";
	}
	
	virtual string getExeFilename() const {
		return agentFilename;
	}
	
	virtual void execProgram() const {
		execl(agentFilename.c_str(), "PassengerLoggingAgent", (char *) 0);
	}
	
	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) {
		VariantMap options = agentsOptions;
		options.set("logging_agent_address", loggingAgentAddress);
		options.set("logging_agent_password", loggingAgentPassword);
		options.writeToFd(fd);
	}
	
	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) {
		if (args[0] == "initialized") {
			return true;
		} else {
			return false;
		}
	}
	
public:
	LoggingAgentWatcher(const ResourceLocator &resourceLocator) {
		agentFilename = resourceLocator.getAgentsDir() + "/PassengerLoggingAgent";
	}
	
	virtual void sendStartupInfo(MessageChannel &channel) {
		channel.write("LoggingServer info",
			loggingAgentAddress.c_str(),
			loggingAgentPassword.c_str(),
			NULL);
	}
};


/**
 * Touch all files in the server instance dir every 6 hours in order to prevent /tmp
 * cleaners from weaking havoc:
 * http://code.google.com/p/phusion-passenger/issues/detail?id=365
 */
class ServerInstanceDirToucher {
private:
	oxt::thread *thr;
	
	static void
	threadMain() {
		while (!this_thread::interruption_requested()) {
			syscalls::sleep(60 * 60 * 6);
			
			begin_touch:
			
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			// Fork a process which touches everything in the server instance dir.
			pid_t pid = syscalls::fork();
			if (pid == 0) {
				// Child
				int prio, ret, e;
				
				closeAllFileDescriptors(2);
				
				// Make process nicer.
				do {
					prio = getpriority(PRIO_PROCESS, getpid());
				} while (prio == -1 && errno == EINTR);
				if (prio != -1) {
					prio++;
					if (prio > 20) {
						prio = 20;
					}
					do {
						ret = setpriority(PRIO_PROCESS, getpid(), prio);
					} while (ret == -1 && errno == EINTR);
				} else {
					perror("getpriority");
				}
				
				do {
					ret = chdir(serverInstanceDir->getPath().c_str());
				} while (ret == -1 && errno == EINTR);
				if (ret == -1) {
					e = errno;
					fprintf(stderr, "chdir(\"%s\") failed: %s (%d)\n",
						serverInstanceDir->getPath().c_str(),
						strerror(e), e);
					fflush(stderr);
					_exit(1);
				}
				
				execlp("/bin/sh", "/bin/sh", "-c", "find . | xargs touch", (char *) 0);
				e = errno;
				fprintf(stderr, "Cannot execute 'find . | xargs touch': %s (%d)\n",
					strerror(e), e);
				fflush(stderr);
				_exit(1);
			} else if (pid == -1) {
				// Error
				P_WARN("Could touch the server instance directory because "
					"fork() failed. Retrying in 2 minutes...");
				this_thread::restore_interruption si(di);
				this_thread::restore_syscall_interruption rsi(dsi);
				syscalls::sleep(60 * 2);
				goto begin_touch;
			} else {
				syscalls::waitpid(pid, NULL, 0);
			}
		}
	}

public:
	ServerInstanceDirToucher() {
		thr = new oxt::thread(threadMain, "Server instance dir toucher", 96 * 1024);
	}
	
	~ServerInstanceDirToucher() {
		thr->interrupt_and_join();
		delete thr;
	}
};


/**
 * Most operating systems overcommit memory. We *know* that this watchdog process
 * doesn't use much memory; on OS X it uses about 200 KB of private RSS. If the
 * watchdog is killed by the system Out-Of-Memory Killer or then it's all over:
 * the system administrator will have to restart the web server for Phusion
 * Passenger to be usable again. So in this function we do whatever is necessary
 * to prevent this watchdog process from becoming a candidate for the OS's
 * Out-Of-Memory Killer.
 */
static void
disableOomKiller() {
	// Linux-only way to disable OOM killer for current process. Requires root
	// privileges, which we should have.
	FILE *f = fopen("/proc/self/oom_adj", "w");
	if (f != NULL) {
		fprintf(f, "-17");
		fclose(f);
	}
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
waitForStarterProcessOrWatchers(vector<AgentWatcher *> &watchers) {
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
		vector<AgentWatcher *>::const_iterator it;
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
cleanupAgentsInBackground(vector<AgentWatcher *> &watchers) {
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	pid_t pid;
	int e;
	
	pid = fork();
	if (pid == 0) {
		// Child
		vector<AgentWatcher *>::const_iterator it;
		Timer timer(false);
		fd_set fds, fds2;
		int max, agentProcessesDone;
		unsigned long long deadline = 30000; // miliseconds
		
		// Wait until all agent processes have exited.
		
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
			for (it = watchers.begin(); it != watchers.end(); it++) {
				(*it)->forceShutdown();
			}
		} else {
			P_DEBUG("All Phusion Passenger agent processes have exited.");
		}
		
		// Now clean up the server instance directory.
		delete generation.get();
		delete serverInstanceDir.get();
		
		_exit(0);
		
	} else if (pid == -1) {
		// Error
		e = errno;
		throw SystemException("fork() failed", errno);
		
	} else {
		// Parent
		
		// Let child process handle cleanup.
		serverInstanceDir->detach();
		generation->detach();
	}
}

static void
forceAllAgentsShutdown(vector<AgentWatcher *> &watchers) {
	vector<AgentWatcher *>::iterator it;
	
	for (it = watchers.begin(); it != watchers.end(); it++) {
		(*it)->forceShutdown();
	}
}

int
main(int argc, char *argv[]) {
	disableOomKiller();
	
	agentsOptions = initializeAgent(argc, argv, "PassengerWatchdog");
	logLevel      = agentsOptions.getInt("log_level");
	webServerPid  = agentsOptions.getPid("web_server_pid");
	tempDir       = agentsOptions.get("temp_dir");
	userSwitching = agentsOptions.getBool("user_switching");
	defaultUser   = agentsOptions.get("default_user");
	defaultGroup  = agentsOptions.get("default_group");
	webServerWorkerUid = agentsOptions.getUid("web_server_worker_uid");
	webServerWorkerGid = agentsOptions.getGid("web_server_worker_gid");
	passengerRoot = agentsOptions.get("passenger_root");
	rubyCommand   = agentsOptions.get("ruby");
	maxPoolSize        = agentsOptions.getInt("max_pool_size");
	maxInstancesPerApp = agentsOptions.getInt("max_instances_per_app");
	poolIdleTime       = agentsOptions.getInt("pool_idle_time");
	serializedPrestartURLs  = agentsOptions.get("prestart_urls");
	
	try {
		randomGenerator = new RandomGenerator();
		errorEvent = new EventFd();
		
		MessageChannel feedbackChannel(FEEDBACK_FD);
		serverInstanceDir.reset(new ServerInstanceDir(webServerPid, tempDir));
		generation = serverInstanceDir->newGeneration(userSwitching, defaultUser,
			defaultGroup, webServerWorkerUid, webServerWorkerGid);
		agentsOptions.set("server_instance_dir", serverInstanceDir->getPath());
		agentsOptions.setInt("generation_number", generation->getNumber());
		
		ServerInstanceDirToucher serverInstanceDirToucher;
		ResourceLocator resourceLocator(passengerRoot);
		if (agentsOptions.get("analytics_server", false).empty()) {
			// Using local, server instance specific logging agent.
			loggingAgentAddress  = "unix:" + generation->getPath() + "/logging.socket";
			loggingAgentPassword = randomGenerator->generateAsciiString(64);
		} else {
			// Using remote logging agent.
			loggingAgentAddress = agentsOptions.get("analytics_server");
		}
		
		HelperAgentWatcher helperAgentWatcher(resourceLocator);
		LoggingAgentWatcher loggingAgentWatcher(resourceLocator);
		
		vector<AgentWatcher *> watchers;
		vector<AgentWatcher *>::iterator it;
		watchers.push_back(&helperAgentWatcher);
		if (agentsOptions.get("analytics_server", false).empty()) {
			watchers.push_back(&loggingAgentWatcher);
		}
		
		for (it = watchers.begin(); it != watchers.end(); it++) {
			try {
				(*it)->start();
			} catch (const std::exception &e) {
				feedbackChannel.write("Watchdog startup error",
					e.what(), NULL);
				forceAllAgentsShutdown(watchers);
				return 1;
			}
			// Allow other exceptions to propagate and crash the watchdog.
		}
		for (it = watchers.begin(); it != watchers.end(); it++) {
			try {
				(*it)->startWatching();
			} catch (const std::exception &e) {
				feedbackChannel.write("Watchdog startup error",
					e.what(), NULL);
				forceAllAgentsShutdown(watchers);
				return 1;
			}
			// Allow other exceptions to propagate and crash the watchdog.
		}
		
		feedbackChannel.write("Basic startup info",
			serverInstanceDir->getPath().c_str(),
			toString(generation->getNumber()).c_str(),
			NULL);
		
		for (it = watchers.begin(); it != watchers.end(); it++) {
			(*it)->sendStartupInfo(feedbackChannel);
		}
		
		feedbackChannel.write("All agents started", NULL);
		
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		bool exitGracefully = waitForStarterProcessOrWatchers(watchers);
		AgentWatcher::stopWatching(watchers);
		if (exitGracefully) {
			/* Fork a child process which cleans up all the agent processes in
			 * the background and exit this watchdog process so that we don't block
			 * the web server.
			 */
			cleanupAgentsInBackground(watchers);
			return 0;
		} else {
			P_DEBUG("Web server did not exit gracefully, forcing shutdown of all service processes...");
			forceAllAgentsShutdown(watchers);
			return 1;
		}
	} catch (const tracable_exception &e) {
		P_ERROR(e.what() << "\n" << e.backtrace());
		return 1;
	} catch (const std::exception &e) {
		P_ERROR(e.what());
		return 1;
	}
}
