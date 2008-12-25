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
#include <string>
#include <sstream>
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
using namespace oxt;
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
	boost::mutex lock;
	string statusReportFIFO;
	shared_ptr<oxt::thread> statusReportThread;
	
	void statusReportThreadMain() {
		TRACE_POINT();
		try {
			while (!this_thread::interruption_requested()) {
				struct stat buf;
				int ret;
				
				UPDATE_TRACE_POINT();
				do {
					ret = stat(statusReportFIFO.c_str(), &buf);
				} while (ret == -1 && errno == EINTR);
				if (ret == -1 || !S_ISFIFO(buf.st_mode)) {
					// Something bad happened with the status report
					// FIFO, so we bail out.
					break;
				}
				
				UPDATE_TRACE_POINT();
				FILE *f = syscalls::fopen(statusReportFIFO.c_str(), "w");
				if (f == NULL) {
					break;
				}
				
				UPDATE_TRACE_POINT();
				MessageChannel channel(fileno(f));
				string report;
				report.append("----------- Backtraces -----------\n");
				report.append(oxt::thread::all_backtraces());
				report.append("\n\n");
				report.append(pool.toString());
				
				UPDATE_TRACE_POINT();
				try {
					channel.writeScalar(report);
					channel.writeScalar(pool.toXml());
				} catch (...) {
					// Ignore write errors.
				}
				syscalls::fclose(f);
			}
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "Status report thread interrupted.");
		}
	}
	
	void deleteStatusReportFIFO() {
		TRACE_POINT();
		if (!statusReportFIFO.empty()) {
			int ret;
			do {
				ret = unlink(statusReportFIFO.c_str());
			} while (ret == -1 && errno == EINTR);
		}
	}

public:
	Server(int serverSocket,
	       const unsigned int logLevel,
	       const string &spawnServerCommand,
	       const string &logFile,
	       const string &rubyCommand,
	       const string &user,
	       const string &statusReportFIFO)
		: pool(spawnServerCommand, logFile, rubyCommand, user) {
		
		Passenger::setLogLevel(logLevel);
		this->serverSocket = serverSocket;
		this->statusReportFIFO = statusReportFIFO;
	}
	
	~Server() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		
		P_TRACE(2, "Shutting down server.");
		
		syscalls::close(serverSocket);
		
		if (statusReportThread != NULL) {
			UPDATE_TRACE_POINT();
			statusReportThread->interrupt_and_join();
		}
		
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
		deleteStatusReportFIFO();
		
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
	static const int CLIENT_THREAD_STACK_SIZE = 1024 * 128;

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
	
	void processGet(const vector<string> &args) {
		TRACE_POINT();
		Application::SessionPtr session;
		bool failed = false;
		
		try {
			PoolOptions options(args, 1);
			session = server.pool.get(options);
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
				channel.writeFileDescriptor(session->getStream());
				session->closeStream();
			} catch (const exception &) {
				UPDATE_TRACE_POINT();
				P_TRACE(3, "Client " << this << ": something went wrong "
					"while sending 'ok' back to the client.");
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
		server.pool.clear();
	}
	
	void processSetMaxIdleTime(const vector<string> &args) {
		TRACE_POINT();
		server.pool.setMaxIdleTime(atoi(args[1]));
	}
	
	void processSetMax(const vector<string> &args) {
		TRACE_POINT();
		server.pool.setMax(atoi(args[1]));
	}
	
	void processGetActive(const vector<string> &args) {
		TRACE_POINT();
		channel.write(toString(server.pool.getActive()).c_str(), NULL);
	}
	
	void processGetCount(const vector<string> &args) {
		TRACE_POINT();
		channel.write(toString(server.pool.getCount()).c_str(), NULL);
	}
	
	void processSetMaxPerApp(unsigned int maxPerApp) {
		TRACE_POINT();
		server.pool.setMaxPerApp(maxPerApp);
	}
	
	void processGetSpawnServerPid(const vector<string> &args) {
		TRACE_POINT();
		channel.write(toString(server.pool.getSpawnServerPid()).c_str(), NULL);
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
	
	// Ignore SIGPIPE.
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
	
	try {
		if (!statusReportFIFO.empty()) {
			statusReportThread = ptr(
				new oxt::thread(
					bind(&Server::statusReportThreadMain, this),
					"Status report thread",
					1024 * 128
				)
			);
		}
		
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
			MessageChannel(serverSocket).writeFileDescriptor(fds[1]);
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
		Server server(SERVER_SOCKET_FD, atoi(argv[1]),
			argv[2], argv[3], argv[4], argv[5], argv[6]);
		return server.start();
	} catch (const tracable_exception &e) {
		P_ERROR(e.what() << "\n" << e.backtrace());
		return 1;
	} catch (const exception &e) {
		P_ERROR(e.what());
		return 1;
	} catch (...) {
		P_ERROR("Unknown exception thrown in main thread.");
		throw;
	}
}

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_EXECUTABLE_H_ */
