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
#ifndef _PASSENGER_SPAWN_MANAGER_H_
#define _PASSENGER_SPAWN_MANAGER_H_

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/function.hpp>
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
#include <grp.h>
#include <pwd.h>
#include <signal.h>

#include "AbstractSpawnManager.h"
#include "ServerInstanceDir.h"
#include "FileDescriptor.h"
#include "Constants.h"
#include "MessageChannel.h"
#include "AccountsDatabase.h"
#include "RandomGenerator.h"
#include "Exceptions.h"
#include "Logging.h"
#include "Utils/Base64.h"
#include "Utils/SystemTime.h"
#include "Utils/IOUtils.h"

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

/**
 * An AbstractSpawnManager implementation.
 *
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
 * time a process of the same application is spawned, the spawn time is significantly
 * lower than the first time. Nevertheless, spawning is a relatively expensive operation
 * (compared to the processing of a typical HTTP request/response), and so should be
 * avoided whenever possible.
 *
 * See the documentation of the spawn server for full implementation details.
 *
 * @ingroup Support
 */
class SpawnManager: public AbstractSpawnManager {
private:
	static const int SERVER_SOCKET_FD = 3;
	static const int OWNER_SOCKET_FD  = 4;
	static const int HIGHEST_FD       = OWNER_SOCKET_FD;

	string spawnServerCommand;
	ServerInstanceDir::GenerationPtr generation;
	AccountsDatabasePtr accountsDatabase;
	string rubyCommand;
	AnalyticsLoggerPtr analyticsLogger;
	int logLevel;
	string debugLogFile;
	
	boost::mutex lock;
	RandomGenerator random;
	
	pid_t pid;
	FileDescriptor ownerSocket;
	string socketFilename;
	string socketPassword;
	bool serverNeedsRestart;
	
	static void deleteAccount(AccountsDatabasePtr accountsDatabase, const string &username) {
		accountsDatabase->remove(username);
	}
	
	/**
	 * Restarts the spawn server.
	 *
	 * @pre System call interruption is disabled.
	 * @throws RuntimeException An error occurred while creating a Unix server socket.
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException An error occurred while generating random data.
	 */
	void restartServer() {
		TRACE_POINT();
		if (pid != 0) {
			UPDATE_TRACE_POINT();
			ownerSocket.close();
			
			/* Wait at most 5 seconds for the spawn server to exit.
			 * If that doesn't work, kill it, then wait at most 5 seconds
			 * for it to exit.
			 */
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
			}
			pid = 0;
		}
		
		FileDescriptor serverSocket;
		string socketFilename;
		string socketPassword;
		int ret, fds[2];
		
		UPDATE_TRACE_POINT();
		socketFilename = generation->getPath() + "/spawn-server/socket." +
			toString(getpid()) + "." +
			pointerToIntString(this);
		socketPassword = Base64::encode(random.generateByteString(32));
		serverSocket = createUnixServer(socketFilename.c_str());
		do {
			ret = chmod(socketFilename.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			int e = errno;
			syscalls::unlink(socketFilename.c_str());
			throw FileSystemException("Cannot set permissions on '" + socketFilename + "'",
				e, socketFilename);
		}
		
		if (syscalls::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			int e = errno;
			syscalls::unlink(socketFilename.c_str());
			throw SystemException("Cannot create a Unix socket", e);
		}

		UPDATE_TRACE_POINT();
		pid = syscalls::fork();
		if (pid == 0) {
			dup2(serverSocket, HIGHEST_FD + 1);
			dup2(fds[1], HIGHEST_FD + 2);
			dup2(HIGHEST_FD + 1, SERVER_SOCKET_FD);
			dup2(HIGHEST_FD + 2, OWNER_SOCKET_FD);
			
			// Close all unnecessary file descriptors
			for (long i = sysconf(_SC_OPEN_MAX) - 1; i > HIGHEST_FD; i--) {
				close(i);
			}
			
			execlp(rubyCommand.c_str(),
				rubyCommand.c_str(),
				spawnServerCommand.c_str(),
				/* The spawn server changes the process names of the subservers
				 * that it starts, for better usability. However, the process name length
				 * (as shown by ps) is limited. Here, we try to expand that limit by
				 * deliberately passing a useless whitespace string to the spawn server.
				 * This argument is ignored by the spawn server. This works on some
				 * systems, such as Ubuntu Linux.
				 */
				"                                                             ",
				(char *) NULL);
			int e = errno;
			fprintf(stderr, "*** Passenger ERROR (%s:%d):\n"
				"Could not start the spawn server: %s: %s (%d)\n",
				__FILE__, __LINE__,
				rubyCommand.c_str(), strerror(e), e);
			fflush(stderr);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			syscalls::unlink(socketFilename.c_str());
			syscalls::close(fds[0]);
			syscalls::close(fds[1]);
			pid = 0;
			throw SystemException("Unable to fork a process", e);
		} else {
			FileDescriptor ownerSocket = fds[0];
			syscalls::close(fds[1]);
			serverSocket.close();
			
			// Pass arguments to spawn server.
			writeExact(ownerSocket, socketFilename + "\n");
			writeExact(ownerSocket, socketPassword + "\n");
			writeExact(ownerSocket, generation->getPath() + "\n");
			if (analyticsLogger != NULL) {
				writeExact(ownerSocket, analyticsLogger->getAddress() + "\n");
				writeExact(ownerSocket, analyticsLogger->getUsername() + "\n");
				writeExact(ownerSocket, Base64::encode(analyticsLogger->getPassword()) + "\n");
				writeExact(ownerSocket, analyticsLogger->getNodeName() + "\n");
			} else {
				writeExact(ownerSocket, "\n");
				writeExact(ownerSocket, "\n");
				writeExact(ownerSocket, "\n");
				writeExact(ownerSocket, "\n");
			}
			writeExact(ownerSocket, toString(logLevel) + "\n");
			writeExact(ownerSocket, debugLogFile + "\n");
			
			this->ownerSocket    = ownerSocket;
			this->socketFilename = socketFilename;
			this->socketPassword = socketPassword;
			spawnServerStarted();
		}
	}
	
