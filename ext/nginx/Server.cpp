// g++ -Wall -I.. -I../apache2 Server.cpp -o server ../libboost_oxt.a ../apache2/Utils.o ../apache2/Logging.o -lpthread
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>
#include <errno.h>

#include <set>

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include "oxt/thread.hpp"
#include "oxt/system_calls.hpp"

#include "ScgiRequestParser.h"
#include "HttpStatusExtractor.h"

#include "StandardApplicationPool.h"
#include "Application.h"
#include "PoolOptions.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace boost;
using namespace oxt;
using namespace Passenger;

/**
 * Wrapper class around a file descriptor integer, for RAII behavior.
 *
 * A FileDescriptor object behaves just like an int, so that you can pass it to
 * system calls such as read(). It performs reference counting. When the last
 * copy of a FileDescriptor has been destroyed, the underlying file descriptor
 * will be automatically closed.
 */
class FileDescriptor {
private:
	struct SharedData {
		int fd;
		
		SharedData(int fd) {
			this->fd = fd;
		}
		
		~SharedData() {
			if (syscalls::close(fd) == -1) {
				throw SystemException("Cannot close file descriptor", errno);
			}
		}
	};
	
	shared_ptr<SharedData> data;
	
public:
	FileDescriptor(int fd) {
		data = ptr(new SharedData(fd));
	}
	
	operator int () const {
		return data->fd;
	}
};

class Client {
private:
	static const int CLIENT_THREAD_STACK_SIZE = 1024 * 128;
	
	StandardApplicationPoolPtr pool;
	int serverSocket;
	oxt::thread *thr;
	
	FileDescriptor acceptConnection() {
		struct sockaddr_un addr;
		socklen_t addrlen = sizeof(addr);
		int fd = syscalls::accept(serverSocket,
			(struct sockaddr *) &addr,
			&addrlen);
		if (fd == -1) {
			throw SystemException("Cannot accept new connection", errno);
		} else {
			return FileDescriptor(fd);
		}
	}
	
	bool readAndParseResponseHeaders(FileDescriptor &fd, ScgiRequestParser &parser) {
		char buf[1024 * 16];
		ssize_t size;
		unsigned int accepted;
		
		do {
			size = syscalls::read(fd, buf, sizeof(buf));
			accepted = parser.feed(buf, size);
		} while (parser.acceptingInput());
		
		return parser.hasHeader("DOCUMENT_ROOT");
	}
	
	void forwardResponse(Application::SessionPtr &session, FileDescriptor &clientFd) {
		HttpStatusExtractor ex;
		int stream = session->getStream();
		int eof = false;
		MessageChannel output(clientFd);
		char buf[1024 * 32];
		ssize_t size;
		
		/* Read data from the backend process until we're able to
		 * extract the HTTP status line from it.
		 */
		while (!eof) {
			size = syscalls::read(stream, buf, sizeof(buf));
			if (size <= 0) {
				eof = true;
			} else if (ex.feed(buf, size)) {
				/* We now have an HTTP status line. Send back
				 * a proper HTTP response, then exit this while
				 * loop and continue with forwarding the rest
				 * of the response data.
				 */
				string statusLine("HTTP/1.1 ");
				statusLine.append(ex.getStatusLine());
				output.writeRaw(statusLine.c_str(), statusLine.size());
				output.writeRaw(ex.getBuffer().c_str(), ex.getBuffer().size());
				break;
			}
		}
		while (!eof) {
			size = syscalls::read(stream, buf, sizeof(buf));
			if (size <= 0) {
				eof = true;
			} else {
				output.writeRaw(buf, size);
			}
		}
	}
	
	void handleRequest(FileDescriptor &clientFd) {
		ScgiRequestParser parser;
		if (!readAndParseResponseHeaders(clientFd, parser)) {
			return;
		}
		
		PoolOptions options(canonicalizePath(parser.getHeader("DOCUMENT_ROOT") + "/.."));
		Application::SessionPtr session(pool->get(options));
		
		session->sendHeaders(parser.getHeaderData().c_str(),
			parser.getHeaderData().size());
		session->shutdownWriter();
		forwardResponse(session, clientFd);
	}
	
