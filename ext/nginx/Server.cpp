// g++ -DPASSENGER_DEBUG -Wall -I.. -I../apache2 Server.cpp -o server ../libboost_oxt.a ../apache2/Utils.o ../apache2/Logging.o -lpthread -g
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <set>
#include <vector>
#include <string>

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

#define HELPER_SERVER_PASSWORD_SIZE     64


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
	FileDescriptor() {
		// Do nothing.
	}
	
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
	string password;
	int serverSocket;
	oxt::thread *thr;
	
	FileDescriptor acceptConnection() {
		TRACE_POINT();
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
	
	bool readAndParseRequestHeaders(FileDescriptor &fd, ScgiRequestParser &parser, string &requestBody) {
		TRACE_POINT();
		char buf[1024 * 16];
		ssize_t size;
		unsigned int accepted;
		
		do {
			size = syscalls::read(fd, buf, sizeof(buf));
			if (size == -1) {
				throw SystemException("Cannot read request header", errno);
			} else {
				accepted = parser.feed(buf, size);
			}
		} while (parser.acceptingInput());

		if (parser.getState() == ScgiRequestParser::ERROR) {
			P_ERROR("Invalid SCGI header received.");
			return false;
		} else if (!parser.hasHeader("DOCUMENT_ROOT")) {
			P_ERROR("DOCUMENT_ROOT header is missing.");
			return false;
		} else {
			requestBody.assign(buf + accepted, size - accepted);
			return true;
		}
	}
	
	void sendRequestBody(Application::SessionPtr &session,
	                     FileDescriptor &clientFd,
	                     const string &partialRequestBody,
	                     unsigned long contentLength) {
		TRACE_POINT();
		char buf[1024 * 16];
		ssize_t size;
		size_t bytesToRead;
		unsigned long bytesForwarded = 0;
		
		if (partialRequestBody.size() > 0) {
			UPDATE_TRACE_POINT();
			session->sendBodyBlock(partialRequestBody.c_str(),
				partialRequestBody.size());
			bytesForwarded = partialRequestBody.size();
		}
		
		bool done = bytesForwarded == contentLength;
		while (!done) {
			UPDATE_TRACE_POINT();
			
			bytesToRead = contentLength - bytesForwarded;
			if (bytesToRead > sizeof(buf)) {
				bytesToRead = sizeof(buf);
			}
			size = syscalls::read(clientFd, buf, bytesToRead);
			
			if (size == 0) {
				done = true;
			} else if (size == -1) {
				throw SystemException("Cannot read request body", errno);
			} else {
				UPDATE_TRACE_POINT();
				session->sendBodyBlock(buf, size);
				bytesForwarded += size;
				done = bytesForwarded == contentLength;
			}
		}
	}
	
	void forwardResponse(Application::SessionPtr &session, FileDescriptor &clientFd) {
		TRACE_POINT();
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
			UPDATE_TRACE_POINT();
			size = syscalls::read(stream, buf, sizeof(buf));
			if (size == 0) {
				eof = true;
			} else if (size == -1) {
				throw SystemException("Cannot read response from backend process", errno);
			} else if (ex.feed(buf, size)) {
				/* We now have an HTTP status line. Send back
				 * a proper HTTP response, then exit this while
				 * loop and continue with forwarding the rest
				 * of the response data.
				 */
				UPDATE_TRACE_POINT();
				string statusLine("HTTP/1.1 ");
				statusLine.append(ex.getStatusLine());
				UPDATE_TRACE_POINT();
				output.writeRaw(statusLine.c_str(), statusLine.size());
				UPDATE_TRACE_POINT();
				output.writeRaw(ex.getBuffer().c_str(), ex.getBuffer().size());
				break;
			}
		}
		
		UPDATE_TRACE_POINT();
		while (!eof) {
			UPDATE_TRACE_POINT();
			size = syscalls::read(stream, buf, sizeof(buf));
			if (size == 0) {
				eof = true;
			} else if (size == -1) {
				throw SystemException("Cannot read response from backend process", errno);
			} else {
				UPDATE_TRACE_POINT();
				output.writeRaw(buf, size);
			}
		}
	}
	
	void handleRequest(FileDescriptor &clientFd) {
		TRACE_POINT();
		ScgiRequestParser parser;
		string partialRequestBody;
		unsigned long contentLength;
		
		if (!readAndParseRequestHeaders(clientFd, parser, partialRequestBody)) {
			return;
		}
		
		// TODO: check password
		
		try {
			PoolOptions options(canonicalizePath(parser.getHeader("DOCUMENT_ROOT") + "/.."));
			Application::SessionPtr session(pool->get(options));
			
			UPDATE_TRACE_POINT();
			session->sendHeaders(parser.getHeaderData().c_str(),
				parser.getHeaderData().size());
			
			contentLength = atol(parser.getHeader("CONTENT_LENGTH"));
			sendRequestBody(session,
				clientFd,
				partialRequestBody,
				contentLength);
			
			session->shutdownWriter();
			forwardResponse(session, clientFd);
		} catch (const boost::thread_interrupted &) {
			throw;
		} catch (const tracable_exception &e) {
			P_ERROR("Uncaught exception in PassengerServer client thread:\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace:\n" << e.backtrace());
		} catch (const exception &e) {
			P_ERROR("Uncaught exception in PassengerServer client thread:\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace: not available");
		} catch (...) {
			P_ERROR("Uncaught unknown exception in PassengerServer client thread.");
		}
	}
	
	void threadMain() {
		TRACE_POINT();
		try {
			while (true) {
				UPDATE_TRACE_POINT();
				FileDescriptor fd(acceptConnection());
				handleRequest(fd);
			}
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "Client thread " << this << " interrupted.");
		} catch (const tracable_exception &e) {
			P_ERROR("Uncaught exception in PassengerServer client thread:\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace:\n" << e.backtrace());
			abort();
		} catch (const exception &e) {
			P_ERROR("Uncaught exception in PassengerServer client thread:\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace: not available");
			abort();
		} catch (...) {
			P_ERROR("Uncaught unknown exception in PassengerServer client thread.");
			throw;
		}
	}
	
