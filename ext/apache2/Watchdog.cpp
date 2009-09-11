#include <oxt/thread.hpp>
#include <oxt/system_calls.hpp>
#include <string>

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
#include "Base64.h"
#include "Logging.h"
#include "Exceptions.h"

using namespace std;
using namespace boost;
using namespace oxt;
using namespace Passenger;


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

static boost::mutex globalMutex;
static bool exitGracefully = false;

struct HelperServerFeedback {
	FileDescriptor feedbackFd;
	string socketFilename;
};


static string
findHelperServer() {
	return passengerRoot + "/ext/apache2/HelperServer";
}

static void
killAndWait(pid_t pid) {
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	syscalls::kill(pid, SIGKILL);
	syscalls::waitpid(pid, NULL, 0);
}

static pid_t
startHelperServer(const string &helperServerFilename, unsigned int generationNumber,
	const string &webServerPassword, HelperServerFeedback &feedback
) {
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	int fds[2], e, ret;
	pid_t pid;
	
	if (syscalls::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
		int e = errno;
		throw SystemException("Cannot create a Unix socket pair", e);
	}
	
	pid = syscalls::fork();
	if (pid == 0) {
		// Child
		long max_fds, i;
		
		// Make sure the feedback fd is 3 and close all other file descriptors.
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
					fprintf(stderr, "Passenger Watchdog: dup2() failed: %s (%d)\n",
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
		
		execl(helperServerFilename.c_str(),
			helperServerFilename.c_str(),
			toString(logLevel).c_str(),
			"3",  // feedback fd
			toString(webServerPid).c_str(),
			tempDir.c_str(),
			userSwitching ? "true" : "false",
			defaultUser.c_str(),
			toString(workerUid).c_str(),
			toString(workerGid).c_str(),
			passengerRoot.c_str(),
			rubyCommand.c_str(),
			toString(generationNumber).c_str(),
			(char *) 0);
		e = errno;
		try {
			MessageChannel(3).write("exec error", toString(e).c_str(), NULL);
			_exit(1);
		} catch (...) {
			fprintf(stderr, "Passenger Watchdog: could not execute %s: %s (%d)\n",
				helperServerFilename.c_str(), strerror(e), e);
			fflush(stderr);
			_exit(1);
		}
	} else if (pid == -1) {
		// Error
		e = errno;
		syscalls::close(fds[0]);
		syscalls::close(fds[1]);
		throw SystemException("Cannot create a new process", e);
	} else {
		// Parent
		FileDescriptor helperServerFeedbackFd(fds[0]);
		MessageChannel helperServerFeedbackChannel(fds[0]);
		vector<string> args;
		
		syscalls::close(fds[1]);
		this_thread::restore_interruption ri(di);
		this_thread::restore_syscall_interruption rsi(dsi);
		
		try {
			// Send the desired web server account password.
			helperServerFeedbackChannel.write("web server account password",
				Base64::encode(webServerPassword).c_str(),
				NULL);
		} catch (const SystemException &ex) {
			killAndWait(pid);
			throw SystemException("Unable to start the helper server: "
				"an error occurred while sending startup arguments",
				ex.code());
		} catch (...) {
			killAndWait(pid);
			throw;
		}
		
		// Now read its feedback.
		try {
			if (!helperServerFeedbackChannel.read(args)) {
				this_thread::disable_interruption di2;
				this_thread::disable_syscall_interruption dsi2;
				
				/* The feedback fd was closed for an unknown reason.
				 * Did the helper server crash?
				 */
				ret = syscalls::waitpid(pid, NULL, WNOHANG);
				if (ret == 0) {
					/* Doesn't look like it; it seems it's still running.
					 * We can't do anything without proper feedback so kill
					 * the helper server and throw an exception.
					 */
					killAndWait(pid);
					throw RuntimeException("Unable to start the helper server: "
						"an unknown error occurred during its startup");
				} else {
					/* Seems like it. */
					throw RuntimeException("Unable to start the helper server: "
						"it seems to have crashed during startup for an unknown reason");
				}
			}
		} catch (const SystemException &ex) {
			killAndWait(pid);
			throw SystemException("Unable to start the helper server: "
				"unable to read its initialization feedback",
				ex.code());
		} catch (const RuntimeException &) {
			throw;
		} catch (...) {
			killAndWait(pid);
			throw;
		}
		
		if (args[0] == "initialized") {
			feedback.feedbackFd = helperServerFeedbackFd;
			feedback.socketFilename = args[1];
		} else if (args[0] == "system error") {
			killAndWait(pid);
			throw SystemException(args[1], atoi(args[2]));
		} else if (args[0] == "exec error") {
			killAndWait(pid);
			throw SystemException("Unable to start the helper server", atoi(args[1]));
		} else {
			killAndWait(pid);
			throw RuntimeException("The helper server sent an unknown feedback message '" + args[0] + "'");
		}
		
		return pid;
	}
}

static void
relayFeedback(const string &webServerPassword, const HelperServerFeedback &feedback) {
	MessageChannel feedbackChannel(feedbackFd);
	feedbackChannel.write("initialized",
		feedback.socketFilename.c_str(),
		Base64::encode(webServerPassword).c_str(),
		NULL);
}

static void
cleanupHelperServerInBackground(ServerInstanceDirPtr &serverInstanceDir,
	ServerInstanceDir::GenerationPtr &generation, FileDescriptor &helperServerFeedbackFd)
{
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	pid_t pid;
	
	pid = fork();
	if (pid == 0) {
		// Child
		char x;
		
		// Wait until helper server has exited.
		syscalls::read(helperServerFeedbackFd, &x, 1);
		
		// Now clean up the server instance directory.
		generation.reset();
		serverInstanceDir.reset();
		
		_exit(0);
		
	} else if (pid == -1) {
		// Error
		// TODO
		
	} else {
		// Parent
		
		// Let child process handle cleanup.
		serverInstanceDir->detach();
		generation->detach();
	}
}

static void
watchdogMainLoop() {
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	
	try {
		ServerInstanceDirPtr serverInstanceDir(new ServerInstanceDir(webServerPid, tempDir));
		ServerInstanceDir::GenerationPtr generation =
			serverInstanceDir->newGeneration(userSwitching, defaultUser, workerUid, workerGid);
		
		char webServerPasswordData[MessageServer::MAX_PASSWORD_SIZE];
		string webServerPassword;
		string helperServerFilename;
		bool done = false;
		bool firstStart = true;
		pid_t pid;
		int ret, status;
		
		generateSecureToken(webServerPasswordData, sizeof(webServerPasswordData));
		webServerPassword.assign(webServerPasswordData, sizeof(webServerPasswordData));
		helperServerFilename = findHelperServer();
		
		while (!done && !this_thread::interruption_requested()) {
			HelperServerFeedback feedback;
			
			try {
				this_thread::restore_interruption ri(di);
				this_thread::restore_syscall_interruption rsi(dsi);
				pid = startHelperServer(helperServerFilename, generation->getNumber(),
					webServerPassword, feedback);
			} catch (const thread_interrupted &) {
				return;
			}
			
			if (firstStart) {
				firstStart = false;
				this_thread::restore_interruption ri(di);
				this_thread::restore_syscall_interruption rsi(dsi);
				try {
					relayFeedback(webServerPassword, feedback);
				} catch (const thread_interrupted &) {
					killAndWait(pid);
					return;
				} catch (...) {
					killAndWait(pid);
					throw;
				}
			}
			
			
			try {
				this_thread::restore_interruption ri(di);
				this_thread::restore_syscall_interruption rsi(dsi);
				ret = syscalls::waitpid(pid, &status, 0);
			} catch (const thread_interrupted &) {
				// If we get interrupted here it means something happened
				// to the web server.
				
				bool graceful;
				globalMutex.lock();
				graceful = exitGracefully;
				globalMutex.unlock();
				
				if (graceful) {
					/* The web server exited gracefully. In this case it must have
					 * sent an exit message to the helper server. So we fork a child
					 * process which waits until the helper server has exited, and then
					 * removes the generation directory and server instance directory.
					 * The parent watchdog process exits so that it doesn't block
					 * the web server's shutdown process.
					 */
					cleanupHelperServerInBackground(serverInstanceDir, generation,
						feedback.feedbackFd);
				} else {
					/* Looks like the web server crashed. Let's kill the
					 * entire HelperServer process group (i.e. HelperServer and all
					 * descendant processes).
					 */
					syscalls::killpg(pid, SIGKILL);
					syscalls::waitpid(pid, NULL, 0);
				}
				return;
			}
			
			if (ret != -1) {
				done = WIFEXITED(status) && WEXITSTATUS(status) == 0;
			}
			// If waitpid() returns -1 then the child process has somehow disappeared.
			// Not sure what happens, but continue the loop and restart it.
			if (!done) {
				P_DEBUG("Helper server crashed, restarting it...");
			}
		}
	} catch (const tracable_exception &e) {
		P_ERROR(e.what() << "\n" << e.backtrace());
	}
}

static void
disableOomKiller() {
	FILE *f = fopen("/proc/self/oom_adj", "w");
	if (f != NULL) {
		fprintf(f, "-17");
		fclose(f);
	}
}

int
main(int argc, char *argv[]) {
	logLevel      = atoi(argv[1]);
	feedbackFd    = atoi(argv[2]);
	webServerPid  = (pid_t) atoll(argv[3]);
	tempDir       = argv[4];
	userSwitching = strcmp(argv[5], "true") == 0;
	defaultUser   = argv[6];
	workerUid     = (uid_t) atoll(argv[7]);
	workerGid     = (uid_t) atoll(argv[8]);
	passengerRoot = argv[9];
	rubyCommand   = argv[10];
	
	disableOomKiller();
	setup_syscall_interruption_support();
	
	oxt::thread watchdogThread(watchdogMainLoop, "Watchdog thread", 16 * 1024);
	
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	char x;
	int ret = syscalls::read(feedbackFd, &x, 1);
	if (ret == 1) {
		// The web server exited gracefully.
		globalMutex.lock();
		exitGracefully = true;
		globalMutex.unlock();
		watchdogThread.interrupt_and_join();
	} else {
		// The web server exited abnormally.
		watchdogThread.interrupt_and_join();
	}
	return 0;
}