	void threadMain() {
		while (true) {
			try {
				FileDescriptor fd = acceptConnection();
				handleRequest(fd);
			} catch (const boost::thread_interrupted &) {
				P_TRACE(2, "Client thread " << this << " interrupted.");
				break;
			} catch (const tracable_exception &e) {
				P_TRACE(2, "Uncaught exception in PassengerServer client thread:\n"
					<< "   message: " << toString(args) << "\n"
					<< "   exception: " << e.what() << "\n"
					<< "   backtrace:\n" << e.backtrace());
				abort();
			} catch (const exception &e) {
				P_TRACE(2, "Uncaught exception in PassengerServer client thread:\n"
					<< "   message: " << toString(args) << "\n"
					<< "   exception: " << e.what() << "\n"
					<< "   backtrace: not available");
				abort();
			} catch (...) {
				P_TRACE(2, "Uncaught unknown exception in PassengerServer client thread.");
				throw;
			}
		}
	}
	
public:
	Client(StandardApplicationPoolPtr &pool, int serverSocket) {
		this->pool = pool;
		this->serverSocket = serverSocket;
		thr = new oxt::thread(
			bind(&Client::threadMain, this),
			"Thread", CLIENT_THREAD_STACK_SIZE
		);
	}
};

typedef shared_ptr<Client> ClientPtr;

class Server {
private:
	static const unsigned int BACKLOG_SIZE = 50;

	int serverSocket;
	unsigned int numberOfThreads;
	set<ClientPtr> clients;
	StandardApplicationPoolPtr pool;
	
	void initializePool(unsigned int maxPoolSize) {
		pool = ptr(new StandardApplicationPool(
			"/home/hongli/Projects/mod_rails/bin/passenger-spawn-server",
			"",
			"/opt/r8ee/bin/ruby"
		));
		pool->setMax(maxPoolSize);
	}
	
	void startListening() {
		this_thread::disable_syscall_interruption dsi;
		const char socketName[] = "/tmp/passenger_scgi.sock";
		struct sockaddr_un addr;
		int ret;
		
		serverSocket = syscalls::socket(PF_UNIX, SOCK_STREAM, 0);
		if (serverSocket == -1) {
			throw SystemException("Cannot create an unconnected Unix socket", errno);
		}
		
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, socketName, sizeof(addr.sun_path));
		addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
		syscalls::unlink(socketName);
		
		ret = syscalls::bind(serverSocket, (const struct sockaddr *) &addr, sizeof(addr));
		if (ret == -1) {
			int e = errno;
			syscalls::close(serverSocket);
			
			string message("Cannot bind on Unix socket '");
			message.append(socketName);
			message.append("'");
			throw SystemException(message, e);
		}
		
		ret = syscalls::listen(serverSocket, BACKLOG_SIZE);
		if (ret == -1) {
			int e = errno;
			syscalls::close(serverSocket);
			
			string message("Cannot bind on Unix socket '");
			message.append(socketName);
			message.append("'");
			throw SystemException(message, e);
		}
	}
	
	void startClientHandlerThreads() {
		for (unsigned int i = 0; i < numberOfThreads; i++) {
			ClientPtr client(new Client(pool, serverSocket));
			clients.insert(client);
		}
	}

public:
	Server(unsigned int maxPoolSize) {
		numberOfThreads = maxPoolSize * 4;
		setup_syscall_interruption_support();
		initializePool(maxPoolSize);
		startListening();
	}
	
	~Server() {
		syscalls::close(serverSocket);
	}
	
	void start() {
		startClientHandlerThreads();
		while (true) {
			sleep(1);
		}
	}
};

int
main() {
	try {
		Server(6).start();
		return 0;
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

