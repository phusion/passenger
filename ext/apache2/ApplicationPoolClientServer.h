#ifndef _PASSENGER_APPLICATION_POOL_CLIENT_SERVER_H_
#define _PASSENGER_APPLICATION_POOL_CLIENT_SERVER_H_

#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

#include <set>

#include <sys/types.h>
#include <sys/socket.h>
#include <cstdlib>
#include <errno.h>
#include <unistd.h>

#include "ApplicationPool.h"
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

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
 *       // The child process doesn't run a server (only the parent process does)
 *       // so we call detach to free the server resources (things like file
 *       // descriptors).
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
 * @warning
 *   ApplicationPoolServer uses threads internally. Threads will disappear after a fork(),
 *   so ApplicationPoolServer will become usable as a server after a fork(). After a fork(),
 *   you can still call connect() (and, of course, detach()), but the same
 *   ApplicationPoolServer better still be running in the parent process. So in case of
 *   Apache with the prefork MPM, be sure to create an ApplicationPoolServer
 *   <em>after</em> Apache has daemonized.
 *
 * <h2>Implementation notes</h2>
 * Notice that ApplicationPoolServer does do not use TCP sockets at all, or even named Unix
 * sockets, depite being a server that can handle multiple clients! So ApplicationPoolServer
 * will expose no open ports or temporary Unix socket files. Only child processes are able
 * to use the ApplicationPoolServer.
 *
 * This is implemented through anonymous Unix sockets (<tt>socketpair()</tt>) and file descriptor
 * passing. It allows one to emulate <tt>accept()</tt>. During initialization,
 * ApplicationPoolServer creates a pair of Unix sockets, one called <tt>serverSocket</tt>
 * and the other called <tt>connectSocket</tt>. There is a thread which continuously
 * listens on serverSocket for incoming data. The data itself is not important, because it
 * only serves to wake up the thread. ApplicationPoolServer::connect() sends some data through
 * connectSocket, which wakes up the server thread. The server thread will then create
 * a pair of Unix sockets. One of them is passed through serverSocket. The other will be
 * handled by a newly created client thread. So the socket that was passed through serverSocket
 * is the client's connection to the server, while the other socket is the server's connection
 * to the client.
 *
 * Note that serverSocket and connectSocket are solely used for setting up new connections
 * ala accept(). They are not used for any actual data. In fact, they cannot be used in any
 * other way without some sort of inter-process synchronization mechanism, because all
 * child processes are connected to the same serverSocket. In contrast,
 * ApplicationPoolServer::connect() allows one to setup a private communicate channel between
 * the server and the current child process.
 *
 * Also note that each client is handled by a seperate thread. This is necessary because
 * ApplicationPoolServer internally uses StandardApplicationPool, and the current algorithm
 * for StandardApplicationPool::get() can block (in the case that the spawning limit has
 * been exceeded). While it is possible to get around this problem without using threads,
 * a thread-based implementation is easier to write.
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
		 * The socket connection to the server, as was established by
		 * ApplicationPoolServer::connect().
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
		
		virtual int getReader() {
			return reader;
		}
		
		virtual void closeReader() {
			if (reader != -1) {
				close(reader);
				reader = -1;
			}
		}
		
		virtual int getWriter() {
			return writer;
		}
		
		virtual void closeWriter() {
			if (writer != -1) {
				close(writer);
				writer = -1;
			}
		}
		
		virtual pid_t getPid() {
			return pid;
		}
	};

	/**
	 * An ApplicationPool implementation that works together with ApplicationPoolServer.
	 * It doesn't do much by itself, its job is mostly to forward queries/commands to
	 * the server and returning the result. Most of the logic is in the server.
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
		
		virtual Application::SessionPtr get(const string &appRoot, const string &user = "", const string &group = "") {
			MessageChannel channel(data->server);
			vector<string> args;
			int reader, writer;
			
			channel.write("get", appRoot.c_str(), user.c_str(), group.c_str(), NULL);
			if (!channel.read(args)) {
				throw IOException("The ApplicationPool server unexpectedly closed the connection.");
			}
			if (args[0] == "ok") {
				reader = channel.readFileDescriptor();
				writer = channel.readFileDescriptor();
				return ptr(new RemoteSession(data, atoi(args[1]), atoi(args[2]), reader, writer));
			} else if (args[0] == "SpawnException") {
				throw SpawnException(args[1]);
			} else if (args[0] == "IOException") {
				throw IOException(args[1]);
			} else {
				throw IOException("The ApplicationPool server returned an unknown message.");
			}
		}
	};
	
	/**
	 * Contains information about exactly one client.
	 */
	struct ClientInfo {
		/** The connection to the client. */
		int fd;
		/** The thread which handles the client. */
		thread *thr;
		bool detached;
		
		ClientInfo() {
			detached = false;
		}
		
		void detach() {
			detached = true;
		}
		
		~ClientInfo() {
			close(fd);
			// For some reason, joining or deleting (detaching)
			// the thread after fork() will cause a segfault.
			// I haven't figured out why that happens, so for now
			// I'll just ignore the thread (which isn't running
			// anyway).
			if (!detached) {
				delete thr;
			}
		}
	};
	
	typedef shared_ptr<ClientInfo> ClientInfoPtr;
	
	StandardApplicationPool pool;
	int serverSocket;
	int connectSocket;
	bool done, detached;
	
	mutex lock;
	thread *serverThread;
	set<ClientInfoPtr> clients;
	
	// TODO: check for exceptions in threads, possibly forwarding them
	
	/**
	 * The entry point of the server thread which sets up private connections.
	 * See the class overview's implementation notes for details.
	 */
	void serverThreadMainLoop() {
		while (!done) {
			int fds[2], ret;
			char x;
			
			// The received data only serves to wake up the server socket,
			// and is not important.
			do {
				ret = read(serverSocket, &x, 1);
			} while (ret == -1 && errno == EINTR);
			if (ret == 0) {
				break;
			}
			
			// Incoming connect request.
			do {
				ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				int e = errno;
				P_ERROR("Cannot create an anonymous Unix socket: " <<
					strerror(e) << " (" << e << ")");
				abort();
				
				// Shut up compiler warning.
				bool x = false;
				if (x) {
					printf("%d", e);
				}
			}
			
			try {
				MessageChannel(serverSocket).writeFileDescriptor(fds[1]);
				do {
					ret = close(fds[1]);
				} while (ret == -1 && errno == EINTR);
			} catch (const exception &e) {
				P_ERROR("Cannot send a file descriptor: " << e.what());
				abort();
			}
			
			ClientInfoPtr info(new ClientInfo());
			info->fd = fds[0];
			info->thr = new thread(bind(&ApplicationPoolServer::clientThreadMainLoop, this, info));
			mutex::scoped_lock l(lock);
			clients.insert(info);
		}
	}
	
	/**
	 * The entry point of a thread which handles exactly one client.
	 */
	void clientThreadMainLoop(ClientInfoPtr client) {
		MessageChannel channel(client->fd);
		vector<string> args;
		map<int, Application::SessionPtr> sessions;
		int lastID = 0;

		try {
			while (!done) {
				if (!channel.read(args)) {
					break;
				}
			
				if (args[0] == "get" && args.size() == 4) {
					Application::SessionPtr session;
					bool failed = false;
					try {
						session = pool.get(args[1], args[2], args[3]);
					} catch (const SpawnException &e) {
						channel.write("SpawnException", e.what(), NULL);
						failed = true;
					} catch (const IOException &e) {
						channel.write("IOException", e.what(), NULL);
						failed = true;
					}
					if (!failed) {
						channel.write("ok", toString(session->getPid()).c_str(),
							toString(lastID).c_str(), NULL);
						channel.writeFileDescriptor(session->getReader());
						channel.writeFileDescriptor(session->getWriter());
						session->closeReader();
						session->closeWriter();
						sessions[lastID] = session;
						lastID++;
					}
				
				} else if (args[0] == "close" && args.size() == 2) {
					sessions.erase(atoi(args[1]));
				
				} else if (args[0] == "setMax" && args.size() == 2) {
					pool.setMax(atoi(args[1]));
				
				} else if (args[0] == "getActive" && args.size() == 1) {
					channel.write(toString(pool.getActive()).c_str(), NULL);
				
				} else if (args[0] == "getCount" && args.size() == 1) {
					channel.write(toString(pool.getCount()).c_str(), NULL);
				
				} else {
					string name;
					if (args.empty()) {
						name = "(null)";
					} else {
						name = args[0];
					}
					P_WARN("An ApplicationPoolServer client sent an invalid command: "
						<< name << " (" << args.size() << " elements)");
					done = true;
				}
			}
		} catch (const exception &e) {
			P_WARN("Uncaught exception in ApplicationPoolServer client thread: " <<
				e.what());
		}
		
		mutex::scoped_lock l(lock);
		clients.erase(client);
	}
	
