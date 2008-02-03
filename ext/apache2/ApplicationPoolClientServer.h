#ifndef _PASSENGER_APPLICATION_POOL_CLIENT_SERVER_H_
#define _PASSENGER_APPLICATION_POOL_CLIENT_SERVER_H_

#include <boost/thread/thread.hpp>
#include <set>

#include <apr_portable.h>
#include <apr_poll.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "ApplicationPool.h"
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace boost;
using namespace std;

class ApplicationPoolServer {
private:
	/*
	 * The only thing this struct does is making sure that the
	 * ApplicationPoolServer's thread function runs in the context
	 * of ApplicationPoolServer. Look at threadMain() instead for
	 * the interesting stuff.
	 */
	struct ServerThreadDelegator {
		ApplicationPoolServer *self;

		ServerThreadDelegator(ApplicationPoolServer *self) {
			this->self = self;
		}
		
		void operator()() {
			self->threadMain();
		}
	};
	
	class ApplicationPoolClient: public ApplicationPool {
	private:
		int sock;
		
	public:
		ApplicationPoolClient(int sock) {
			this->sock = sock;
		}
		
		virtual ~ApplicationPoolClient() {
			close(sock);
		}
		
		virtual ApplicationPtr get(const string &appRoot, const string &user = "", const string &group = "") {
			MessageChannel channel(sock);
			vector<string> args;
			int reader, writer;
			
			channel.write(appRoot.c_str(), user.c_str(), group.c_str(), NULL);
			channel.read(args);
			reader = channel.readFileDescriptor();
			writer = channel.readFileDescriptor();
			ApplicationPtr app(new Application(appRoot, atoi(args[0].c_str()), reader, writer));
			return app;
		}
	};
	
	class SocketDemultiplexer {
	private:
		apr_pool_t *pool;
		apr_pollset_t *pollset;
		apr_uint32_t used, capacity;
		set<int> fds;
		int *pollResult;
		
		void createAprPollFd(int fd, apr_pollfd_t *apr_fd) {
			apr_socket_t *sock = NULL;
			apr_status_t ret;
			
			ret = apr_os_sock_put(&sock, &fd, pool);
			if (ret != APR_SUCCESS) {
				throw APRException("Unable to convert a file descriptor to an APR socket", ret);
			}
			
			apr_fd->desc_type = APR_POLL_SOCKET;
			apr_fd->reqevents = APR_POLLIN;
			apr_fd->p = pool;
			apr_fd->desc.s = sock;
			apr_fd->client_data = (void *) fd;
		}
		
		void addToPollset(apr_pollset_t *pollset, int fd) {
			apr_pollfd_t apr_fd;
			apr_status_t ret;
			
			createAprPollFd(fd, &apr_fd);
			ret = apr_pollset_add(pollset, &apr_fd);
			if (ret != APR_SUCCESS) {
				throw APRException("Cannot add file descriptor to poll set", ret);
			}
		}
		
	public:
		SocketDemultiplexer() {
			apr_status_t ret;
			
			ret = apr_pool_create(&pool, NULL);
			if (ret != APR_SUCCESS) {
				throw APRException("Cannot create an APR memory pool", ret);
			}
			used = 0;
			capacity = 8;
			
			ret = apr_pollset_create(&pollset, capacity, pool, 0);
			if (ret != APR_SUCCESS) {
				apr_pool_destroy(pool);
				throw APRException("Cannot create an APR poll set", ret);
			}
			pollResult = (int *) malloc(sizeof(int) * capacity);
		}
		
		~SocketDemultiplexer() {
			apr_pool_destroy(pool);
			free(pollResult);
			// Apparently APR does not always close the file descriptor,
			// so we do it manually.
			closeAll();
		}
	
		void add(int fd) {
			if (used == capacity) {
				apr_pollset_t *newPollset;
				int *newPollResult;
				apr_status_t ret;
				
				ret = apr_pollset_create(&newPollset, capacity * 2, pool, 0);
				if (ret != APR_SUCCESS) {
					throw APRException("Cannot create a new APR poll set", ret);
				}
				newPollResult = (int *) realloc(pollResult, sizeof(int) * capacity * 2);
				if (newPollResult == NULL) {
					throw MemoryException("Cannot allocate memory for result vector.");
				}
				
				apr_pollset_destroy(pollset);
				pollset = newPollset;
				pollResult = newPollResult;
				capacity *= 2;
				
				for (set<int>::const_iterator it(fds.begin()); it != fds.end(); it++) {
					addToPollset(pollset, *it);
				}
			}
			addToPollset(pollset, fd);
			fds.insert(fd);
			used++;
		}
		
