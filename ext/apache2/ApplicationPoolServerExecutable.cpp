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

#include <sys/time.h>
#include <sys/resource.h>
#include <string>
#include <vector>
#include <set>
#include <map>

#include "MessageChannel.h"
#include "StandardApplicationPool.h"
#include "Application.h"
#include "Logging.h"
#include "Exceptions.h"


using namespace boost;
using namespace std;
using namespace Passenger;

class Server;
class Client;
typedef shared_ptr<Client> ClientPtr;

#define SERVER_SOCKET_FD 3


/*****************************************
 * Server
 *****************************************/

class Server {
private:
	friend class Client;

	int serverSocket;
	StandardApplicationPool pool;
	set<ClientPtr> clients;
	mutex lock;

public:
	Server(int serverSocket,
	       const string &spawnServerCommand,
	       const string &logFile,
	       const string &rubyCommand,
	       const string &user)
		: pool(spawnServerCommand, logFile, rubyCommand, user) {
		this->serverSocket = serverSocket;
	}
	
	~Server() {
		int ret;
		do {
			ret = close(serverSocket);
		} while (ret == -1 && errno == EINTR);
		
		// Wait for all clients to disconnect.
		set<ClientPtr> clientsCopy;
		{
			/* If we clear _clients_ directly, then it may result in a deadlock.
			 * So we make a copy of the set inside a critical section in order to increase
			 * the reference counts, and then we release all references outside the critical
			 * section.
			 */
			mutex::scoped_lock l(lock);
			clientsCopy = clients;
			clients.clear();
		}
		clientsCopy.clear();
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
	static const int CLIENT_THREAD_STACK_SIZE = 1024 * 128;

	/** The Server that this Client object belongs to. */
	Server &server;
	
	/** The connection to the client. */
	int fd;
	MessageChannel channel;
	
	/** The thread which handles the client connection. */
	thread *thr;
	
	/**
	 * Maps session ID to sessions created by ApplicationPool::get(). Session IDs
	 * are sent back to the ApplicationPool client. This allows the ApplicationPool
	 * client to tell us which of the multiple sessions it wants to close, later on.
	 */
	map<int, Application::SessionPtr> sessions;
	
	/** Last used session ID. */
	int lastSessionID;
	
	void processGet(const vector<string> &args) {
		Application::SessionPtr session;
		bool failed = false;
		
		try {
			session = server.pool.get(args[1], args[2] == "true", args[3],
				args[4], args[5], args[6]);
			sessions[lastSessionID] = session;
			lastSessionID++;
		} catch (const SpawnException &e) {
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
		} catch (const IOException &e) {
			channel.write("IOException", e.what(), NULL);
			failed = true;
		}
		if (!failed) {
			try {
				channel.write("ok", toString(session->getPid()).c_str(),
					toString(lastSessionID - 1).c_str(), NULL);
				channel.writeFileDescriptor(session->getStream());
				session->closeStream();
			} catch (const exception &) {
				P_TRACE(3, "Client " << this << ": something went wrong "
					"while sending 'ok' back to the client.");
				sessions.erase(lastSessionID - 1);
				throw;
			}
		}
	}
	
	void processClose(const vector<string> &args) {
		sessions.erase(atoi(args[1]));
	}
	
	void processClear(const vector<string> &args) {
		server.pool.clear();
	}
	
	void processSetMaxIdleTime(const vector<string> &args) {
		server.pool.setMaxIdleTime(atoi(args[1]));
	}
	
	void processSetMax(const vector<string> &args) {
		server.pool.setMax(atoi(args[1]));
	}
	
	void processGetActive(const vector<string> &args) {
		channel.write(toString(server.pool.getActive()).c_str(), NULL);
	}
	
	void processGetCount(const vector<string> &args) {
		channel.write(toString(server.pool.getCount()).c_str(), NULL);
	}
	
	void processGetSpawnServerPid(const vector<string> &args) {
		channel.write(toString(server.pool.getSpawnServerPid()).c_str(), NULL);
	}
	
	void processUnknownMessage(const vector<string> &args) {
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
		try {
			vector<string> args;
			while (true) {
				if (!channel.read(args)) {
					// Client closed connection.
					break;
				}
				
				P_TRACE(3, "Client " << this << ": received message: " <<
					toString(args));
				if (args[0] == "get" && args.size() == 7) {
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
				} else if (args[0] == "getSpawnServerPid" && args.size() == 1) {
					processGetSpawnServerPid(args);
				} else {
					processUnknownMessage(args);
					break;
				}
			}
		} catch (const exception &e) {
			P_WARN("Uncaught exception in ApplicationPoolServer client thread: " <<
				e.what());
		}
		
		mutex::scoped_lock l(server.lock);
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
		thr = new thread(
			bind(&Client::threadMain, this, self),
			CLIENT_THREAD_STACK_SIZE
		);
	}
	
	~Client() {
		if (thr != NULL) {
			thr->join();
			delete thr;
		}
		close(fd);
	}
};


int
Server::start() {
	while (true) {
		int fds[2], ret;
		char x;
		
		// The received data only serves to wake up the server socket,
		// and is not important.
		do {
			ret = read(serverSocket, &x, 1);
		} while (ret == -1 && errno == EINTR);
		if (ret == 0) {
			// All web server processes disconnected from this server.
			// So we can safely quit.
			break;
		}
		
		// We have an incoming connect request from an
		// ApplicationPool client.
		do {
			ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			int e = errno;
			P_ERROR("Cannot create an anonymous Unix socket: " <<
				strerror(e) << " (" << e << ") --- aborting!");
			abort();
			
			// Shut up compiler warning.
			printf("%d", e);
		}
		
		try {
			MessageChannel(serverSocket).writeFileDescriptor(fds[1]);
			do {
				ret = close(fds[1]);
			} while (ret == -1 && errno == EINTR);
		} catch (SystemException &e) {
			P_ERROR("Cannot send a file descriptor: " << e.sys() <<
				" --- aborting!");
			abort();
		} catch (const exception &e) {
			P_ERROR("Cannot send a file descriptor: " << e.what() <<
				" --- aborting!");
			abort();
		}
		
		ClientPtr client(new Client(*this, fds[0]));
		pair<set<ClientPtr>::iterator, bool> p;
		{
			mutex::scoped_lock l(lock);
			clients.insert(client);
		}
		client->start(client);
	}
	return 0;
}

int
main(int argc, char *argv[]) {
	try {
		Server server(SERVER_SOCKET_FD, argv[1], argv[2], argv[3], argv[4]);
		return server.start();
	} catch (const exception &e) {
		fprintf(stderr, "*** An unexpected error occured in the Passenger "
			"ApplicationPool server:\n%s\n",
			e.what());
		fflush(stderr);
		abort();
		return 1;
	}
}

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_EXECUTABLE_H_ */
