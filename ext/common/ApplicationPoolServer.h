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
#ifndef _PASSENGER_APPLICATION_POOL_SERVER_H_
#define _PASSENGER_APPLICATION_POOL_SERVER_H_

#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <cstdio>
#include <cstdlib>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include "MessageChannel.h"
#include "ApplicationPool.h"
#include "Application.h"
#include "Exceptions.h"
#include "Logging.h"

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * Multi-process usage support for ApplicationPool.
 *
 * ApplicationPoolServer implements a client/server architecture for ApplicationPool.
 * This allows one to use ApplicationPool in a multi-process environment (unlike
 * StandardApplicationPool). The cache/pool data is stored in the server. Different
 * processes can then access the pool through the server.
 *
 * ApplicationPoolServer itself does not inherit ApplicationPool. Instead, it returns
 * an ApplicationPool object via the connect() call. For example:
 * @code
 *   // Create an ApplicationPoolServer.
 *   ApplicationPoolServer server(...);
 *   
 *   // Now fork a child process, like Apache's prefork MPM eventually will.
 *   pid_t pid = fork();
 *   if (pid == 0) {
 *       // Child process
 *       
 *       // Connect to the server. After connection, we have an ApplicationPool
 *       // object!
 *       ApplicationPoolPtr pool(server.connect());
 *
 *       // We don't need to connect to the server anymore, so we detach from it.
 *       // This frees up some resources, such as file descriptors.
 *       server.detach();
 *
 *       ApplicationPool::SessionPtr session(pool->get("/home/webapps/foo"));
 *       do_something_with(session);
 *
 *       _exit(0);
 *   } else {
 *       // Parent process
 *       waitpid(pid, NULL, 0);
 *   }
 * @endcode
 *
 * <h2>Implementation notes</h2>
 *
 * <h3>Separate server executable</h3>
 * The actual server is implemented in ApplicationPoolServerExecutable.cpp, this class is
 * just a convenience class for starting/stopping the server executable and connecting
 * to it.
 *
 * In the past, the server logic itself was implemented in this class. This implies that
 * the ApplicationPool server ran inside the Apache process. This presented us with several
 * problems:
 * - Because of the usage of threads in the ApplicationPool server, the Apache VM size would
 *   go way up. This gave people the (wrong) impression that Passenger uses a lot of memory,
 *   or that it leaks memory.
 * - Although it's not entirely confirmed, we suspect that it caused heap fragmentation as
 *   well. Apache allocates lots and lots of small objects on the heap, and ApplicationPool
 *   server isn't exactly helping. This too gave people the (wrong) impression that
 *   Passenger leaks memory.
 * - It would unnecessarily bloat the VM size of Apache worker processes.
 * - We had to resort to all kinds of tricks to make sure that fork()ing a process doesn't
 *   result in file descriptor leaks.
 * - Despite everything, there was still a small chance that file descriptor leaks would
 *   occur, and this could not be fixed. The reason for this is that the Apache control
 *   process may call fork() right after the ApplicationPool server has established a new
 *   connection with a client.
 *
 * Because of these problems, it was decided to split the ApplicationPool server to a
 * separate executable. This comes with no performance hit.
 *
 * <h3>Anonymous server socket</h3>
 * Notice that ApplicationPoolServer does do not use TCP sockets at all, or even named Unix
 * sockets, despite being a server that can handle multiple clients! So ApplicationPoolServer
 * will expose no open ports or temporary Unix socket files. Only child processes are able
 * to use the ApplicationPoolServer.
 *
 * This is implemented through anonymous Unix sockets (<tt>socketpair()</tt>) and file descriptor
 * passing. It allows one to emulate <tt>accept()</tt>. ApplicationPoolServer is connected to
 * the server executable through a Unix socket pair. connect() sends a connect request to the
 * server through that socket. The server will then create a new socket pair, and pass one of
 * them back. This new socket pair represents the newly established connection.
 *
 * @ingroup Support
 */
class ApplicationPoolServer {
private:
	/**
	 * Contains data shared between RemoteSession and Client.
	 * Since RemoteSession and Client have different life times, i.e. one may be
	 * destroyed before the other, they both use a smart pointer that points to
	 * a SharedData. This way, the SharedData object is only destroyed when
	 * both the RemoteSession and the Client object has been destroyed.
	 */
	struct SharedData {
		/**
		 * The socket connection to the ApplicationPool server, as was
		 * established by ApplicationPoolServer::connect().
		 *
		 * The value may be -1, which indicates that the connection has
		 * been closed.
		 */
		int server;
		
