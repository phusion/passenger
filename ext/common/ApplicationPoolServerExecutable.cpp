/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_SERVER_EXECUTABLE_H_
#define _PASSENGER_APPLICATION_POOL_SERVER_EXECUTABLE_H_

/*
 * This is the ApplicationPool server executable. See the ApplicationPoolServer
 * class for background information.
 *
 * Each client is handled by a seperate thread. This is necessary because we use
 * StandardApplicationPool, and the current algorithm for StandardApplicationPool::get()
 * can block (in the case that the spawning limit has been exceeded). While it is
 * possible to get around this problem without using threads, a thread-based implementation
 * is easier to write.
 */

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

#include <oxt/system_calls.hpp>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pwd.h>
#include <string>
#include <sstream>
#include <vector>
#include <set>
#include <map>

#include "MessageChannel.h"
#include "StandardApplicationPool.h"
#include "ApplicationPoolStatusReporter.h"
#include "Application.h"
#include "Logging.h"
#include "Exceptions.h"


using namespace boost;
using namespace std;
using namespace oxt;
using namespace Passenger;

class Server;
class Client;
typedef shared_ptr<Client> ClientPtr;

#define SERVER_SOCKET_FD 3

// The following variables contain pre-calculated data which are used by
// Server::fatalSignalHandler(). It's not safe to allocate memory inside
// a signal handler.

/** The filename of the ApplicationPoolServerExecutable binary. */
static string exeFile;
static string gdbBacktraceGenerationCommand;
static const char *gdbBacktraceGenerationCommandStr;


/*****************************************
 * Server
 *****************************************/

class Server {
private:
	friend class Client;

	int serverSocket;
	StandardApplicationPoolPtr pool;
	set<ClientPtr> clients;
	boost::mutex lock;
	string user;

	/**
	 * Lowers this process's privilege to that of <em>username</em>,
	 * and sets stricter permissions for the Phusion Passenger temp
	 * directory.
	 */
	void lowerPrivilege(const string &username) {
		struct passwd *entry;
		
		entry = getpwnam(username.c_str());
		if (entry != NULL) {
			if (initgroups(username.c_str(), entry->pw_gid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
					"privilege to that of user '" << username <<
					"': cannot set supplementary groups for this "
					"user: " << strerror(e) << " (" << e << ")");
			}
			if (setgid(entry->pw_gid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
					"privilege to that of user '" << username <<
					"': cannot set group ID: " << strerror(e) <<
					" (" << e << ")");
			}
			if (setuid(entry->pw_uid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
					"privilege to that of user '" << username <<
					"': cannot set user ID: " << strerror(e) <<
					" (" << e << ")");
			}
		} else {
			P_WARN("WARNING: Unable to lower ApplicationPoolServerExecutable's "
				"privilege to that of user '" << username <<
				"': user does not exist.");
		}
	}
	
	static void fatalSignalHandler(int signum) {
		char message[1024];
		
		snprintf(message, sizeof(message) - 1,
			"*** ERROR: ApplicationPoolServerExecutable received fatal signal "
			"%d. Running gdb to obtain the backtrace:\n\n",
			signum);
		message[sizeof(message) - 1] = '\0';
		write(STDERR_FILENO, message, strlen(message));
		write(STDERR_FILENO, "----------------- Begin gdb output -----------------\n",
			sizeof("----------------- Begin gdb output -----------------\n") - 1);
		system(gdbBacktraceGenerationCommandStr);
		write(STDERR_FILENO, "----------------- End gdb output -----------------\n",
			sizeof("----------------- End gdb output -----------------\n") - 1);
		
		// Invoke default signal handler.
		kill(getpid(), signum);
	}
	