	/**
	 * Connects to the spawn server and returns the connection.
	 *
	 * @throws RuntimeException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	FileDescriptor connect() const {
		TRACE_POINT();
		FileDescriptor fd = connectToUnixServer(socketFilename.c_str());
		MessageChannel channel(fd);
		UPDATE_TRACE_POINT();
		channel.writeScalar(socketPassword);
		return fd;
	}
	
	/**
	 * Send the spawn command to the spawn server.
	 *
	 * @param PoolOptions The spawn options to use.
	 * @return A Process smart pointer, representing the spawned process.
	 * @throws SpawnException Something went wrong.
	 * @throws Anything thrown by options.environmentVariables->getItems().
	 * @throws boost::thread_interrupted
	 */
	ProcessPtr sendSpawnCommand(const PoolOptions &options) {
		TRACE_POINT();
		FileDescriptor connection;
		MessageChannel channel;
		
		P_DEBUG("Spawning a new application process for " << options.appRoot << "...");
		
		try {
			connection = connect();
			channel = MessageChannel(connection);
		} catch (const SystemException &e) {
			throw SpawnException(string("Could not connect to the spawn server: ") +
				e.sys());
		} catch (const std::exception &e) {
			throw SpawnException(string("Could not connect to the spawn server: ") +
				e.what());
		}
		
		UPDATE_TRACE_POINT();
		vector<string> args;
		string appRoot;
		pid_t appPid;
		int i, nServerSockets, ownerPipe;
		Process::SocketInfoMap serverSockets;
		string detachKey = random.generateAsciiString(43);
		// The connect password must be a URL-friendly string because users will
		// insert it in HTTP headers.
		string connectPassword = random.generateAsciiString(43);
		string gupid = integerToHex(SystemTime::get() / 60) + "-" +
			random.generateAsciiString(11);
		AccountPtr account;
		function<void ()> destructionCallback;
		
		try {
			args.push_back("spawn_application");
			options.toVector(args);
			
			args.push_back("detach_key");
			args.push_back(detachKey);
			args.push_back("connect_password");
			args.push_back(connectPassword);
			if (accountsDatabase != NULL) {
				string username = "_backend-" + toString(accountsDatabase->getUniqueNumber());
				string password = random.generateByteString(MESSAGE_SERVER_MAX_PASSWORD_SIZE);
				account = accountsDatabase->add(username, password, false, options.rights);
				destructionCallback = boost::bind(&SpawnManager::deleteAccount,
					accountsDatabase, username);
				
				args.push_back("pool_account_username");
				args.push_back(username);
				args.push_back("pool_account_password_base64");
				args.push_back(Base64::encode(password));
			}
			
			channel.write(args);
		} catch (const SystemException &e) {
			throw SpawnException(string("Could not write 'spawn_application' "
				"command to the spawn server: ") + e.sys());
		}
		
		try {
			UPDATE_TRACE_POINT();
			// Read status.
			if (!channel.read(args)) {
				throw SpawnException("The spawn server has exited unexpectedly.");
			}
			if (args.size() != 1) {
				throw SpawnException("The spawn server sent an invalid message.");
			}
			if (args[0] == "error_page") {
				UPDATE_TRACE_POINT();
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
			UPDATE_TRACE_POINT();
			if (!channel.read(args)) {
				throw SpawnException("The spawn server has exited unexpectedly.");
			}
			if (args.size() != 3) {
				throw SpawnException("The spawn server sent an invalid message.");
			}
			
			appRoot = args[0];
			appPid  = (pid_t) stringToULL(args[1]);
			nServerSockets = atoi(args[2]);
			
			UPDATE_TRACE_POINT();
			for (i = 0; i < nServerSockets; i++) {
				if (!channel.read(args)) {
					throw SpawnException("The spawn server has exited unexpectedly.");
				}
				if (args.size() != 3) {
					throw SpawnException("The spawn server sent an invalid message.");
				}
				serverSockets[args[0]] = Process::SocketInfo(args[1], args[2]);
			}
			if (serverSockets.find("main") == serverSockets.end()) {
				UPDATE_TRACE_POINT();
				throw SpawnException("The spawn server sent an invalid message.");
			}
		} catch (const SystemException &e) {
			throw SpawnException(string("Could not read from the spawn server: ") + e.sys());
		}
		
		UPDATE_TRACE_POINT();
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
		
		UPDATE_TRACE_POINT();
		P_DEBUG("Application process " << appPid << " spawned");
		return ProcessPtr(new Process(appRoot, appPid, ownerPipe, serverSockets,
			detachKey, connectPassword, gupid, destructionCallback));
	}
	
	/**
	 * @throws boost::thread_interrupted
	 * @throws Anything thrown by options.environmentVariables->getItems().
	 */
	ProcessPtr
	handleSpawnException(const SpawnException &e, const PoolOptions &options) {
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
			return sendSpawnCommand(options);
		} else {
			throw SpawnException("The spawn server died unexpectedly, and restarting it failed.");
		}
	}
	