		mutable boost::mutex lock;
		
		~SharedData() {
			TRACE_POINT();
			if (server != -1) {
				disconnect();
			}
		}
		
		/**
		 * Disconnect from the ApplicationPool server.
		 */
		void disconnect() {
			TRACE_POINT();
			int ret;
			do {
				ret = close(server);
			} while (ret == -1 && errno == EINTR);
			server = -1;
		}
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;
	
	/**
	 * An Application::Session which works together with ApplicationPoolServer.
	 */
	class RemoteSession: public Application::Session {
	private:
		SharedDataPtr data;
		int id;
		int fd;
		pid_t pid;
	public:
		RemoteSession(SharedDataPtr data, pid_t pid, int id, int fd) {
			this->data = data;
			this->pid = pid;
			this->id = id;
			this->fd = fd;
		}
		
		virtual ~RemoteSession() {
			closeStream();
			boost::mutex::scoped_lock(data->lock);
			MessageChannel(data->server).write("close", toString(id).c_str(), NULL);
		}
		
		virtual int getStream() const {
			return fd;
		}
		
		virtual void setReaderTimeout(unsigned int msec) {
			MessageChannel(fd).setReadTimeout(msec);
		}
		
		virtual void setWriterTimeout(unsigned int msec) {
			MessageChannel(fd).setWriteTimeout(msec);
		}
		
		virtual void shutdownReader() {
			if (fd != -1) {
				int ret = syscalls::shutdown(fd, SHUT_RD);
				if (ret == -1) {
					throw SystemException("Cannot shutdown the reader stream",
						errno);
				}
			}
		}
		
		virtual void shutdownWriter() {
			if (fd != -1) {
				int ret = syscalls::shutdown(fd, SHUT_WR);
				if (ret == -1) {
					throw SystemException("Cannot shutdown the writer stream",
						errno);
				}
			}
		}
		
		virtual void closeStream() {
			if (fd != -1) {
				int ret = syscalls::close(fd);
				fd = -1;
				if (ret == -1) {
					if (errno == EIO) {
						throw SystemException("A write operation on the session stream failed",
							errno);
					} else {
						throw SystemException("Cannot close the session stream",
							errno);
					}
				}
			}
		}
		
		virtual void discardStream() {
			fd = -1;
		}
		
		virtual pid_t getPid() const {
			return pid;
		}
	};
	
	/**
	 * An ApplicationPool implementation that works together with ApplicationPoolServer.
	 * It doesn't do much by itself, its job is mostly to forward queries/commands to
	 * the server and returning the result. Most of the logic is in the server executable.
	 */
	class Client: public ApplicationPool {
	private:
		// The smart pointer only serves to keep the shared data alive.
		// We access the shared data via a normal pointer, for performance.
		SharedDataPtr dataSmartPointer;
		SharedData *data;
		
	public:
		/**
		 * Create a new Client.
		 *
		 * @param sock The newly established socket connection with the ApplicationPoolServer.
		 */
		Client(int sock) {
			dataSmartPointer = ptr(new SharedData());
			data = dataSmartPointer.get();
			data->server = sock;
		}
		
		virtual bool connected() const {
			boost::mutex::scoped_lock(data->lock);
			return data->server != -1;
		}
		
		virtual void clear() {
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			try {
				channel.write("clear", NULL);
			} catch (...) {
				data->disconnect();
				throw;
			}
		}
		
		virtual void setMaxIdleTime(unsigned int seconds) {
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			try {
				channel.write("setMaxIdleTime", toString(seconds).c_str(), NULL);
			} catch (...) {
				data->disconnect();
				throw;
			}
		}
		
		virtual void setMax(unsigned int max) {
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			try {
				channel.write("setMax", toString(max).c_str(), NULL);
			} catch (...) {
				data->disconnect();
				throw;
			}
		}
		
		virtual unsigned int getActive() const {
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			vector<string> args;
			
			try {
				channel.write("getActive", NULL);
				channel.read(args);
				return atoi(args[0].c_str());
			} catch (...) {
				data->disconnect();
				throw;
			}
		}
		
		virtual unsigned int getCount() const {
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			vector<string> args;
			
			try {
				channel.write("getCount", NULL);
				channel.read(args);
				return atoi(args[0].c_str());
			} catch (...) {
				data->disconnect();
				throw;
			}
		}
		