		void remove(int fd) {
			apr_pollfd_t apr_fd;
			
			createAprPollFd(fd, &apr_fd);
			if (apr_pollset_remove(pollset, &apr_fd) != APR_SUCCESS) {
				throw MemoryException("Cannot remove a file descriptor from the poll set.");
			}
			fds.erase(fds.find(fd));
			used--;
		}
		
		unsigned int poll(int **fileDescriptors, unsigned int timeout) {
			apr_int32_t num;
			const apr_pollfd_t *result;
			apr_status_t ret;
			
			do {
				ret = apr_pollset_poll(pollset, timeout * 1000, &num, &result);
			} while (ret == APR_EINTR);
			if (ret == APR_TIMEUP) {
				return 0;
			} else if (ret != APR_SUCCESS) {
				throw APRException("Cannot poll file descriptor set", ret);
			}
			for (apr_int32_t i = 0; i < num; i++) {
				pollResult[i] = (int) result[i].client_data;
			}
			*fileDescriptors = pollResult;
			return num;
		}
		
		void closeAll() {
			for (set<int>::const_iterator it(fds.begin()); it != fds.end(); it++) {
				close(*it);
			}
		}
	};
	
	StandardApplicationPool pool;
	int serverSocket;
	int connectSocket;
	bool done, detached;
	thread *thr;
	SocketDemultiplexer demultiplexer;
	
	void threadMain() {
		while (!done) {
			int *fds;
			unsigned int num;
			
			num = demultiplexer.poll(&fds, 500);
			for (unsigned int i = 0; i < num; i++) {
				if (fds[i] == serverSocket) {
					acceptNewClient(demultiplexer);
				} else {
					handleClient(fds[i]);
				}
			}
		}
	}
	
	void acceptNewClient(SocketDemultiplexer &demultiplexer) {
		int fds[2], ret;
		char x;

		// Discard data, not important. Whatever data was sent only serves
		// to wake up the server socket.
		do {
			ret = read(serverSocket, &x, 1);
		} while (ret == -1 && errno == EINTR);
		socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
		demultiplexer.add(fds[0]);
		MessageChannel(serverSocket).writeFileDescriptor(fds[1]);
		close(fds[1]);
	}
	
	void handleClient(int client) {
		MessageChannel channel(client);
		vector<string> args;
		
		if (channel.read(args) && args.size() == 3) {
			ApplicationPtr app(pool.get(args[0], args[1], args[2]));
			channel.write(toString(app->getPid()).c_str(), NULL);
			channel.writeFileDescriptor(app->getReader());
			channel.writeFileDescriptor(app->getWriter());
		}
	}
	
/*	unsigned int randomNumber() {
		FILE *f = fopen("/dev/urandom", "r");
		if (f == NULL) {
			return rand();
		} else {
			unsigned int result;
			if (fread(&result, 1, sizeof(result), f) == 0) {
				result = rand();
			}
			fclose(f);
			return result;
		}
	}
	
	bool generateUniqueSocketFilename(char *output, size_t size) {
		stringstream base;
		
		if (getenv("TMPDIR") && *getenv("TMPDIR")) {
			base << getenv("TMPDIR");
		} else {
			base << "/tmp";
		}
		base << "/phusion_passenger.tmp." << getpid() << ".";
		
		for (int i = 0; i < 10000; i++) {
			stringstream filename;
			const char *filenameString;
			struct stat buf;
			
			filename << base.str() << randomNumber() << ".sock";
			if (filename.str().size() > size - 1) {
				continue;
			}
			filenameString = filename.str().c_str();
			if (stat(filenameString, &buf) == -1 && errno == ENOENT) {
				strncpy(output, filenameString, size);
				return true;
			}
		}
		return false;
	} */
	
	void finalize() {
		demultiplexer.closeAll();
		// serverSocket will be closed by demultiplexer.
		close(connectSocket);
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
		demultiplexer.add(serverSocket);
		thr = new thread(ServerThreadDelegator(this));
	}
	
	~ApplicationPoolServer() {
		if (!detached) {
			done = true;
			thr->join();
			delete thr;
			finalize();
		}
	}
	
	ApplicationPoolPtr connect() {
		int ret;
		do {
			// Write some random data to wake up the server.
			ret = write(connectSocket, "x", 1);
		} while ((ret == -1 && errno == EAGAIN) || ret == 0);
		ret = MessageChannel(connectSocket).readFileDescriptor();
		return ptr(new ApplicationPoolClient(ret));
	}
	
	/**
	 * @warning Never call this method in the process in which this
	 *          ApplicationPoolServer was created!
	 */
	void detach() {
		detached = true;
		#ifdef VALGRIND_FRIENDLY
			delete thr;
		#endif
		finalize();
	}
};

typedef shared_ptr<ApplicationPoolServer> ApplicationPoolServerPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_CLIENT_SERVER_H_ */