public:
	Client(StandardApplicationPoolPtr &pool, const string &password, int serverSocket) {
		this->pool = pool;
		this->password = password;
		this->serverSocket = serverSocket;
		thr = new oxt::thread(
			bind(&Client::threadMain, this),
			"Thread", CLIENT_THREAD_STACK_SIZE
		);
	}
	
	~Client() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		
		thr->interrupt_and_join();
		delete thr;
	}
};

typedef shared_ptr<Client> ClientPtr;

class Server {
private:
	static const unsigned int BACKLOG_SIZE = 50;

	string password;
	int adminPipe;
	int serverSocket;
	unsigned int numberOfThreads;
	set<ClientPtr> clients;
	StandardApplicationPoolPtr pool;
	
	void initializePool(const string &rootDir, const string &ruby,
	                    unsigned int maxPoolSize) {
		pool = ptr(new StandardApplicationPool(
			rootDir + "/bin/passenger-spawn-server",
			"", ruby
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
			ClientPtr client(new Client(pool, password, serverSocket));
			clients.insert(client);
		}
	}

public:
	Server(const string &password, const string &rootDir, const string &ruby,
	       int adminPipe, unsigned int maxPoolSize) {
		this->password  = password;
		this->adminPipe = adminPipe;
		numberOfThreads = maxPoolSize * 4;
		initializePool(rootDir, ruby, maxPoolSize);
		startListening();
	}
	
	~Server() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		
		P_DEBUG("Shutting down helper server...");
		clients.clear();
		P_TRACE(2, "All threads have been shut down.");
		syscalls::close(serverSocket);
		syscalls::close(adminPipe);
	}
	
	void start() {
		TRACE_POINT();
		char buf;
		
		startClientHandlerThreads();
		P_TRACE(2, "Entering main loop.");
		try {
			syscalls::read(adminPipe, &buf, 1);
		} catch (const boost::thread_interrupted &) {
			// Do nothing.
		}
	}
};

static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

static string
receivePassword(int adminPipe) {
	TRACE_POINT();
	MessageChannel channel(adminPipe);
	char buf[HELPER_SERVER_PASSWORD_SIZE];
	
	if (!channel.readRaw(buf, HELPER_SERVER_PASSWORD_SIZE)) {
		P_ERROR("Could not read password from the admin pipe.");
	}
	return string(buf, HELPER_SERVER_PASSWORD_SIZE);
}

int
main(int argc, char *argv[]) {
	TRACE_POINT();
	try {
		string password;
		string rootDir  = argv[1];
		string ruby     = argv[2];
		int adminPipe   = atoi(argv[3]);
		int maxPoolSize = atoi(argv[4]);
		
		setup_syscall_interruption_support();
		setLogLevel(2);
		ignoreSigpipe();
		password = receivePassword(adminPipe);
		Server(password, rootDir, ruby, adminPipe, maxPoolSize).start();
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
	
	P_TRACE(2, "Helper server exited.");
	return 0;
}