		virtual void setMaxPerApp(unsigned int max) {
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			try {
				channel.write("setMaxPerApp", toString(max).c_str(), NULL);
			} catch (...) {
				data->disconnect();
				throw;
			}
		}
		
		virtual pid_t getSpawnServerPid() const {
			this_thread::disable_syscall_interruption dsi;
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			vector<string> args;
			
			try {
				channel.write("getSpawnServerPid", NULL);
				channel.read(args);
				return atoi(args[0].c_str());
			} catch (...) {
				data->disconnect();
				throw;
			}
		}
		
		virtual Application::SessionPtr get(const PoolOptions &options) {
			this_thread::disable_syscall_interruption dsi;
			TRACE_POINT();
			
			MessageChannel channel(data->server);
			boost::mutex::scoped_lock l(data->lock);
			vector<string> args;
			int stream;
			bool result;
			bool serverMightNeedEnvironmentVariables = true;
			
			/* Send a 'get' request to the ApplicationPool server.
			 * For efficiency reasons, we do not send the data for
			 * options.environmentVariables over the wire yet until
			 * it's necessary.
			 */
			try {
				vector<string> args;
				
				args.push_back("get");
				options.toVector(args, false);
				channel.write(args);
			} catch (const SystemException &e) {
				UPDATE_TRACE_POINT();
				data->disconnect();
				
				string message("Could not send data to the ApplicationPool server: ");
				message.append(e.brief());
				throw SystemException(message, e.code());
			}
			
			/* The first few replies from the server might be for requesting
			 * environment variables in the pool options object, so keep handling
			 * these requests until we receive a different reply.
			 */
			while (serverMightNeedEnvironmentVariables) {
				try {
					result = channel.read(args);
				} catch (const SystemException &e) {
					UPDATE_TRACE_POINT();
					data->disconnect();
					throw SystemException("Could not read a response from "
						"the ApplicationPool server for the 'get' command", e.code());
				}
				if (!result) {
					UPDATE_TRACE_POINT();
					data->disconnect();
					throw IOException("The ApplicationPool server unexpectedly "
						"closed the connection while we're reading a response "
						"for the 'get' command.");
				}
				
				if (args[0] == "getEnvironmentVariables") {
					try {
						if (options.environmentVariables) {
							UPDATE_TRACE_POINT();
							channel.writeScalar(options.serializeEnvironmentVariables());
						} else {
							UPDATE_TRACE_POINT();
							channel.writeScalar("");
						}
					} catch (const SystemException &e) {
						data->disconnect();
						throw SystemException("Could not send a response "
							"for the 'getEnvironmentVariables' request "
							"to the ApplicationPool server",
							e.code());
					}
				} else {
					serverMightNeedEnvironmentVariables = false;
				}
			}
			
			/* We've now received a reply other than "getEnvironmentVariables".
			 * Handle this...
			 */
			if (args[0] == "ok") {
				UPDATE_TRACE_POINT();
				pid_t pid = (pid_t) atol(args[1]);
				int sessionID = atoi(args[2]);
				
				try {
					stream = channel.readFileDescriptor();
				} catch (...) {
					UPDATE_TRACE_POINT();
					data->disconnect();
					throw;
				}
				
				return ptr(new RemoteSession(dataSmartPointer,
					pid, sessionID, stream));
			} else if (args[0] == "SpawnException") {
				UPDATE_TRACE_POINT();
				if (args[2] == "true") {
					string errorPage;
					
					try {
						result = channel.readScalar(errorPage);
					} catch (...) {
						data->disconnect();
						throw;
					}
					if (!result) {
						throw IOException("The ApplicationPool server "
							"unexpectedly closed the connection while "
							"we're reading the error page data.");
					}
					throw SpawnException(args[1], errorPage);
				} else {
					throw SpawnException(args[1]);
				}
			} else if (args[0] == "BusyException") {
				UPDATE_TRACE_POINT();
				throw BusyException(args[1]);
			} else if (args[0] == "IOException") {
				UPDATE_TRACE_POINT();
				data->disconnect();
				throw IOException(args[1]);
			} else {
				UPDATE_TRACE_POINT();
				data->disconnect();
				throw IOException("The ApplicationPool server returned "
					"an unknown message: " + toString(args));
			}
		}
	};
	
	
	static const int SERVER_SOCKET_FD = 3;
	
