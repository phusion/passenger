#ifndef _PASSENGER_SPAWN_MANAGER_H_
#define _PASSENGER_SPAWN_MANAGER_H_

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdarg>
#include <unistd.h>
#include <errno.h>
#ifdef TESTING_SPAWN_MANAGER
	#include <signal.h>
#endif

#include "Application.h"
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * @brief Spawning of Ruby on Rails application instances.
 *
 * This class is responsible for spawning new instances of Ruby on Rails applications.
 * Use the spawn() method to do so.
 *
 * @note This class is fully thread-safe.
 *
 * <h2>Implementation details</h2>
 * Internally, this class makes use of a spawn server, which is written in Ruby. This server
 * is automatically started when a SpawnManager instance is created, and automatically
 * shutdown when that instance is destroyed. The existance of the spawn server is almost
 * totally transparent to users of this class. Spawn requests are sent to the server,
 * and details about the spawned process is returned.
 *
 * If the spawn server dies during the middle of an operation, it will be restarted.
 * See spawn() for full details.
 *
 * The communication channel with the server is anonymous, i.e. no other processes
 * can access the communication channel, so communication is guaranteed to be safe
 * (unless, of course, if the spawn server itself is a trojan).
 *
 * The server will try to keep the spawning time as small as possible, by keeping
 * corresponding Ruby on Rails frameworks and application code in memory. So the second
 * time an instance of the same application is spawned, the spawn time is significantly
 * lower than the first time. Nevertheless, spawning is a relatively expensive operation
 * (compared to the processing of a typical HTTP request/response), and so should be
 * avoided whenever possible.
 *
 * See the documentation of the spawn server for full implementation details.
 *
 * @ingroup Support
 */
class SpawnManager {
private:
	static const int SPAWN_SERVER_INPUT_FD = 3;

	string spawnServerCommand;
	string logFile;
	string environment;
	string rubyCommand;
	
	mutex lock;
	
	MessageChannel channel;
	pid_t pid;
	bool serverNeedsRestart;

	/**
	 * Restarts the spawn server.
	 *
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 */
	void restartServer() {
		if (pid != 0) {
			channel.close();
			
			// Wait at most 5 seconds for the spawn server to exit.
			// If that doesn't work, kill it, then wait at most 5 seconds
			// for it to exit.
			time_t begin = time(NULL);
			bool done = false;
			while (!done && time(NULL) - begin < 5) {
				if (waitpid(pid, NULL, WNOHANG) > 0) {
					done = true;
				} else {
					usleep(100000);
				}
			}
			if (!done) {
				P_TRACE("Spawn server did not exit in time, killing it...");
				kill(pid, SIGTERM);
				begin = time(NULL);
				while (time(NULL) - begin < 5) {
					if (waitpid(pid, NULL, WNOHANG) > 0) {
						break;
					} else {
						usleep(100000);
					}
				}
				P_TRACE("Spawn server has exited.");
			}
			pid = 0;
		}
		
		int fds[2];
		FILE *logFileHandle = NULL;
		
		serverNeedsRestart = true;
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket", errno);
		}
		if (!logFile.empty()) {
			logFileHandle = fopen(logFile.c_str(), "a");
			if (logFileHandle == NULL) {
				string message("Cannot open log file '");
				message.append(logFile);
				message.append("' for writing.");
				throw IOException(message);
			}
		}