public:
	/**
	 * Create a new ApplicationPoolServer object.
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
	 * @throws SystemException An error occured while trying to setup the spawn server
	 *            or the server socket.
	 * @throws IOException The specified log file could not be opened.
	 */
	ApplicationPoolServer(const string &spawnServerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby")
	: pool(spawnServerCommand, logFile, environment, rubyCommand) {
		int fds[2];
		
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket pair", errno);
		}
		serverSocket = fds[0];
		connectSocket = fds[1];
		done = false;
		detached = false;
		serverThread = new thread(bind(&ApplicationPoolServer::serverThreadMainLoop, this));
	}
	
	~ApplicationPoolServer() {
		if (!detached) {
			done = true;
			close(connectSocket);
			serverThread->join();
			delete serverThread;
			close(serverSocket);
			
			set<ClientInfoPtr> clientsCopy;
			{
				mutex::scoped_lock l(lock);
				clientsCopy = clients;
			}
			set<ClientInfoPtr>::iterator it;
			for (it = clientsCopy.begin(); it != clientsCopy.end(); it++) {
				(*it)->thr->join();
			}
		}
	}
	
	/**
	 * Connects to the server and returns a usable ApplicationPool object.
	 * All cache/pool data of this ApplicationPool is actually stored on the server
	 * and shared with other clients, but that is totally transparent.
	 *
	 * @throws SystemException Something went wrong.
	 * @throws IOException Something went wrong.
	 */
	ApplicationPoolPtr connect() {
		MessageChannel channel(connectSocket);
		int fd;
		
		// Write some random data to wake up the server.
		channel.writeRaw("x", 1);
		
		fd = channel.readFileDescriptor();
		return ptr(new Client(fd));
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
		detached = true;
		close(connectSocket);
		close(serverSocket);
		serverThread->join();
		delete serverThread;
		
		// A client thread might have a reference to a ClientInfo
		// object. And because that thread doesn't run anymore after a
		// fork(), the reference never gets removed and the ClientInfo
		// object never gets destroyed. So we forcefully delete
		// ClientInfo objects in order to close the client file
		// descriptors.
		set<ClientInfoPtr>::iterator it;
		for (it = clients.begin(); it != clients.end(); it++) {
			if (!it->unique()) {
				(*it)->detach();
				delete it->get();
			}
		}
		clients.clear();
		
		pool.detach();
	}
};

typedef shared_ptr<ApplicationPoolServer> ApplicationPoolServerPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_CLIENT_SERVER_H_ */