	void setupSignalHandlers() {
		// Ignore SIGPIPE and SIGHUP.
		struct sigaction action;
		action.sa_handler = SIG_IGN;
		action.sa_flags   = 0;
		sigemptyset(&action.sa_mask);
		sigaction(SIGPIPE, &action, NULL);
		sigaction(SIGHUP, &action, NULL);
		
		// Setup handlers for other signals.
		FILE *f;
		string gdbCommandFile;
		
		gdbCommandFile = getPassengerTempDir() + "/info/gdb_backtrace_command.txt";
		f = fopen(gdbCommandFile.c_str(), "w");
		if (f != NULL) {
			// Write a file which contains commands for gdb to obtain
			// the backtrace of this process.
			fprintf(f, "attach %lu\n", (unsigned long) getpid());
			fprintf(f, "thread apply all bt full\n");
			fclose(f);
			chmod(gdbCommandFile.c_str(), S_IRUSR | S_IRGRP | S_IROTH);
			
			gdbBacktraceGenerationCommand = "gdb -n -batch -x \"";
			gdbBacktraceGenerationCommand.append(gdbCommandFile);
			gdbBacktraceGenerationCommand.append("\" < /dev/null");
			gdbBacktraceGenerationCommandStr = gdbBacktraceGenerationCommand.c_str();
			
			// Install the signal handlers.
			action.sa_handler = fatalSignalHandler;
			action.sa_flags   = SA_RESETHAND;
			sigemptyset(&action.sa_mask);
			sigaction(SIGQUIT, &action, NULL);
			sigaction(SIGILL,  &action, NULL);
			sigaction(SIGABRT, &action, NULL);
			sigaction(SIGFPE,  &action, NULL);
			sigaction(SIGBUS,  &action, NULL);
			sigaction(SIGSEGV, &action, NULL);
			sigaction(SIGALRM, &action, NULL);
			sigaction(SIGUSR1, &action, NULL);
		}
	}

public:
	Server(int serverSocket,
	       const unsigned int logLevel,
	       const string &spawnServerCommand,
	       const string &logFile,
	       const string &rubyCommand,
	       const string &user,
	       const string &passengerTempDir)
	{
		setPassengerTempDir(passengerTempDir);
		
		pool = ptr(new StandardApplicationPool(spawnServerCommand,
			logFile, rubyCommand, user));
		Passenger::setLogLevel(logLevel);
		this->serverSocket = serverSocket;
		this->user = user;
		
		P_TRACE(2, "ApplicationPoolServerExecutable initialized (PID " << getpid() << ")");
	}
	
	~Server() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		
		P_TRACE(2, "Shutting down server.");
		
		syscalls::close(serverSocket);
		
		// Wait for all clients to disconnect.
		UPDATE_TRACE_POINT();
		set<ClientPtr> clientsCopy;
		{
			/* If we clear _clients_ directly, then it may result in a deadlock.
			 * So we make a copy of the set inside a critical section in order to increase
			 * the reference counts, and then we release all references outside the critical
			 * section.
			 */
			boost::mutex::scoped_lock l(lock);
			clientsCopy = clients;
			clients.clear();
		}
		clientsCopy.clear();
		
		P_TRACE(2, "Server shutdown complete.");
	}
	
	int start(); // Will be defined later, because Client depends on Server's interface.
};


/*****************************************
 * Client
 *****************************************/

/**
 * Represents a single ApplicationPool client, connected to this server.
 *
 * @invariant
 * The life time of a Client object is guaranteed to be less than
 * that of its associated Server object.
 */
class Client {
private:
	static const int CLIENT_THREAD_STACK_SIZE = 1024 * 64;

	/** The Server that this Client object belongs to. */
	Server &server;
	
	/** The connection to the client. */
	int fd;
	MessageChannel channel;
	
	/** The thread which handles the client connection. */
	oxt::thread *thr;
	
	/**
	 * Maps session ID to sessions created by ApplicationPool::get(). Session IDs
	 * are sent back to the ApplicationPool client. This allows the ApplicationPool
	 * client to tell us which of the multiple sessions it wants to close, later on.
	 */
	map<int, Application::SessionPtr> sessions;
	
	/** Last used session ID. */
	int lastSessionID;
	
	class ClientCommunicationError: public oxt::tracable_exception {
	private:
		string briefMessage;
		string systemMessage;
		string fullMessage;
		int m_code;
	public:
		/**
		 * Create a new ClientCommunicationError.
		 *
		 * @param briefMessage A brief message describing the error.
		 * @param errorCode An optional error code, i.e. the value of errno right after the error occured, if applicable.
		 * @note A system description of the error will be appended to the given message.
		 *    For example, if <tt>errorCode</tt> is <tt>EBADF</tt>, and <tt>briefMessage</tt>
		 *    is <em>"Something happened"</em>, then what() will return <em>"Something happened: Bad
		 *    file descriptor (10)"</em> (if 10 is the number for EBADF).
		 * @post code() == errorCode
		 * @post brief() == briefMessage
		 */
		ClientCommunicationError(const string &briefMessage, int errorCode = -1) {
			if (errorCode != -1) {
				stringstream str;
				
				str << strerror(errorCode) << " (" << errorCode << ")";
				systemMessage = str.str();
			}
			setBriefMessage(briefMessage);
			m_code = errorCode;
		}

