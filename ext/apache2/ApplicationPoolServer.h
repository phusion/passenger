/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_SERVER_H_
#define _PASSENGER_APPLICATION_POOL_SERVER_H_

#include <boost/shared_ptr.hpp>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <cstdlib>
#include <errno.h>
#include <unistd.h>

#include "MessageChannel.h"
#include "ApplicationPool.h"
#include "Application.h"
#include "Exceptions.h"
#include "Logging.h"

namespace Passenger {

using namespace std;
using namespace boost;


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
		 */
		int server;
		
		~SharedData() {
			close(server);
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
		int reader;
		int writer;
		pid_t pid;
	public:
		RemoteSession(SharedDataPtr data, pid_t pid, int id, int reader, int writer) {
			this->data = data;
			this->pid = pid;
			this->id = id;
			this->reader = reader;
			this->writer = writer;
		}
		
		virtual ~RemoteSession() {
			closeReader();
			closeWriter();
			MessageChannel(data->server).write("close", toString(id).c_str(), NULL);
		}
		
		virtual int getReader() const {
			return reader;
		}
		
		virtual void closeReader() {
			if (reader != -1) {
				close(reader);
				reader = -1;
			}
		}
		
		virtual int getWriter() const {
			return writer;
		}
		
		virtual void closeWriter() {
			if (writer != -1) {
				close(writer);
				writer = -1;
			}
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
		SharedDataPtr data;
		
	public:
		/**
		 * Create a new Client.
		 *
		 * @param sock The newly established socket connection with the ApplicationPoolServer.
		 */
		Client(int sock) {
			data = ptr(new SharedData());
			data->server = sock;
		}
		
		virtual void clear() {
			MessageChannel channel(data->server);
			channel.write("clear", NULL);
		}
		
		virtual void setMaxIdleTime(unsigned int seconds) {
			MessageChannel channel(data->server);
			channel.write("setMaxIdleTime", toString(seconds).c_str(), NULL);
		}
		
		virtual void setMax(unsigned int max) {
			MessageChannel channel(data->server);
			channel.write("setMax", toString(max).c_str(), NULL);
		}
		
		virtual unsigned int getActive() const {
			MessageChannel channel(data->server);
			vector<string> args;
			
			channel.write("getActive", NULL);
			channel.read(args);
			return atoi(args[0].c_str());
		}
		
		virtual unsigned int getCount() const {
			MessageChannel channel(data->server);
			vector<string> args;
			
			channel.write("getCount", NULL);
			channel.read(args);
			return atoi(args[0].c_str());
		}
		
		virtual pid_t getSpawnServerPid() const {
			MessageChannel channel(data->server);
			vector<string> args;
			
			channel.write("getSpawnServerPid", NULL);
			channel.read(args);
			return atoi(args[0].c_str());
		}
		
		virtual Application::SessionPtr get(const string &appRoot, bool lowerPrivilege = true,
		           const string &lowestUser = "nobody") {
			MessageChannel channel(data->server);
			vector<string> args;
			int reader, writer;
			
			channel.write("get", appRoot.c_str(),
				(lowerPrivilege) ? "true" : "false",
				lowestUser.c_str(), NULL);
			if (!channel.read(args)) {
				throw IOException("The ApplicationPool server unexpectedly closed the connection.");
			}
			if (args[0] == "ok") {
				reader = channel.readFileDescriptor();
				writer = channel.readFileDescriptor();
				return ptr(new RemoteSession(data, atoi(args[1]), atoi(args[2]), reader, writer));
			} else if (args[0] == "SpawnException") {
				if (args[2] == "true") {
					string errorPage;
					
					if (!channel.readScalar(errorPage)) {
						throw IOException("The ApplicationPool server unexpectedly closed the connection.");
					}
					throw SpawnException(args[1], errorPage);
				} else {
					throw SpawnException(args[1]);
				}
			} else if (args[0] == "IOException") {
				throw IOException(args[1]);
			} else {
				throw IOException("The ApplicationPool server returned an unknown message.");
			}
		}
	};
	
	
	string m_serverExecutable;
	string m_spawnServerCommand;
	string m_logFile;
	string m_environment;
	string m_rubyCommand;
	string m_user;
	
	pid_t serverPid;
	int serverSocket;
	
	void restartServer() {
		int fds[2];
		pid_t pid;
		
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket pair", errno);
		}
		
		pid = fork();
		if (pid == 0) { // Child process.
			dup2(fds[0], 3);
			
			// Close all unnecessary file descriptors
			for (long i = sysconf(_SC_OPEN_MAX) - 1; i > 2; i--) {
				close(i);
			}
			
			execlp(m_serverExecutable.c_str(),
				m_serverExecutable.c_str(),
				m_spawnServerCommand.c_str(),
				m_logFile.c_str(),
				m_environment.c_str(),
				m_rubyCommand.c_str(),
				m_user.c_str(),
				NULL);
			int e = errno;
			fprintf(stderr, "*** Passenger ERROR: Cannot execute %s: %s (%d)\n",
				m_serverExecutable.c_str(), strerror(e), e);
			fflush(stderr);
			_exit(1);
		} else if (pid == -1) { // Error.
			close(fds[0]);
			close(fds[1]);
			throw SystemException("Cannot create a new process", errno);
		} else { // Parent process.
			close(fds[0]);
			serverSocket = fds[1];
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
	 * @param environment The RAILS_ENV environment that all RoR applications
	 *            should use. If an empty string is specified, the current value
	 *            of the RAILS_ENV environment variable will be used.
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
	             const string &environment = "production",
	             const string &rubyCommand = "ruby",
	             const string &user = "")
	: m_serverExecutable(serverExecutable),
	  m_spawnServerCommand(spawnServerCommand),
	  m_logFile(logFile),
	  m_environment(environment),
	  m_rubyCommand(rubyCommand),
	  m_user(user) {
		serverSocket = -1;
		restartServer();
	}
	
	~ApplicationPoolServer() {
		if (serverSocket != -1) {
			close(serverSocket);
			// TODO: don't wait indefinitely
			waitpid(serverPid, NULL, 0);
		}
	}
	
	/**
	 * Connects to the server and returns a usable ApplicationPool object.
	 * All cache/pool data of this ApplicationPool is actually stored on
	 * the server and shared with other clients, but that is totally
	 * transparent to the user of the ApplicationPool object.
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
		MessageChannel channel(serverSocket);
		int clientConnection;
		
		// Write some random data to wake up the server.
		channel.writeRaw("x", 1);
		
		clientConnection = channel.readFileDescriptor();
		return ptr(new Client(clientConnection));
	}
	
	/**
	 * Detach the server by freeing up some server resources such as file descriptors.
	 * This should be called by child processes that wish to use a server, but do
	 * not run the server itself.
	 *
	 * This method may only be called once. The ApplicationPoolServer object
	 * will become unusable once detach() has been called.
	 *
	 * @warning Never call this method in the process in which this
	 *    ApplicationPoolServer was created!
	 */
	void detach() {
		close(serverSocket);
		serverSocket = -1;
	}
};

typedef shared_ptr<ApplicationPoolServer> ApplicationPoolServerPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_H_ */
