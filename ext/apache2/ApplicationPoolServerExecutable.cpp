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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
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

static bool serverDone = false;


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
	string statusReportFIFO;
	
	void statusReportThread() {
		while (true) {
			struct stat buf;
			int ret;
			
			do {
				ret = stat(statusReportFIFO.c_str(), &buf);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1 || !S_ISFIFO(buf.st_mode)) {
				// Something bad happened with the status
				// report FIFO, so we bail out.
				return;
			}
			
			FILE *f;
			
			do {
				f = fopen(statusReportFIFO.c_str(), "w");
			} while (f == NULL && errno == EINTR);
			if (f == NULL) {
				return;
			}
			
			string report(pool.toString());
			fwrite(report.c_str(), 1, report.size(), f);
			fclose(f);
			
			// Prevent sending too much data at once.
			sleep(1);
		}
	}

public:
	Server(int serverSocket,
	       const string &spawnServerCommand,
	       const string &logFile,
	       const string &rubyCommand,
	       const string &user)
		: pool(spawnServerCommand, logFile, rubyCommand, user) {
		
		this->serverSocket = serverSocket;
		
		char filename[PATH_MAX];
		snprintf(filename, sizeof(filename), "/tmp/passenger_status.%d.fifo",
			getpid());
		filename[PATH_MAX - 1] = '\0';
		if (mkfifo(filename, S_IRUSR | S_IWUSR) == -1 && errno != EEXIST) {
			fprintf(stderr, "*** WARNING: Could not create FIFO '%s'; "
				"disabling Passenger ApplicationPool status reporting.\n",
				filename);
			fflush(stderr);
		} else {
			statusReportFIFO = filename;
		}
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
		
		do {
			ret = unlink(statusReportFIFO.c_str());
		} while (ret == -1 && errno == EINTR);
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
			session = server.pool.get(args[1], args[2] == "true", args[3], args[4], args[5]);
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
		vector<string> args;
		while (true) {
			try {
				if (!channel.read(args)) {
					// Client closed connection.
					break;
				}
			} catch (const SystemException &e) {
				P_WARN("Exception in ApplicationPoolServer client thread during "
					"reading of a message: " << e.what());
				break;
			}
			
			P_TRACE(4, "Client " << this << ": received message: " <<
				toString(args));
			try {
				if (args[0] == "get" && args.size() == 6) {
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
			} catch (const exception &e) {
				P_WARN("Uncaught exception in ApplicationPoolServer client thread:\n"
					<< "   message: " << toString(args) << "\n"
					<< "   exception: " << e.what());
				break;
			}
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


static void
gracefulShutdown(int sig) {
	serverDone = true;
}

int
Server::start() {
	if (!statusReportFIFO.empty()) {
		new thread(
			bind(&Server::statusReportThread, this),
			1024 * 128
		);
	}
	
	serverDone = false;
	signal(SIGINT, gracefulShutdown);
	siginterrupt(SIGINT, 1);
	
	while (!serverDone) {
		int fds[2], ret;
		char x;
		
		// The received data only serves to wake up the server socket,
		// and is not important.
		if (!serverDone) {
			do {
				ret = read(serverSocket, &x, 1);
			} while (ret == -1 && errno == EINTR && !serverDone);
		}
		if (ret == 0 || serverDone) {
			// All web server processes disconnected from this server.
			// So we can safely quit.
			break;
		}
		
		// We have an incoming connect request from an
		// ApplicationPool client.
		if (!serverDone) {
			do {
				ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
			} while (ret == -1 && errno == EINTR && !serverDone);
		}
		if (ret == -1 || serverDone) {
			throw SystemException("Cannot create an anonymous Unix socket", errno);
		}
		
		MessageChannel(serverSocket).writeFileDescriptor(fds[1]);
		do {
			ret = close(fds[1]);
		} while (ret == -1 && errno == EINTR);
		
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
		return 1;
	}
}

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_EXECUTABLE_H_ */
