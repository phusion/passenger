/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_SPAWN_MANAGER_H_
#define _PASSENGER_SPAWN_MANAGER_H_

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdarg>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>

#include "Application.h"
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Logging.h"

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

/**
 * @brief Spawning of Ruby on Rails/Rack application instances.
 *
 * This class is responsible for spawning new instances of Ruby on Rails or
 * Rack applications. Use the spawn() method to do so.
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
	string rubyCommand;
	string user;
	
	boost::mutex lock;
	
	MessageChannel channel;
	pid_t pid;
	bool serverNeedsRestart;

	/**
	 * Restarts the spawn server.
	 *
	 * @pre System call interruption is disabled.
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 */
	void restartServer() {
		TRACE_POINT();
		if (pid != 0) {
			UPDATE_TRACE_POINT();
			channel.close();
			
			// Wait at most 5 seconds for the spawn server to exit.
			// If that doesn't work, kill it, then wait at most 5 seconds
			// for it to exit.
			time_t begin = syscalls::time(NULL);
			bool done = false;
			while (!done && syscalls::time(NULL) - begin < 5) {
				if (syscalls::waitpid(pid, NULL, WNOHANG) > 0) {
					done = true;
				} else {
					syscalls::usleep(100000);
				}
			}
			UPDATE_TRACE_POINT();
			if (!done) {
				UPDATE_TRACE_POINT();
				P_TRACE(2, "Spawn server did not exit in time, killing it...");
				syscalls::kill(pid, SIGTERM);
				begin = syscalls::time(NULL);
				while (syscalls::time(NULL) - begin < 5) {
					if (syscalls::waitpid(pid, NULL, WNOHANG) > 0) {
						break;
					} else {
						syscalls::usleep(100000);
					}
				}
				P_TRACE(2, "Spawn server has exited.");
			}
			pid = 0;
		}
		
		int fds[2];
		FILE *logFileHandle = NULL;
		
		serverNeedsRestart = true;
		if (syscalls::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket", errno);
		}
		if (!logFile.empty()) {
			logFileHandle = syscalls::fopen(logFile.c_str(), "a");
			if (logFileHandle == NULL) {
				string message("Cannot open log file '");
				message.append(logFile);
				message.append("' for writing.");
				throw IOException(message);
			}
		}

		UPDATE_TRACE_POINT();
		pid = syscalls::fork();
		if (pid == 0) {
			if (!logFile.empty()) {
				dup2(fileno(logFileHandle), STDERR_FILENO);
				fclose(logFileHandle);
			}
			dup2(STDERR_FILENO, STDOUT_FILENO);
			dup2(fds[1], SPAWN_SERVER_INPUT_FD);
			
			// Close all unnecessary file descriptors
			for (long i = sysconf(_SC_OPEN_MAX) - 1; i > SPAWN_SERVER_INPUT_FD; i--) {
				close(i);
			}
			
			if (!user.empty()) {
				struct passwd *entry = getpwnam(user.c_str());
				if (entry != NULL) {
					if (setgid(entry->pw_gid) != 0) {
						int e = errno;
						fprintf(stderr, "*** Passenger: cannot run spawn "
							"manager as group %d: %s (%d)\n",
							entry->pw_gid,
							strerror(e),
							e);
					}
					if (setuid(entry->pw_uid) != 0) {
						int e = errno;
						fprintf(stderr, "*** Passenger: cannot run spawn "
							"manager as user %s (%d): %s (%d)\n",
							user.c_str(), entry->pw_uid,
							strerror(e),
							e);
					}
				} else {
					fprintf(stderr, "*** Passenger: cannot run spawn manager "
						"as nonexistant user '%s'.\n",
						user.c_str());
				}
				fflush(stderr);
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
			fprintf(stderr, "*** Passenger ERROR: Could not start the spawn server: %s: %s (%d)\n",
				rubyCommand.c_str(), strerror(e), e);
			fflush(stderr);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			syscalls::close(fds[0]);
			syscalls::close(fds[1]);
			if (logFileHandle != NULL) {
				syscalls::fclose(logFileHandle);
			}
			pid = 0;
			throw SystemException("Unable to fork a process", e);
		} else {
			syscalls::close(fds[1]);
			if (!logFile.empty()) {
				syscalls::fclose(logFileHandle);
			}
			channel = MessageChannel(fds[0]);
			serverNeedsRestart = false;
			
			#ifdef TESTING_SPAWN_MANAGER
				if (nextRestartShouldFail) {
					syscalls::kill(pid, SIGTERM);
					syscalls::usleep(500000);
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
	 * @param environment The RAILS_ENV/RACK_ENV environment that should be used.
	 * @param spawnMethod The spawn method to use.
	 * @param appType The application type.
	 * @return An Application smart pointer, representing the spawned application.
	 * @throws SpawnException Something went wrong.
	 */
	ApplicationPtr sendSpawnCommand(
		const string &appRoot,
		bool lowerPrivilege,
		const string &lowestUser,
		const string &environment,
		const string &spawnMethod,
		const string &appType
	) {
		TRACE_POINT();
		vector<string> args;
		int ownerPipe;
		
		try {
			channel.write("spawn_application",
				appRoot.c_str(),
				(lowerPrivilege) ? "true" : "false",
				lowestUser.c_str(),
				environment.c_str(),
				spawnMethod.c_str(),
				appType.c_str(),
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
			syscalls::close(ownerPipe);
			throw SpawnException("The spawn server sent an invalid message.");
		}
		
		pid_t pid = atoi(args[0]);
		bool usingAbstractNamespace = args[2] == "true";
		
		if (!usingAbstractNamespace) {
			int ret;
			do {
				ret = chmod(args[1].c_str(), S_IRUSR | S_IWUSR);
			} while (ret == -1 && errno == EINTR);
			do {
				ret = chown(args[1].c_str(), getuid(), getgid());
			} while (ret == -1 && errno == EINTR);
		}
		return ApplicationPtr(new Application(appRoot, pid, args[1],
			usingAbstractNamespace, ownerPipe));
	}
	
	/**
	 * @throws boost::thread_interrupted
	 */
	ApplicationPtr
	handleSpawnException(const SpawnException &e, const string &appRoot,
	                     bool lowerPrivilege, const string &lowestUser,
	                     const string &environment, const string &spawnMethod,
	                     const string &appType) {
		TRACE_POINT();
		bool restarted;
		try {
			P_DEBUG("Spawn server died. Attempting to restart it...");
			this_thread::disable_syscall_interruption dsi;
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
			return sendSpawnCommand(appRoot, lowerPrivilege, lowestUser,
				environment, spawnMethod, appType);
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
		TRACE_POINT();
		try {
			channel.write("reload", appRoot.c_str(), NULL);
		} catch (const SystemException &e) {
			throw SystemException("Could not write 'reload' command "
				"to the spawn server", e.code());
		}
	}
	
	void handleReloadException(const SystemException &e, const string &appRoot) {
		TRACE_POINT();
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
	 * @param rubyCommand The Ruby interpreter's command.
	 * @param user The user that the spawn manager should run as. This
	 *             parameter only has effect if the current process is
	 *             running as root. If the empty string is given, or if
	 *             the <tt>user</tt> is not a valid username, then
	 *             the spawn manager will be run as the current user.
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 */
	SpawnManager(const string &spawnServerCommand,
	             const string &logFile = "",
	             const string &rubyCommand = "ruby",
	             const string &user = "") {
		TRACE_POINT();
		this->spawnServerCommand = spawnServerCommand;
		this->logFile = logFile;
		this->rubyCommand = rubyCommand;
		this->user = user;
		pid = 0;
		#ifdef TESTING_SPAWN_MANAGER
			nextRestartShouldFail = false;
		#endif
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		try {
			restartServer();
		} catch (const IOException &e) {
			throw prependMessageToException(e, "Could not start the spawn server");
		} catch (const SystemException &e) {
			throw prependMessageToException(e, "Could not start the spawn server");
		}
	}
	
	~SpawnManager() throw() {
		TRACE_POINT();
		if (pid != 0) {
			UPDATE_TRACE_POINT();
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			P_TRACE(2, "Shutting down spawn manager (PID " << pid << ").");
			channel.close();
			syscalls::waitpid(pid, NULL, 0);
			P_TRACE(2, "Spawn manager exited.");
		}
	}
	
	/**
	 * Spawn a new instance of a Ruby on Rails or Rack application.
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
	 * @param environment The RAILS_ENV/RACK_ENV environment that should be used. May not be empty.
	 * @param spawnMethod The spawn method to use. Either "smart" or "conservative".
	 *                    See the Ruby class SpawnManager for details.
	 * @param appType The application type. Either "rails" or "rack".
	 * @return A smart pointer to an Application object, which represents the application
	 *         instance that has been spawned. Use this object to communicate with the
	 *         spawned application.
	 * @throws SpawnException Something went wrong.
	 * @throws boost::thread_interrupted
	 */
	ApplicationPtr spawn(
		const string &appRoot,
		bool lowerPrivilege = true,
		const string &lowestUser = "nobody",
		const string &environment = "production",
		const string &spawnMethod = "smart",
		const string &appType = "rails"
	) {
		TRACE_POINT();
		boost::mutex::scoped_lock l(lock);
		try {
			return sendSpawnCommand(appRoot, lowerPrivilege, lowestUser,
				environment, spawnMethod, appType);
		} catch (const SpawnException &e) {
			if (e.hasErrorPage()) {
				throw;
			} else {
				return handleSpawnException(e, appRoot, lowerPrivilege,
					lowestUser, environment, spawnMethod, appType);
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
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
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