	string m_serverExecutable;
	string m_spawnServerCommand;
	string m_logFile;
	string m_rubyCommand;
	string m_user;
	
	/**
	 * The PID of the ApplicationPool server process. If no server process
	 * is running, then <tt>serverPid == 0</tt>.
	 *
	 * @invariant
	 *    if serverPid == 0:
	 *       serverSocket == -1
	 */
	pid_t serverPid;
	
	/**
	 * The connection to the ApplicationPool server process. If no server
	 * process is running, then <tt>serverSocket == -1</tt>.
	 *
	 * @invariant
	 *    if serverPid == 0:
	 *       serverSocket == -1
	 */
	int serverSocket;
	
	/**
	 * Shutdown the currently running ApplicationPool server process.
	 *
	 * @pre System call interruption is disabled.
	 * @pre serverSocket != -1 && serverPid != 0
	 * @post serverSocket == -1 && serverPid == 0
	 */
	void shutdownServer() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		int ret, status;
		time_t begin;
		bool done = false;
		
		syscalls::close(serverSocket);
		
		P_TRACE(2, "Waiting for existing ApplicationPoolServerExecutable (PID " <<
			serverPid << ") to exit...");
		begin = syscalls::time(NULL);
		while (!done && syscalls::time(NULL) < begin + 5) {
			/*
			 * Some Apache modules fork(), but don't close file descriptors.
			 * mod_wsgi is one such example. Because of that, closing serverSocket
			 * won't always cause the ApplicationPool server to exit. So we send it a
			 * signal. This must be the same as the oxt/system_calls.hpp interruption
			 * signal.
			 */
			syscalls::kill(serverPid, oxt::INTERRUPTION_SIGNAL);
			
			ret = syscalls::waitpid(serverPid, &status, WNOHANG);
			done = ret > 0 || ret == -1;
			if (!done) {
				syscalls::usleep(100000);
			}
		}
		if (done) {
			if (ret > 0) {
				if (WIFEXITED(status)) {
					P_TRACE(2, "ApplicationPoolServerExecutable exited with exit status " <<
						WEXITSTATUS(status) << ".");
				} else if (WIFSIGNALED(status)) {
					P_TRACE(2, "ApplicationPoolServerExecutable exited because of signal " <<
						WTERMSIG(status) << ".");
				} else {
					P_TRACE(2, "ApplicationPoolServerExecutable exited for an unknown reason.");
				}
			} else {
				P_TRACE(2, "ApplicationPoolServerExecutable exited.");
			}
		} else {
			P_DEBUG("ApplicationPoolServerExecutable not exited in time. Killing it...");
			syscalls::kill(serverPid, SIGKILL);
			syscalls::waitpid(serverPid, NULL, 0);
		}
		
		serverSocket = -1;
		serverPid = 0;
	}
	
	/**
	 * Start an ApplicationPool server process. If there's already one running,
	 * then the currently running one will be shutdown.
	 *
	 * @pre System call interruption is disabled.
	 * @post serverSocket != -1 && serverPid != 0
	 * @throw SystemException Something went wrong.
	 */
	void restartServer() {
		TRACE_POINT();
		int fds[2];
		pid_t pid;
		
		if (serverPid != 0) {
			shutdownServer();
		}
		
		if (syscalls::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket pair", errno);
		}
		
		pid = syscalls::fork();
		if (pid == 0) { // Child process.
			dup2(STDERR_FILENO, STDOUT_FILENO);  // Redirect stdout to the same channel as stderr.
			dup2(fds[0], SERVER_SOCKET_FD);
			
			// Close all unnecessary file descriptors
			for (long i = sysconf(_SC_OPEN_MAX) - 1; i > SERVER_SOCKET_FD; i--) {
				close(i);
			}
			
			execlp(
				#if 0
					"valgrind",
					"valgrind",
				#else
					m_serverExecutable.c_str(),
				#endif
				m_serverExecutable.c_str(),
				toString(Passenger::getLogLevel()).c_str(),
				m_spawnServerCommand.c_str(),
				m_logFile.c_str(),
				m_rubyCommand.c_str(),
				m_user.c_str(),
				getPassengerTempDir().c_str(),
				(char *) 0);
			int e = errno;
			fprintf(stderr, "*** Passenger ERROR (%s:%d):\n"
				"Cannot execute %s: %s (%d)\n",
				__FILE__, __LINE__,
				m_serverExecutable.c_str(), strerror(e), e);
			fflush(stderr);
			_exit(1);
		} else if (pid == -1) { // Error.
			syscalls::close(fds[0]);
			syscalls::close(fds[1]);
			throw SystemException("Cannot create a new process", errno);
		} else { // Parent process.
			syscalls::close(fds[0]);
			serverSocket = fds[1];
			
			int flags = fcntl(serverSocket, F_GETFD);
			if (flags != -1) {
				fcntl(serverSocket, F_SETFD, flags | FD_CLOEXEC);
			}
			
			serverPid = pid;
		}
	}

public:
	/**
	 * Create a new ApplicationPoolServer object.
	 *
	 * @param serverExecutable The filename of the ApplicationPool server
	 *            executable to use.
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
	 * @throws SystemException An error occured while trying to setup the spawn server
	 *            or the server socket.
	 * @throws IOException The specified log file could not be opened.
	 */
	ApplicationPoolServer(const string &serverExecutable,
	             const string &spawnServerCommand,
	             const string &logFile = "",
	             const string &rubyCommand = "ruby",
	             const string &user = "")
	: m_serverExecutable(serverExecutable),
	  m_spawnServerCommand(spawnServerCommand),
	  m_logFile(logFile),
	  m_rubyCommand(rubyCommand),
	  m_user(user) {
		TRACE_POINT();
		serverSocket = -1;
		serverPid = 0;
		this_thread::disable_syscall_interruption dsi;
		restartServer();
	}
	