		virtual ~ClientCommunicationError() throw() {}

		virtual const char *what() const throw() {
			return fullMessage.c_str();
		}

		void setBriefMessage(const string &message) {
			briefMessage = message;
			if (systemMessage.empty()) {
				fullMessage = briefMessage;
			} else {
				fullMessage = briefMessage + ": " + systemMessage;
			}
		}

		/**
		 * The value of <tt>errno</tt> at the time the error occured.
		 */
		int code() const throw() {
			return m_code;
		}

		/**
		 * Returns a brief version of the exception message. This message does
		 * not include the system error description, and is equivalent to the
		 * value of the <tt>message</tt> parameter as passed to the constructor.
		 */
		string brief() const throw() {
			return briefMessage;
		}

		/**
		 * Returns the system's error message. This message contains both the
		 * content of <tt>strerror(errno)</tt> and the errno number itself.
		 *
		 * @post if code() == -1: result.empty()
		 */
		string sys() const throw() {
			return systemMessage;
		}
	};
	
	/**
	 * A StringListCreator which fetches its items from the client.
	 * Used as an optimization for ApplicationPoolServer::Client.get():
	 * environment variables are only serialized by the client process
	 * if a new backend process is being spawned.
	 */
	class EnvironmentVariablesFetcher: public StringListCreator {
	private:
		MessageChannel &channel;
		PoolOptions &options;
	public:
		EnvironmentVariablesFetcher(MessageChannel &theChannel, PoolOptions &theOptions)
			: channel(theChannel),
			  options(theOptions)
		{ }
		
		/**
		 * @throws ClientCommunicationError
		 */
		virtual const StringListPtr getItems() const {
			string data;
			
			/* If an I/O error occurred while communicating with the client,
			 * then throw a ClientCommunicationException, which will bubble
			 * all the way up to the thread main loop, where the connection
			 * with the client will be broken.
			 */
			try {
				channel.write("getEnvironmentVariables", NULL);
			} catch (const SystemException &e) {
				throw ClientCommunicationError(
					"Unable to send a 'getEnvironmentVariables' request to the client",
					e.code());
			}
			try {
				if (!channel.readScalar(data)) {
					throw ClientCommunicationError("Unable to read a reply from the client for the 'getEnvironmentVariables' request.");
				}
			} catch (const SystemException &e) {
				throw ClientCommunicationError(
					"Unable to read a reply from the client for the 'getEnvironmentVariables' request",
					e.code());
			}
			
			if (!data.empty()) {
				SimpleStringListCreator list(data);
				return list.getItems();
			} else {
				return ptr(new StringList());
			}
		}
	};
	
	void processGet(const vector<string> &args) {
		TRACE_POINT();
		Application::SessionPtr session;
		bool failed = false;
		
		try {
			PoolOptions options(args, 1);
			options.environmentVariables = ptr(new EnvironmentVariablesFetcher(channel, options));
			session = server.pool->get(options);
			sessions[lastSessionID] = session;
			lastSessionID++;
		} catch (const SpawnException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			
			if (e.hasErrorPage()) {
				P_TRACE(3, "Client " << this << ": SpawnException "
					"occured (with error page)");
				channel.write("SpawnException", e.what(), "true", NULL);
				channel.writeScalar(e.getErrorPage());
			} else {
				P_TRACE(3, "Client " << this << ": SpawnException "
					"occured (no error page)");
				channel.write("SpawnException", e.what(), "false", NULL);
			}
			failed = true;
		} catch (const BusyException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			channel.write("BusyException", e.what(), NULL);
			failed = true;
		} catch (const IOException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			channel.write("IOException", e.what(), NULL);
			failed = true;
		}
		UPDATE_TRACE_POINT();
		if (!failed) {
			this_thread::disable_syscall_interruption dsi;
			try {
				UPDATE_TRACE_POINT();
				channel.write("ok", toString(session->getPid()).c_str(),
					toString(lastSessionID - 1).c_str(), NULL);
				UPDATE_TRACE_POINT();
				channel.writeFileDescriptor(session->getStream());
				UPDATE_TRACE_POINT();
				session->closeStream();
			} catch (const exception &e) {
				P_TRACE(3, "Client " << this << ": could not send "
					"'ok' back to the ApplicationPool client: " <<
					e.what());
				sessions.erase(lastSessionID - 1);
				throw;
			}
		}
	}
	