		pid = fork();
		if (pid == 0) {
			if (!logFile.empty()) {
				dup2(fileno(logFileHandle), STDERR_FILENO);
				fclose(logFileHandle);
			}
			dup2(STDERR_FILENO, STDOUT_FILENO);
			if (!environment.empty()) {
				setenv("RAILS_ENV", environment.c_str(), true);
			}
			dup2(fds[1], SPAWN_SERVER_INPUT_FD);
			
			// Close all unnecessary file descriptors
			for (long i = sysconf(_SC_OPEN_MAX) - 1; i >= 0; i--) {
				if (i > SPAWN_SERVER_INPUT_FD) {
					close(i);
				}
			}
			
			execlp(rubyCommand.c_str(),
				rubyCommand.c_str(),
				spawnServerCommand.c_str(),
				// The spawn server changes the process names of the subservers
				// that it starts, for better usability. However, the process name length
				// (as shown by ps) is limited. Here, we try to expand that limit by
				// deliberately passing a useless whitespace string to the spawn server.
				// This argument is ignored by the spawn server. This works on some
				// systems, such as Ubuntu Linux.
				"                                                             ",
				NULL);
			int e = errno;
			fprintf(stderr, "*** Passenger ERROR: Could not start the spawn server: %s: %s\n",
				rubyCommand.c_str(), strerror(e));
			fflush(stderr);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			close(fds[0]);
			close(fds[1]);
			if (logFileHandle != NULL) {
				fclose(logFileHandle);
			}
			pid = 0;
			throw SystemException("Unable to fork a process", e);
		} else {
			close(fds[1]);
			if (!logFile.empty()) {
				fclose(logFileHandle);
			}
			channel = MessageChannel(fds[0]);
			serverNeedsRestart = false;
			
			#ifdef TESTING_SPAWN_MANAGER
				if (nextRestartShouldFail) {
					kill(pid, SIGTERM);
					usleep(500000);
				}
			#endif
		}
	}
	
	/**
	 * Send the spawn command to the spawn server.
	 *
	 * @param appRoot The application root of the application to spawn.
	 * @param lowerPrivilege Whether to lower the application's privileges.
	 * @param lowestUser The user to fallback to if lowering privilege fails.
	 * @return An Application smart pointer, representing the spawned application.
	 * @throws SpawnException Something went wrong.
	 */
	ApplicationPtr sendSpawnCommand(const string &appRoot, bool lowerPrivilege, const string &lowestUser) {
		vector<string> args;
		int ownerPipe;
		
		try {
			channel.write("spawn_application",
				appRoot.c_str(),
				(lowerPrivilege) ? "true" : "false",
				lowestUser.c_str(),
				NULL);
		} catch (const SystemException &e) {
			throw SpawnException(string("Could not write 'spawn_application' "
				"command to the spawn server: ") + e.sys());
		}
		
		try {
			// Read status.
			if (!channel.read(args)) {
				throw SpawnException("The spawn server has exited unexpectedly.");
			}
			if (args.size() != 1) {
				throw SpawnException("The spawn server sent an invalid message.");
			}
			if (args[0] == "error_page") {
				string errorPage;
				
				if (!channel.readScalar(errorPage)) {
					throw SpawnException("The spawn server has exited unexpectedly.");
				}
				throw SpawnException("An error occured while spawning the application.",
					errorPage);
			} else if (args[0] != "ok") {
				throw SpawnException("The spawn server sent an invalid message.");
			}
			
			// Read application info.
			if (!channel.read(args)) {
				throw SpawnException("The spawn server has exited unexpectedly.");
			}
		} catch (const SystemException &e) {
			throw SpawnException(string("Could not read from the spawn server: ") + e.sys());
		}
		
		try {
			ownerPipe = channel.readFileDescriptor();
		} catch (const SystemException &e) {
			throw SpawnException(string("Could not receive the spawned "
				"application's owner pipe from the spawn server: ") +
				e.sys());
		} catch (const IOException &e) {
			throw SpawnException(string("Could not receive the spawned "
				"application's owner pipe from the spawn server: ") +
				e.what());
		}
		
		if (args.size() != 3) {
			close(ownerPipe);
			throw SpawnException("The spawn server sent an invalid message.");
		}
		
		pid_t pid = atoi(args[0]);
		bool usingAbstractNamespace = args[2] == "true";
		
		if (!usingAbstractNamespace) {
			chmod(args[1].c_str(), S_IRUSR | S_IWUSR);
			chown(args[1].c_str(), getuid(), getgid());
		}
		return ApplicationPtr(new Application(appRoot, pid, args[1],
			usingAbstractNamespace, ownerPipe));
	}
	
	ApplicationPtr
	handleSpawnException(const SpawnException &e, const string &appRoot,
	                     bool lowerPrivilege, const string &lowestUser) {
		bool restarted;
		try {
			P_DEBUG("Spawn server died. Attempting to restart it...");
			restartServer();
			P_DEBUG("Restart seems to be successful.");
			restarted = true;
		} catch (const IOException &e) {
			P_DEBUG("Restart failed: " << e.what());
			restarted = false;
		} catch (const SystemException &e) {
			P_DEBUG("Restart failed: " << e.what());
			restarted = false;
		}
		if (restarted) {
			return sendSpawnCommand(appRoot, lowerPrivilege, lowestUser);
		} else {
			throw SpawnException("The spawn server died unexpectedly, and restarting it failed.");
		}
	}
	
	/**
	 * Send the reload command to the spawn server.
	 *
	 * @param appRoot The application root to reload.
	 * @throws SystemException Something went wrong.
	 */
	void sendReloadCommand(const string &appRoot) {
		try {
			channel.write("reload", appRoot.c_str(), NULL);
		} catch (const SystemException &e) {
			throw SystemException("Could not write 'reload' command "
				"to the spawn server", e.code());
		}
	}
	
	void handleReloadException(const SystemException &e, const string &appRoot) {
		bool restarted;
		try {
			P_DEBUG("Spawn server died. Attempting to restart it...");
			restartServer();
			P_DEBUG("Restart seems to be successful.");
			restarted = true;
		} catch (const IOException &e) {
			P_DEBUG("Restart failed: " << e.what());
			restarted = false;
		} catch (const SystemException &e) {
			P_DEBUG("Restart failed: " << e.what());
			restarted = false;
		}
		if (restarted) {
			return sendReloadCommand(appRoot);
		} else {
			throw SpawnException("The spawn server died unexpectedly, and restarting it failed.");
		}
	}
	
	IOException prependMessageToException(const IOException &e, const string &message) {
		return IOException(message + ": " + e.what());
	}
	
	SystemException prependMessageToException(const SystemException &e, const string &message) {
		return SystemException(message + ": " + e.brief(), e.code());
	}