	~ApplicationPoolServer() {
		TRACE_POINT();
		if (serverSocket != -1) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			shutdownServer();
		}
	}
	
	/**
	 * Connects to the server and returns a usable ApplicationPool object.
	 * All cache/pool data of this ApplicationPool is actually stored on
	 * the server and shared with other clients, but that is totally
	 * transparent to the user of the ApplicationPool object.
	 *
	 * @note
	 *   All methods of the returned ApplicationPool object may throw
	 *   SystemException, IOException or boost::thread_interrupted.
	 *
	 * @warning
	 * One may only use the returned ApplicationPool object for handling
	 * one session at a time. For example, don't do stuff like this:
	 * @code
	 *   ApplicationPoolPtr pool = server.connect();
	 *   Application::SessionPtr session1 = pool->get(...);
	 *   Application::SessionPtr session2 = pool->get(...);
	 * @endcode
	 * Otherwise, a deadlock can occur under certain circumstances.
	 * @warning
	 * Instead, one should call connect() multiple times:
	 * @code
	 *   ApplicationPoolPtr pool1 = server.connect();
	 *   Application::SessionPtr session1 = pool1->get(...);
	 *   
	 *   ApplicationPoolPtr pool2 = server.connect();
	 *   Application::SessionPtr session2 = pool2->get(...);
	 * @endcode
	 *
	 * @throws SystemException Something went wrong.
	 * @throws IOException Something went wrong.
	 */
	ApplicationPoolPtr connect() {
		TRACE_POINT();
		try {
			this_thread::disable_syscall_interruption dsi;
			MessageChannel channel(serverSocket);
			int clientConnection;
			
			// Write some random data to wake up the server.
			channel.writeRaw("x", 1);
			
			clientConnection = channel.readFileDescriptor(false);
			return ptr(new Client(clientConnection));
		} catch (const SystemException &e) {
			throw SystemException("Could not connect to the ApplicationPool server", e.code());
		} catch (const IOException &e) {
			string message("Could not connect to the ApplicationPool server: ");
			message.append(e.what());
			throw IOException(message);
		}
	}
	
	/**
	 * Detach the server, thereby telling it that we don't want to connect
	 * to it anymore. This frees up some resources in the current process,
	 * such as file descriptors.
	 *
	 * This method is particularily useful to Apache worker processes that
	 * have just established a connection with the ApplicationPool server.
	 * Any sessions that are opened prior to calling detach(), will keep
	 * working even after a detach().
	 *
	 * This method may only be called once. The ApplicationPoolServer object
	 * will become unusable once detach() has been called, so call connect()
	 * before calling detach().
	 */
	void detach() {
		TRACE_POINT();
		int ret;
		do {
			ret = close(serverSocket);
		} while (ret == -1 && errno == EINTR);
		serverSocket = -1;
	}
};

typedef shared_ptr<ApplicationPoolServer> ApplicationPoolServerPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_H_ */