	void processClose(const vector<string> &args) {
		TRACE_POINT();
		sessions.erase(atoi(args[1]));
	}
	
	void processClear(const vector<string> &args) {
		TRACE_POINT();
		server.pool->clear();
	}
	
	void processSetMaxIdleTime(const vector<string> &args) {
		TRACE_POINT();
		server.pool->setMaxIdleTime(atoi(args[1]));
	}
	
	void processSetMax(const vector<string> &args) {
		TRACE_POINT();
		server.pool->setMax(atoi(args[1]));
	}
	
	void processGetActive(const vector<string> &args) {
		TRACE_POINT();
		channel.write(toString(server.pool->getActive()).c_str(), NULL);
	}
	
	void processGetCount(const vector<string> &args) {
		TRACE_POINT();
		channel.write(toString(server.pool->getCount()).c_str(), NULL);
	}
	
	void processSetMaxPerApp(unsigned int maxPerApp) {
		TRACE_POINT();
		server.pool->setMaxPerApp(maxPerApp);
	}
	
	void processGetSpawnServerPid(const vector<string> &args) {
		TRACE_POINT();
		channel.write(toString(server.pool->getSpawnServerPid()).c_str(), NULL);
	}
	
	void processUnknownMessage(const vector<string> &args) {
		TRACE_POINT();
		string name;
		if (args.empty()) {
			name = "(null)";
		} else {
			name = args[0];
		}
		P_WARN("An ApplicationPool client sent an invalid command: "
			<< name << " (" << args.size() << " elements)");
	}
	
	/**
	 * The entry point of the thread that handles the client connection.
	 */
	void threadMain(const weak_ptr<Client> self) {
		TRACE_POINT();
		vector<string> args;
		try {
			while (!this_thread::interruption_requested()) {
				UPDATE_TRACE_POINT();
				try {
					if (!channel.read(args)) {
						// Client closed connection.
						break;
					}
				} catch (const SystemException &e) {
					P_TRACE(2, "Exception in ApplicationPoolServer client thread during "
						"reading of a message: " << e.what());
					break;
				}

				P_TRACE(4, "Client " << this << ": received message: " <<
					toString(args));
				
				UPDATE_TRACE_POINT();
				if (args[0] == "get") {
					processGet(args);
				} else if (args[0] == "close" && args.size() == 2) {
					processClose(args);
				} else if (args[0] == "clear" && args.size() == 1) {
					processClear(args);
				} else if (args[0] == "setMaxIdleTime" && args.size() == 2) {
					processSetMaxIdleTime(args);
				} else if (args[0] == "setMax" && args.size() == 2) {
					processSetMax(args);
				} else if (args[0] == "getActive" && args.size() == 1) {
					processGetActive(args);
				} else if (args[0] == "getCount" && args.size() == 1) {
					processGetCount(args);
				} else if (args[0] == "setMaxPerApp" && args.size() == 2) {
					processSetMaxPerApp(atoi(args[1]));
				} else if (args[0] == "getSpawnServerPid" && args.size() == 1) {
					processGetSpawnServerPid(args);
				} else {
					processUnknownMessage(args);
					break;
				}
				args.clear();
			}
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "Client thread " << this << " interrupted.");
		} catch (const tracable_exception &e) {
			P_TRACE(2, "Uncaught exception in ApplicationPoolServer client thread:\n"
				<< "   message: " << toString(args) << "\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace:\n" << e.backtrace());
		} catch (const exception &e) {
			P_TRACE(2, "Uncaught exception in ApplicationPoolServer client thread:\n"
				<< "   message: " << toString(args) << "\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace: not available");
		} catch (...) {
			P_TRACE(2, "Uncaught unknown exception in ApplicationPool client thread.");
			throw;
		}
		
