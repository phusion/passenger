#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <string>

#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "ServerInstanceDir.h"
#include "FileDescriptor.h"
#include "MessageChannel.h"
#include "MessageServer.h"
#include "RandomGenerator.h"
#include "Timer.h"
#include "Base64.h"
#include "Logging.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace std;
using namespace boost;
using namespace oxt;
using namespace Passenger;


static string         webServerType;        // "apache" or "nginx"
static unsigned int   logLevel;
static FileDescriptor feedbackFd;  // This is the feedback fd to the web server, not to the helper server.
static pid_t   webServerPid;
static string  tempDir;
static bool    userSwitching;
static string  defaultUser;
static uid_t   workerUid;
static gid_t   workerGid;
static string  passengerRoot;
static string  rubyCommand;
static unsigned int maxPoolSize;
static unsigned int maxInstancesPerApp;
static unsigned int poolIdleTime;
static string  analyticsLogDir;

static ServerInstanceDirPtr serverInstanceDir;
static ServerInstanceDir::GenerationPtr generation;
static string loggingSocketPassword;
static RandomGenerator randomGenerator;
static EventFd errorEvent;

static string logLevelString;
static string webServerPidString;
static string workerUidString;
static string workerGidString;
static string generationNumber;
static string maxPoolSizeString;
static string maxInstancesPerAppString;
static string poolIdleTimeString;

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
			errorEvent.notify();
		} catch (const exception &e) {
			lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = e.what();
			errorEvent.notify();
		} catch (...) {
			lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = "Unknown error";
			errorEvent.notify();
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
	void killAndWait(pid_t pid) {
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		// If the process is a process group leader then killing the
		// group will likely kill all its child processes too.
		if (syscalls::killpg(pid, SIGKILL) == -1) {
			syscalls::kill(pid, SIGKILL);
		}
		syscalls::waitpid(pid, NULL, 0);
	}
	
public:
	AgentWatcher() {
		thr = NULL;
		pid = 0;
	}
	
	~AgentWatcher() {
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
		int fds[2], e, ret;
		pid_t pid;
		
		/* Create feedback fd for this agent process. We'll send some startup
		 * arguments to this agent process through this fd, and we'll receive
		 * startup information through it as well.
		 */
		if (syscalls::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			int e = errno;
			throw SystemException("Cannot create a Unix socket pair", e);
		}
		
		pid = syscalls::fork();
		if (pid == 0) {
			// Child
			long max_fds, i;
			
			/* Make sure the feedback fd (fds[1]) is 3 and close
			 * all other file descriptors.
			 */
			syscalls::close(fds[0]);
			if (fds[1] != 3) {
				if (syscalls::dup2(fds[1], 3) == -1) {
					/* Something went wrong, report error through feedback fd. */
					e = errno;
					try {
						MessageChannel(fds[1]).write("system error before fork",
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
			}
			/* Close all file descriptors except 0-3. */
			max_fds = sysconf(_SC_OPEN_MAX);
			for (i = 4; i < max_fds; i++) {
				if (i != fds[1]) {
					syscalls::close(i);
				}
			}
			
			try {
				execProgram();
			} catch (...) {
				fprintf(stderr, "PassengerWatchdog: execProgram() threw an exception\n");
				fflush(stderr);
				_exit(1);
			}
			e = errno;
			try {
				MessageChannel(3).write("exec error", toString(e).c_str(), NULL);
				_exit(1);
			} catch (...) {
				fprintf(stderr, "Passenger Watchdog: could not execute %s: %s (%d)\n",
					exeFilename.c_str(), strerror(e), e);
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
			FileDescriptor feedbackFd(fds[0]);
			vector<string> args;
			
			syscalls::close(fds[1]);
			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			
			// Send startup arguments.
			try {
				sendStartupArguments(pid, feedbackFd);
			} catch (const SystemException &ex) {
				killAndWait(pid);
				throw SystemException(string("Unable to start the ") + name() +
					": an error occurred while sending startup arguments",
					ex.code());
			} catch (...) {
				killAndWait(pid);
				throw;
			}
			
			// Now read its feedback.
			try {
				if (!MessageChannel(feedbackFd).read(args)) {
					throw EOFException("");
				}
			} catch (const EOFException &e) {
				this_thread::disable_interruption di2;
				this_thread::disable_syscall_interruption dsi2;
				int status;
				
				/* The feedback fd was closed for an unknown reason.
				 * Did the agent process crash?
				 */
				ret = syscalls::waitpid(pid, &status, WNOHANG);
				if (ret == 0) {
					/* Doesn't look like it; it seems it's still running.
					 * We can't do anything without proper feedback so kill
					 * the agent process and throw an exception.
					 */
					killAndWait(pid);
					throw RuntimeException(string("Unable to start the ") + name() +
						": an unknown error occurred during its startup");
				} else if (ret != -1 && WIFSIGNALED(status)) {
					/* Looks like a crash which caused a signal. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have been killed with signal " +
						getSignalName(WTERMSIG(status)) + " during startup");
				} else {
					/* Looks like it exited after detecting an error. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have crashed during startup for an unknown reason");
				}
			} catch (const SystemException &e) {
				killAndWait(pid);
				throw SystemException(string("Unable to start the ") + name() +
					": unable to read its startup information",
					e.code());
			} catch (const RuntimeException &) {
				/* Rethrow without killing the PID because the process
				 * is already dead.
				 */
				throw;
			} catch (...) {
				killAndWait(pid);
				throw;
			}
			
			if (args[0] == "system error before exec") {
				killAndWait(pid);
				throw SystemException(string("Unable to start the ") + name() +
					": " + args[1], atoi(args[2]));
			} else if (args[0] == "exec error") {
				killAndWait(pid);
				throw SystemException(string("Unable to start the ") + name(),
					atoi(args[1]));
			} else {
				bool processed;
				try {
					processed = processStartupInfo(pid, feedbackFd, args);
				} catch (...) {
					killAndWait(pid);
					throw;
				}
				if (!processed) {
					killAndWait(pid);
					throw RuntimeException(string("The ") + name() +
						" sent an unknown startup info message '" +
						args[0] + "'");
				}
			}
			
			lock_guard<boost::mutex> l(lock);
			this->feedbackFd = feedbackFd;
			this->pid = pid;
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
	 * Returns the agent process feedback fd, or NULL if the agent process
	 * hasn't been started yet. Can be used to check whether this agent process
	 * has exited without using waitpid().
	 */
	const FileDescriptor getFeedbackFd() const {
		lock_guard<boost::mutex> l(lock);
		return feedbackFd;
	}
};


class HelperServerWatcher: public AgentWatcher {
protected:
	string         requestSocketPassword;
	string         messageSocketPassword;
	string         helperServerFilename;
	string         requestSocketFilename;
	string         messageSocketFilename;
	
	virtual const char *name() const {
		return "Phusion Passenger helper server";
	}
	
	virtual string getExeFilename() const {
		return helperServerFilename;
	}
	
	virtual void execProgram() const {
		execl(helperServerFilename.c_str(),
			"PassengerHelperServer",
			logLevelString.c_str(),
			"3",  // feedback fd
			webServerPidString.c_str(),
			tempDir.c_str(),
			userSwitching ? "true" : "false",
			defaultUser.c_str(),
			workerUidString.c_str(),
			workerGidString.c_str(),
			passengerRoot.c_str(),
			rubyCommand.c_str(),
			generationNumber.c_str(),
			maxPoolSizeString.c_str(),
			maxInstancesPerAppString.c_str(),
			poolIdleTimeString.c_str(),
			analyticsLogDir.c_str(),
			(char *) 0);
	}
	
	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) {
		MessageChannel channel(fd);
		channel.write("passwords",
			Base64::encode(requestSocketPassword).c_str(),
			Base64::encode(messageSocketPassword).c_str(),
			Base64::encode(loggingSocketPassword).c_str(),
			NULL);
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
	HelperServerWatcher() {
		requestSocketPassword = randomGenerator.generateByteString(REQUEST_SOCKET_PASSWORD_SIZE);
		messageSocketPassword = randomGenerator.generateByteString(MessageServer::MAX_PASSWORD_SIZE);
		if (webServerType == "apache") {
			helperServerFilename = passengerRoot + "/ext/apache2/PassengerHelperServer";
		} else {
			helperServerFilename = passengerRoot + "/ext/nginx/PassengerHelperServer";
		}
	}
	
	virtual void sendStartupInfo(MessageChannel &channel) {
		channel.write("HelperServer info",
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
	
	virtual const char *name() const {
		return "Phusion Passenger logging agent";
	}
	
	virtual string getExeFilename() const {
		return agentFilename;
	}
	
	virtual void execProgram() const {
		execl(agentFilename.c_str(),
			"PassengerLoggingAgent",
			"3",  // feedback fd
			webServerPidString.c_str(),
			tempDir.c_str(),
			generationNumber.c_str(),
			analyticsLogDir.c_str(),
			"",
			"",
			(char *) 0);
	}
	
	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) {
		MessageChannel channel(fd);
		channel.write("logging socket password",
			Base64::encode(loggingSocketPassword).c_str(),
			NULL);
	}
	
	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) {
		if (args[0] == "initialized") {
			return true;
		} else {
			return false;
		}
	}
	
public:
	LoggingAgentWatcher() {
		agentFilename = passengerRoot + "/ext/common/PassengerLoggingAgent";
	}
	
	virtual void sendStartupInfo(MessageChannel &channel) {
		channel.write("LoggingServer info",
			Base64::encode(loggingSocketPassword).c_str(),
			NULL);
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

static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
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
	FD_SET(feedbackFd, &fds);
	FD_SET(errorEvent.fd(), &fds);
	
	if (feedbackFd > errorEvent.fd()) {
		max = feedbackFd;
	} else {
		max = errorEvent.fd();
	}
	
	ret = syscalls::select(max + 1, &fds, NULL, NULL, NULL);
	if (ret == -1) {
		int e = errno;
		P_ERROR("select() failed: " << strerror(e));
		return false;
	}
	
	if (FD_ISSET(errorEvent.fd(), &fds)) {
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
		ret = syscalls::read(feedbackFd, &x, 1);
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
			
			FD_COPY(&fds, &fds2);
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
	#define READ_ARG(index, expr, defaultValue) ((argc > index) ? (expr) : (defaultValue))
	
	webServerType = argv[1];
	logLevel      = atoi(argv[2]);
	feedbackFd    = atoi(argv[3]);
	webServerPid  = (pid_t) atoll(argv[4]);
	tempDir       = argv[5];
	userSwitching = strcmp(argv[6], "true") == 0;
	defaultUser   = argv[7];
	workerUid     = (uid_t) atoll(argv[8]);
	workerGid     = (uid_t) atoll(argv[9]);
	passengerRoot = argv[10];
	rubyCommand   = argv[11];
	maxPoolSize        = atoi(argv[12]);
	maxInstancesPerApp = atoi(argv[13]);
	poolIdleTime       = atoi(argv[14]);
	analyticsLogDir    = argv[15];
	
	/* Become the session leader so that Apache can't kill this
	 * watchdog with killpg() during shutdown, and so that a
	 * Ctrl-C only affects the web server.
	 */
	setsid();
	
	disableOomKiller();
	ignoreSigpipe();
	setup_syscall_interruption_support();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	setLogLevel(logLevel);
	
	// Change process title.
	strncpy(argv[0], "PassengerWatchdog", strlen(argv[0]));
	for (int i = 1; i < argc; i++) {
		memset(argv[i], '\0', strlen(argv[i]));
	}
	
	try {
		MessageChannel feedbackChannel(feedbackFd);
		serverInstanceDir.reset(new ServerInstanceDir(webServerPid, tempDir));
		generation = serverInstanceDir->newGeneration(userSwitching, defaultUser, workerUid, workerGid);
		
		/* Pre-convert some integers to strings so that we don't have to do this
		 * after forking.
		 */
		logLevelString     = toString(logLevel);
		webServerPidString = toString(webServerPid);
		workerUidString    = toString(workerUid);
		workerGidString    = toString(workerGid);
		generationNumber   = toString(generation->getNumber());
		maxPoolSizeString  = toString(maxPoolSize);
		maxInstancesPerAppString = toString(maxInstancesPerApp);
		poolIdleTimeString = toString(poolIdleTime);
		
		loggingSocketPassword = randomGenerator.generateByteString(32);
		
		HelperServerWatcher helperServerWatcher;
		LoggingAgentWatcher loggingAgentWatcher;
		
		vector<AgentWatcher *> watchers;
		vector<AgentWatcher *>::iterator it;
		watchers.push_back(&helperServerWatcher);
		watchers.push_back(&loggingAgentWatcher);
		
		for (it = watchers.begin(); it != watchers.end(); it++) {
			try {
				(*it)->start();
			} catch (const exception &e) {
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
			} catch (const exception &e) {
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
	} catch (const exception &e) {
		P_ERROR(e.what());
		return 1;
	}
}