	/**
	 * Send the reload command to the spawn server.
	 *
	 * @param appRoot The application root to reload.
	 * @throws RuntimeException 
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void sendReloadCommand(const string &appRoot) {
		TRACE_POINT();
		FileDescriptor connection;
		MessageChannel channel;
		
		try {
			connection = connect();
			channel = MessageChannel(connection);
		} catch (SystemException &e) {
			e.setBriefMessage("Could not connect to the spawn server");
			throw;
		} catch (const RuntimeException &e) {
			throw RuntimeException(string("Could not connect to the spawn server: ") +
				e.what());
		}
		
		try {
			channel.write("reload", appRoot.c_str(), NULL);
		} catch (SystemException &e) {
			e.setBriefMessage("Could not write 'reload' command to the spawn server");
			throw;
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

protected:
	/**
	 * A method which is called after the spawn server has started.
	 * It doesn't do anything by default and serves as a hook for unit tests.
	 */
	virtual void spawnServerStarted() { }
	
public:
	/**
	 * Construct a new SpawnManager.
	 *
	 * @param spawnServerCommand The filename of the spawn server to use.
	 * @param generation The server instance dir generation in which
	 *                   generation-specific are stored.
	 * @param accountsDatabase An accounts database. SpawnManager will automatically
	 *                         create a new account for each spawned process, assigning
	 *                         it the rights as set in the PoolOptions object. This
	 *                         account is also automatically deleted when no longer needed.
	 *                         May be a null pointer.
	 * @param rubyCommand The Ruby interpreter's command.
	 * @throws RuntimeException An error occurred while creating a Unix server socket.
	 * @throws SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException An error occurred while generating random data.
	 */
	SpawnManager(const string &spawnServerCommand,
	             const ServerInstanceDir::GenerationPtr &generation,
	             const AccountsDatabasePtr &accountsDatabase = AccountsDatabasePtr(),
	             const string &rubyCommand = "ruby",
	             const AnalyticsLoggerPtr &analyticsLogger = AnalyticsLoggerPtr(),
	             int logLevel = 0,
	             const string &debugLogFile = ""
	) {
		TRACE_POINT();
		this->spawnServerCommand = spawnServerCommand;
		this->generation  = generation;
		this->accountsDatabase = accountsDatabase;
		this->rubyCommand = rubyCommand;
		this->analyticsLogger = analyticsLogger;
		this->logLevel = logLevel;
		this->debugLogFile = debugLogFile;
		pid = 0;
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
	
	virtual ~SpawnManager() {
		TRACE_POINT();
		if (pid != 0) {
			UPDATE_TRACE_POINT();
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			syscalls::unlink(socketFilename.c_str());
			ownerSocket.close();
			syscalls::waitpid(pid, NULL, 0);
		}
	}
	
	virtual ProcessPtr spawn(const PoolOptions &options) {
		TRACE_POINT();
		AnalyticsScopeLog scope(options.log, "spawn app process");
		ProcessPtr result;
		boost::mutex::scoped_lock l(lock);
		
		try {
			result = sendSpawnCommand(options);
		} catch (const SpawnException &e) {
			if (e.hasErrorPage()) {
				throw;
			} else {
				result = handleSpawnException(e, options);
			}
		}
		scope.success();
		return result;
	}
	
	virtual void reload(const string &appRoot) {
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		try {
			return sendReloadCommand(appRoot);
		} catch (const SystemException &e) {
			return handleReloadException(e, appRoot);
		}
	}
	
	virtual void killSpawnServer() const {
		kill(pid, SIGKILL);
	}
	
	virtual pid_t getServerPid() const {
		return pid;
	}
};

/** Convenient alias for SpawnManager smart pointer. */
typedef shared_ptr<SpawnManager> SpawnManagerPtr;

} // namespace Passenger

#endif /* _PASSENGER_SPAWN_MANAGER_H_ */