		UPDATE_TRACE_POINT();
		boost::mutex::scoped_lock l(server.lock);
		ClientPtr myself(self.lock());
		if (myself != NULL) {
			server.clients.erase(myself);
		}
	}

public:
	/**
	 * Create a new Client object.
	 *
	 * @param the_server The Server object that this Client belongs to.
	 * @param connection The connection to the ApplicationPool client.
	 *
	 * @note
	 * <tt>connection</tt> will be closed upon destruction
	 */
	Client(Server &the_server, int connection)
		: server(the_server),
		  fd(connection),
		  channel(connection) {
		thr = NULL;
		lastSessionID = 0;
	}
	
	/**
	 * Start the thread for handling the connection with this client.
	 *
	 * @param self The iterator of this Client object inside the server's
	 *        <tt>clients</tt> set. This is used to remove itself from
	 *        the <tt>clients</tt> set once the client has closed the
	 *        connection.
	 */
	void start(const weak_ptr<Client> self) {
		stringstream name;
		name << "Client " << fd;
		thr = new oxt::thread(
			bind(&Client::threadMain, this, self),
			name.str(), CLIENT_THREAD_STACK_SIZE
		);
	}
	
	~Client() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		
		if (thr != NULL) {
			if (thr->get_id() != this_thread::get_id()) {
				thr->interrupt_and_join();
			}
			delete thr;
		}
		syscalls::close(fd);
	}
};

int
Server::start() {
	TRACE_POINT();
	setup_syscall_interruption_support();
	
	try {
		uid_t fifoUid;
		gid_t fifoGid;
		
		// Set the FIFO's owner according to whether we're running as root
		// and whether user switching is enabled.
		if (geteuid() == 0 && !user.empty()) {
			determineLowestUserAndGroup(user, fifoUid, fifoGid);
		} else {
			fifoUid = (uid_t) -1;
			fifoGid = (gid_t) -1;
		}
		ApplicationPoolStatusReporter reporter(pool, user.empty(),
			S_IRUSR | S_IWUSR, fifoUid, fifoGid);
		
		if (!user.empty()) {
			lowerPrivilege(user);
		}
		
		setupSignalHandlers();
		
		while (!this_thread::interruption_requested()) {
			int fds[2], ret;
			char x;
			
			// The received data only serves to wake up the server socket,
			// and is not important.
			UPDATE_TRACE_POINT();
			ret = syscalls::read(serverSocket, &x, 1);
			if (ret == 0) {
				// All web server processes disconnected from this server.
				// So we can safely quit.
				break;
			}
			
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			
			// We have an incoming connect request from an
			// ApplicationPool client.
			UPDATE_TRACE_POINT();
			do {
				ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				UPDATE_TRACE_POINT();
				throw SystemException("Cannot create an anonymous Unix socket", errno);
			}
			
			UPDATE_TRACE_POINT();
			MessageChannel(serverSocket).writeFileDescriptor(fds[1], false);
			syscalls::close(fds[1]);
			
			UPDATE_TRACE_POINT();
			ClientPtr client(new Client(*this, fds[0]));
			pair<set<ClientPtr>::iterator, bool> p;
			{
				UPDATE_TRACE_POINT();
				boost::mutex::scoped_lock l(lock);
				clients.insert(client);
			}
			UPDATE_TRACE_POINT();
			client->start(client);
		}
	} catch (const boost::thread_interrupted &) {
		P_TRACE(2, "Main thread interrupted.");
	}
	return 0;
}

int
main(int argc, char *argv[]) {
	try {
		exeFile = argv[0];
		Server server(SERVER_SOCKET_FD, atoi(argv[1]),
			argv[2], argv[3], argv[4], argv[5], argv[6]);
		return server.start();
	} catch (const tracable_exception &e) {
		P_ERROR("*** Fatal error: " << e.what() << "\n" << e.backtrace());
		return 1;
	} catch (const exception &e) {
		P_ERROR("*** Fatal error: " << e.what());
		return 1;
	} catch (...) {
		P_ERROR("*** Fatal error: Unknown exception thrown in main thread.");
		throw;
	}
}

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_EXECUTABLE_H_ */