public:
	#ifdef TESTING_SPAWN_MANAGER
		bool nextRestartShouldFail;
	#endif

	/**
	 * Construct a new SpawnManager.
	 *
	 * @param spawnServerCommand The filename of the spawn server to use.
	 * @param logFile Specify a log file that the spawn server should use.
	 *            Messages on its standard output and standard error channels
	 *            will be written to this log file. If an empty string is
	 *            specified, no log file will be used, and the spawn server
	 *            will use the same standard output/error channels as the
	 *            current process.
	 * @param environment The RAILS_ENV environment that all RoR applications
	 *            should use. If an empty string is specified, the current value
	 *            of the RAILS_ENV environment variable will be used.
	 * @param rubyCommand The Ruby interpreter's command.
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 */
	SpawnManager(const string &spawnServerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby") {
		this->spawnServerCommand = spawnServerCommand;
		this->logFile = logFile;
		this->environment = environment;
		this->rubyCommand = rubyCommand;
		pid = 0;
		#ifdef TESTING_SPAWN_MANAGER
			nextRestartShouldFail = false;
		#endif
		try {
			restartServer();
		} catch (const IOException &e) {
			throw prependMessageToException(e, "Could not start the spawn server");
		} catch (const SystemException &e) {
			throw prependMessageToException(e, "Could not start the spawn server");
		}
	}
	
	~SpawnManager() throw() {
		if (pid != 0) {
			channel.close();
			waitpid(pid, NULL, 0);
		}
	}
	
	/**
	 * Spawn a new instance of a Ruby on Rails application.
	 *
	 * If the spawn server died during the spawning process, then the server
	 * will be automatically restarted, and another spawn attempt will be made.
	 * If restarting the server fails, or if the second spawn attempt fails,
	 * then an exception will be thrown.
	 *
	 * If <tt>lowerPrivilege</tt> is true, then it will be attempt to
	 * switch the spawned application instance to the user who owns the
	 * application's <tt>config/environment.rb</tt>, and to the default
	 * group of that user.
	 *
	 * If that user doesn't exist on the system, or if that user is root,
	 * then it will be attempted to switch to the username given by
	 * <tt>lowestUser</tt> (and to the default group of that user).
	 * If <tt>lowestUser</tt> doesn't exist either, or if switching user failed
	 * (because the spawn server process does not have the privilege to do so),
	 * then the application will be spawned anyway, without reporting an error.
	 *
	 * It goes without saying that lowering privilege is only possible if
	 * the spawn server is running as root (and thus, by induction, that
	 * Passenger and Apache's control process are also running as root).
	 * Note that if Apache is listening on port 80, then its control process must
	 * be running as root. See "doc/Security of user switching.txt" for
	 * a detailed explanation.
	 *
	 * @param appRoot The application root of a RoR application, i.e. the folder that
	 *             contains 'app/', 'public/', 'config/', etc. This must be a valid directory,
	 *             but the path does not have to be absolute.
	 * @param lowerPrivilege Whether to lower the application's privileges.
	 * @param lowestUser The user to fallback to if lowering privilege fails.
	 * @return A smart pointer to an Application object, which represents the application
	 *         instance that has been spawned. Use this object to communicate with the
	 *         spawned application.
	 * @throws SpawnException Something went wrong.
	 */
	ApplicationPtr spawn(const string &appRoot, bool lowerPrivilege = true, const string &lowestUser = "nobody") {
		mutex::scoped_lock l(lock);
		try {
			return sendSpawnCommand(appRoot, lowerPrivilege, lowestUser);
		} catch (const SpawnException &e) {
			if (e.hasErrorPage()) {
				throw;
			} else {
				return handleSpawnException(e, appRoot, lowerPrivilege, lowestUser);
			}
		}
	}
	
	/**
	 * Remove the cached application instances at the given application root.
	 *
	 * Application code might be cached in memory. But once it a while, it will
	 * be necessary to reload the code for an application, such as after
	 * deploying a new version of the application. This method makes sure that
	 * any cached application code is removed, so that the next time an
	 * application instance is spawned, the application code will be freshly
	 * loaded into memory.
	 *
	 * @throws SystemException Unable to communicate with the spawn server,
	 *         even after a restart.
	 * @throws SpawnException The spawn server died unexpectedly, and a
	 *         restart was attempted, but it failed.
	 */
	void reload(const string &appRoot) {
		try {
			return sendReloadCommand(appRoot);
		} catch (const SystemException &e) {
			return handleReloadException(e, appRoot);
		}
	}
	
	/**
	 * Get the Process ID of the spawn server. This method is used in the unit tests
	 * and should not be used directly.
	 */
	pid_t getServerPid() const {
		return pid;
	}
};

/** Convenient alias for SpawnManager smart pointer. */
typedef shared_ptr<SpawnManager> SpawnManagerPtr;

} // namespace Passenger

#endif /* _PASSENGER_SPAWN_MANAGER_H_ */
