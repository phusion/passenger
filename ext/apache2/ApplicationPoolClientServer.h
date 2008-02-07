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

class ApplicationPoolServer {
private:
	struct SharedData {
		int server;
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;

	class RemoteSession: public Application::Session {
	private:
		SharedDataPtr data;
		int id;
		int reader;
		int writer;
	public:
		RemoteSession(SharedDataPtr data, int id, int reader, int writer) {
			this->data = data;
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
	};

	class Client: public ApplicationPool {
	private:
		SharedDataPtr data;
		
	public:
		Client(int sock) {
			data = ptr(new SharedData());
			data->server = sock;
		}
		
		virtual ~Client() {
			if (data->server != -1) {
				close(data->server);
				data->server = -1;
			}
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
			channel.read(args);
			reader = channel.readFileDescriptor();
			writer = channel.readFileDescriptor();
			return ptr(new RemoteSession(data, atoi(args[0].c_str()), reader, writer));
		}
	};
	
	struct ClientInfo {
		int fd;
		thread *thr;
		
		~ClientInfo() {
			close(fd);
			delete thr;
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
	// Don't forget to test them
	
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
			
			socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
			MessageChannel(serverSocket).writeFileDescriptor(fds[1]);
			close(fds[1]);
			
			ClientInfoPtr info(new ClientInfo());
			info->fd = fds[0];
			info->thr = new thread(bind(&ApplicationPoolServer::clientThreadMainLoop, this, info));
			mutex::scoped_lock l(lock);
			clients.insert(info);
		}
	}
	
	void clientThreadMainLoop(ClientInfoPtr client) {
		MessageChannel channel(client->fd);
		vector<string> args;
		map<int, Application::SessionPtr> sessions;
		int lastID = 0;

		while (!done) {
			if (!channel.read(args)) {
				break;
			}
			
			if (args[0] == "get" && args.size() == 4) {
				Application::SessionPtr session(pool.get(args[1], args[2], args[3]));
				channel.write(toString(lastID).c_str(), NULL);
				channel.writeFileDescriptor(session->getReader());
				channel.writeFileDescriptor(session->getWriter());
				session->closeReader();
				session->closeWriter();
				sessions[lastID] = session;
				lastID++;
			} else if (args[0] == "close" && args.size() == 2) {
				sessions.erase(atoi(args[1].c_str()));
			} else if (args[0] == "setMax") {
				pool.setMax(atoi(args[1].c_str()));
			} else if (args[0] == "getActive") {
				channel.write(toString(pool.getActive()).c_str(), NULL);
			} else if (args[0] == "getCount") {
				channel.write(toString(pool.getCount()).c_str(), NULL);
			}
		}
		
		mutex::scoped_lock l(lock);
		clients.erase(client);
	}
	
public:
	ApplicationPoolServer(const string &spawnManagerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby")
	: pool(spawnManagerCommand, logFile, environment, rubyCommand) {
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
	
	ApplicationPoolPtr connect() {
		int ret;
		do {
			// Write some random data to wake up the server.
			ret = write(connectSocket, "x", 1);
		} while ((ret == -1 && errno == EAGAIN) || ret == 0);
		ret = MessageChannel(connectSocket).readFileDescriptor();
		return ptr(new Client(ret));
	}
	
	/**
	 * @warning Never call this method in the process in which this
	 *          ApplicationPoolServer was created!
	 */
	void detach() {
		detached = true;
		close(connectSocket);
		close(serverSocket);
		#ifdef VALGRIND_FRIENDLY
			delete serverThread;
		#endif
		clients.clear();
	}
};

typedef shared_ptr<ApplicationPoolServer> ApplicationPoolServerPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_CLIENT_SERVER_H_ */
