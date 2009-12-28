/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>

#include <set>
#include <vector>
#include <string>

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include "oxt/thread.hpp"
#include "oxt/system_calls.hpp"

#include "ScgiRequestParser.h"
#include "HttpStatusExtractor.h"

#include "ApplicationPool/Pool.h"
#include "ApplicationPool/Server.h"
#include "Session.h"
#include "PoolOptions.h"
#include "MessageServer.h"
#include "BacktracesServer.h"
#include "FileDescriptor.h"
#include "Timer.h"
#include "ServerInstanceDir.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace boost;
using namespace oxt;
using namespace Passenger;

#define REQUEST_SOCKET_PASSWORD_SIZE     64

struct ClientDisconnectedException { };

class ExitHandler: public MessageServer::Handler {
private:
	EventFd &exitEvent;
	
public:
	ExitHandler(EventFd &_exitEvent)
		: exitEvent(_exitEvent)
	{ }
	
	virtual bool processMessage(MessageServer::CommonClientContext &commonContext,
	                            MessageServer::ClientContextPtr &handlerSpecificContext,
	                            const vector<string> &args)
	{
		if (args[0] == "exit") {
			TRACE_POINT();
			commonContext.requireRights(Account::EXIT);
			UPDATE_TRACE_POINT();
			exitEvent.notify();
			UPDATE_TRACE_POINT();
			commonContext.channel.write("exit command received", NULL);
			return true;
		} else {
			return false;
		}
	}
};

/**
 * A representation of a Client from the Server's point of view. This class
 * contains the methods used to communicate from a server to a connected
 * client, i.e. it is a client handler.
 * These Client instances will communicate concurrently with the server through
 * threads. Considering the overhead of these threads, i.e. setup and teardown
 * costs and the volatility of client requests, these client instances will be
 * pooled. It is for this reason that the State design pattern has been applied:
 * this class can be considered as being a skeleton implemention whose state
 * --e.g. the client file descriptor-- needs to be provided in order to function
 * properly.
 */
class Client {
private:
	/** Maximum allowed size of SCGI headers. */
	static const unsigned int MAX_HEADER_SIZE = 1024 * 128;
	/** The client thread stack size in bytes. */
	static const int CLIENT_THREAD_STACK_SIZE =
		// Give 64 KB of normal stack space and more stack space
		// for storing the session header.
		(1024 * 64) + MAX_HEADER_SIZE + 1024;
	
	/** The client number for this Client object, assigned by Server. */
	unsigned int number;
	
	/** The application pool to which this Client object belongs to. */
	ApplicationPool::Ptr pool;
	
	/** This client's password. */
	string password;
	
	/** Whether privilege lowering should be used. */
	bool lowerPrivilege;
	
	/** The user that spawned processes should run as, if initial attempt
	 * at privilege lowering failed. */
	string lowestUser;
	
	/** The server socket file descriptor. */
	int serverSocket;
	
	/** The transaction logger to use. */
	TxnLoggerPtr txnLogger;
	
	/** This client's thread. */
	oxt::thread *thr;
	
	/** A timer for measuring how long this worker thread has been doing
	 * nothing (i.e. waiting for a connection).
	 */
	Timer inactivityTimer;
	
	/**
	 * Attempts to accept a connection made by the client.
	 *
	 * @return The file descriptor corresponding to the accepted connection.
	 * @throws SystemException Could not accept new connection.
	 */
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
	
	/**
	 * Reads and checks the password of a client message channel identified by the given file descriptor.
	 * The HelperServer makes extensive use of Unix Sockets that would normally allow other processes to
	 * connect to it as well. In our case, we just want to limit this to Nginx and it is for this reason
	 * that we've secured communication channels between this server and its clients with passwords.
	 * This method indicates whether or not the password of this client channel matches the one known to
	 * the server.
	 * 
	 * @param fd The file descriptor identifying the client message channel.
	 * @return True if the password of the client channel indicated by the given file descriptor
	 *   matches the password known to the server. False will be returned if either the
	 *   passwords don't match or EOF has been encountered.
	 */
	bool readAndCheckPassword(FileDescriptor &fd) {
		TRACE_POINT();
		MessageChannel channel(fd);
		char buf[REQUEST_SOCKET_PASSWORD_SIZE];
		
		if (channel.readRaw(buf, sizeof(buf))) {
			const char *password_data;
			
			password_data = const_cast<const string &>(password).c_str();
			return memcmp(password_data, buf, sizeof(buf)) == 0;
		} else {
			return false;
		}
	}
	
	/**
	 * Reads and parses the request headers from the given file descriptor with the given SCGI request parser
	 * and if succesful, assigns the remainder of the request (i.e. non SCGI header data) to the given 
	 * requestBody.
	 *
	 * @param fd The file descriptor to read and parse from.
	 * @param parser The ScgiRequestParser to use for parsing the SCGI headers.
	 * @param requestBody The requestBody that was extracted as a result from parsing the SCGI headers.
	 * @return True if the request was succesfully read and parsed. False if an invalid SCGI header was
	 *   received by the parser or if the header information was invalid.
	 * @throws SystemException Request header could not be read.
	 */
	bool readAndParseRequestHeaders(FileDescriptor &fd, ScgiRequestParser &parser, string &requestBody) {
		TRACE_POINT();
		char buf[1024 * 16];
		ssize_t size;
		unsigned int accepted = 0;
		
		do {
			size = syscalls::read(fd, buf, sizeof(buf));
			if (size == -1) {
				throw SystemException("Cannot read request header", errno);
			} else if (size == 0) {
				break;
			} else {
				accepted = parser.feed(buf, size);
			}
		} while (parser.acceptingInput());

		if (parser.getState() != ScgiRequestParser::DONE) {
			if (parser.getState() == ScgiRequestParser::ERROR
			 && parser.getErrorReason() == ScgiRequestParser::LIMIT_REACHED) {
				P_ERROR("SCGI header too large.");
			} else {
				P_ERROR("Invalid SCGI header received.");
			}
			return false;
		} else if (!parser.hasHeader("DOCUMENT_ROOT")) {
			P_ERROR("DOCUMENT_ROOT header is missing.");
			return false;
		} else {
			requestBody.assign(buf + accepted, size - accepted);
			return true;
		}
	}
	
	/**
	 * Sends a request body to this client. The <tt>partialRequestBody</tt> will first be
	 * sent to the specified <tt>session</tt>, but if the specified <tt>contentLength</tt>
	 * is larger than the size of the <tt>partialRequestBody</tt>, then this method will
	 * attempt to read the remaining bytes from the specified <tt>clientFd</tt> and send it
	 * to the <tt>session</tt> as well until <tt>contentLength</tt> bytes have been sent in
	 * total.
	 *
	 * @param session The Ruby on Rails application instance.
	 * @param clientFd The client file descriptor to send the request body to.
	 * @param partialRequestBody The partial request body to send to this client.
	 * @param contentLength The content length of the request body in bytes.
	 * @throws SystemException Request body could not be read from the specified
	 *   <tt>clientFd</tt>.
	 */
	void sendRequestBody(SessionPtr &session,
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
	
	/**
	 * Forwards an HTTP response from the given (Rails) <tt>session</tt> to the
	 * given <tt>clientFd</tt>.
	 * 
	 * @param session The Ruby on Rails session to read the response from.
	 * @param clientFd The client file descriptor to write the response to.
	 * @throws SystemException Something went wrong while reading the response
	 *                         from the backend process or while writing to the
	 *                         response back to the web server.
	 * @throws ClientDisconnectedException The HTTP client closed the connection
	 *                                     before we were able to send back the
	 *                                     full response.
	 */
	void forwardResponse(SessionPtr &session, FileDescriptor &clientFd) {
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
				try {
					string statusLine("HTTP/1.1 ");
					statusLine.append(ex.getStatusLine());
					UPDATE_TRACE_POINT();
					output.writeRaw(statusLine.c_str(), statusLine.size());
					UPDATE_TRACE_POINT();
					output.writeRaw(ex.getBuffer().c_str(), ex.getBuffer().size());
					break;
				} catch (const SystemException &e) {
					if (e.code() == EPIPE) {
						throw ClientDisconnectedException();
					} else {
						throw;
					}
				}
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
				try {
					output.writeRaw(buf, size);
				} catch (const SystemException &e) {
					if (e.code() == EPIPE) {
						throw ClientDisconnectedException();
					} else {
						throw;
					}
				}
			}
		}
	}
	
	/**
	 * Handles a spawn related exception by writing an appropriate HTTP error response (500)
	 * for the given spawn exception <tt>e</ee> to given file descriptor <tt>fd</tt>'s message
	 * channel.
	 *
	 * @param fd The file descriptor identifying the message channel to write the given
	 *   spawn exception <tt>e</tt> to.
	 * @param e The spawn exception to be written to the given <tt>fd</tt>'s message
	 *   channel.
	 * @param friendly Whether to show a friendly error page.
	 */
	void handleSpawnException(FileDescriptor &fd, const SpawnException &e, bool friendly) {
		MessageChannel channel(fd);
		channel.writeRaw("HTTP/1.1 500 Internal Server Error\x0D\x0A");
		channel.writeRaw("Status: 500 Internal Server Error\x0D\x0A");
		channel.writeRaw("Connection: close\x0D\x0A");
		channel.writeRaw("Content-Type: text/html; charset=utf-8\x0D\x0A");
		
		if (friendly) {
			if (e.hasErrorPage()) {
				channel.writeRaw("Content-Length: " +
					toString(e.getErrorPage().size()) +
					"\x0D\x0A");
				channel.writeRaw("\x0D\x0A");
				channel.writeRaw(e.getErrorPage());
			} else {
				channel.writeRaw("Content-Length: " +
					toString(strlen(e.what())) + "\x0D\x0A");
				channel.writeRaw("\x0D\x0A");
				channel.writeRaw(e.what());
			}
		} else {
			const char body[] = "<h1>Internal Server Error (500)</h1>";
			channel.writeRaw("Content-Length: " +
				toString(strlen(body)) + "\x0D\x0A");
			channel.writeRaw("\x0D\x0A");
			channel.writeRaw(body);
		}
	}
	
	/**
	 * Handles an SCGI request from a client whose identity is derived by the given <tt>clientFd</tt>.
	 *
	 * @param clientFd The file descriptor identifying the client to handle the request from.
	 */
	void handleRequest(FileDescriptor &clientFd) {
		TRACE_POINT();
		ScgiRequestParser parser(MAX_HEADER_SIZE);
		string partialRequestBody;
		unsigned long contentLength;
		
		if (!readAndCheckPassword(clientFd)) {
			P_ERROR("Client did not send a correct password.");
			return;
		}
		if (!readAndParseRequestHeaders(clientFd, parser, partialRequestBody)) {
			return;
		}
		
		try {
			TxnLogPtr log = txnLogger->newTransaction(parser.getHeader("PASSENGER_ANALYTICS_ID"));
			
			TxnScopeLog requestProcessingScope(log, "request processing");
			
			PoolOptions options;
			if (parser.getHeader("SCRIPT_NAME").empty()) {
				options.appRoot = extractDirName(parser.getHeader("DOCUMENT_ROOT"));
			} else {
				options.appRoot = extractDirName(resolveSymlink(parser.getHeader("DOCUMENT_ROOT")));
				options.baseURI = parser.getHeader("SCRIPT_NAME");
			}
			options.useGlobalQueue = parser.getHeader("PASSENGER_USE_GLOBAL_QUEUE") == "true";
			options.environment    = parser.getHeader("PASSENGER_ENVIRONMENT");
			options.spawnMethod    = parser.getHeader("PASSENGER_SPAWN_METHOD");
			options.lowerPrivilege = lowerPrivilege;
			options.lowestUser     = lowestUser;
			options.appType        = parser.getHeader("PASSENGER_APP_TYPE");
			options.minProcesses   = atol(parser.getHeader("PASSENGER_MIN_INSTANCES"));
			options.frameworkSpawnerTimeout = atol(parser.getHeader("PASSENGER_FRAMEWORK_SPAWNER_IDLE_TIME"));
			options.appSpawnerTimeout       = atol(parser.getHeader("PASSENGER_APP_SPAWNER_IDLE_TIME"));
			
			/***********************/
			/***********************/
			
			try {
				SessionPtr session;
				
				{
					TxnScopeLog sl(log, "get from pool");
					session = pool->get(options);
					sl.success();
				}
				
				UPDATE_TRACE_POINT();
				TxnScopeLog requestProxyingScope(log, "request proxying");
				
				char headers[parser.getHeaderData().size() +
					sizeof("PASSENGER_CONNECT_PASSWORD") +
					session->getConnectPassword().size() + 1];
				memcpy(headers, parser.getHeaderData().c_str(), parser.getHeaderData().size());
				memcpy(headers + parser.getHeaderData().size(),
					"PASSENGER_CONNECT_PASSWORD",
					sizeof("PASSENGER_CONNECT_PASSWORD"));
				memcpy(headers + parser.getHeaderData().size() + sizeof("PASSENGER_CONNECT_PASSWORD"),
					session->getConnectPassword().c_str(),
					session->getConnectPassword().size() + 1);
				session->sendHeaders(headers, sizeof(headers));
				
				contentLength = atol(
					parser.getHeader("CONTENT_LENGTH").c_str());
				
				sendRequestBody(session,
					clientFd,
					partialRequestBody,
					contentLength);
				
				session->shutdownWriter();
				forwardResponse(session, clientFd);
				
				requestProxyingScope.success();
			} catch (const SpawnException &e) {
				handleSpawnException(clientFd, e,
					parser.getHeader("PASSENGER_FRIENDLY_ERROR_PAGES") == "true");
			} catch (const ClientDisconnectedException &) {
				P_WARN("Couldn't forward the HTTP response back to the HTTP client: "
					"It seems the user clicked on the 'Stop' button in his "
					"browser.");
			}
			
			requestProcessingScope.success();
			clientFd.close();
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
	
	/**
	 * This client's main thread, responsible for accepting connections made by a client
	 * to the server and to handle its request.
	 *
	 * @see acceptConnection(void)
	 * @see handleRequest(FileDescriptor)
	 */
	void threadMain() {
		TRACE_POINT();
		try {
			while (true) {
				UPDATE_TRACE_POINT();
				inactivityTimer.start();
				FileDescriptor fd(acceptConnection());
				inactivityTimer.stop();
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
	/**
	 * Constructs a client handler for the server with the given arguments and runs
	 * it in its own thread.
	 *
	 * @param number The id assigned by the server to identify this client by.
	 * @param pool The application pool where this client belongs to.
	 * @param password The password that is required to connect to this client handler.
	 *   This value is determined and assigned by the server.
	 * @param lowerPrivilege Whether privilege lowering should be used.
	 * @param lowestUser The user that spawned processes should run as, if
	 *   initial attempt at privilege lowering failed.
	 * @param serverSocket The server socket to accept this clients connection from.
	 */
	Client(unsigned int number, ApplicationPool::Ptr pool,
	       const string &password, bool lowerPrivilege,
	       const string &lowestUser, int serverSocket,
	       TxnLoggerPtr logger)
		: inactivityTimer(false)
	{
		this->number = number;
		this->pool = pool;
		this->password = password;
		this->lowerPrivilege = lowerPrivilege;
		this->lowestUser = lowestUser;
		this->serverSocket = serverSocket;
		this->txnLogger = logger;
		thr = new oxt::thread(
			bind(&Client::threadMain, this),
			"Client thread " + toString(number),
			CLIENT_THREAD_STACK_SIZE
		);
	}
	
	/**
	 * Destroys this client and its thread.
	 */
	~Client() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		
		if (thr->joinable()) {
			thr->interrupt_and_join();
		}
		delete thr;
	}
	
	oxt::thread *getThread() const {
		return thr;
	}
	
	unsigned long long inactivityTime() const {
		return inactivityTimer.elapsed();
	}
	
	void resetInactivityTimer() {
		inactivityTimer.start();
	}
};

typedef shared_ptr<Client> ClientPtr;

/**
 * A representation of the Server responsible for handling Client instances.
 *
 * @see Client
 */
class Server {
private:
	static const int MESSAGE_SERVER_THREAD_STACK_SIZE = 64 * 128;
	
	FileDescriptor feedbackFd;
	bool userSwitching;
	string defaultUser;
	unsigned int numberOfThreads;
	FileDescriptor requestSocket;
	string requestSocketPassword;
	MessageChannel feedbackChannel;
	ServerInstanceDir serverInstanceDir;
	ServerInstanceDir::GenerationPtr generation;
	set<ClientPtr> clients;
	TxnLoggerPtr txnLogger;
	ApplicationPool::Ptr pool;
	AccountsDatabasePtr accountsDatabase;
	MessageServerPtr messageServer;
	shared_ptr<oxt::thread> messageServerThread;
	EventFd exitEvent;
	
	string getRequestSocketFilename() const {
		return generation->getPath() + "/request.socket";
	}
	
	/**
	 * Starts listening for client connections on this server's request socket.
	 *
	 * @throws SystemException Something went wrong while trying to create and bind to the Unix socket.
	 * @throws RuntimeException Something went wrong.
	 */
	void startListening() {
		this_thread::disable_syscall_interruption dsi;
		requestSocket = createUnixServer(getRequestSocketFilename().c_str());
		
		int ret;
		do {
			ret = chmod(getRequestSocketFilename().c_str(), S_ISVTX |
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IWGRP | S_IXGRP |
				S_IROTH | S_IWOTH | S_IXOTH);
		} while (ret == -1 && errno == EINTR);
	}
	
	/**
	 * Starts the client handler threads that are responsible for handling the communication
	 * between the client and this Server.
	 *
	 * @see Client
	 */
	void startClientHandlerThreads() {
		for (unsigned int i = 0; i < numberOfThreads; i++) {
			ClientPtr client(new Client(i + 1, pool, requestSocketPassword,
				userSwitching, defaultUser, requestSocket, txnLogger));
			clients.insert(client);
		}
	}
	
	/**
	 * Lowers this process's privilege to that of <em>username</em>.
	 */
	void lowerPrivilege(const string &username) {
		struct passwd *entry;
		
		entry = getpwnam(username.c_str());
		if (entry != NULL) {
			if (initgroups(username.c_str(), entry->pw_gid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower Passenger HelperServer's "
					"privilege to that of user '" << username <<
					"': cannot set supplementary groups for this "
					"user: " << strerror(e) << " (" << e << ")");
			}
			if (setgid(entry->pw_gid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower Passenger HelperServer's "
					"privilege to that of user '" << username <<
					"': cannot set group ID: " << strerror(e) <<
					" (" << e << ")");
			}
			if (setuid(entry->pw_uid) != 0) {
				int e = errno;
				P_WARN("WARNING: Unable to lower Passenger HelperServer's "
					"privilege to that of user '" << username <<
					"': cannot set user ID: " << strerror(e) <<
					" (" << e << ")");
			}
		} else {
			P_WARN("WARNING: Unable to lower Passenger HelperServer's "
				"privilege to that of user '" << username <<
				"': user does not exist.");
		}
	}
	
	void resetWorkerThreadInactivityTimers() {
		set<ClientPtr>::iterator it;
		
		for (it = clients.begin(); it != clients.end(); it++) {
			ClientPtr client = *it;
			client->resetInactivityTimer();
		}
	}
	
	unsigned long long minWorkerThreadInactivityTime() const {
		set<ClientPtr>::const_iterator it;
		unsigned long long result = 0;
		
		for (it = clients.begin(); it != clients.end(); it++) {
			ClientPtr client = *it;
			unsigned long long inactivityTime = client->inactivityTime();
			if (inactivityTime < result || it == clients.begin()) {
				result = inactivityTime;
			}
		}
		return result;
	}
	
public:
	Server(FileDescriptor feedbackFd, pid_t webServerPid, const string &tempDir,
		bool userSwitching, const string &defaultUser, uid_t workerUid, gid_t workerGid,
		const string &passengerRoot, const string &rubyCommand, unsigned int generationNumber,
		unsigned int maxPoolSize, unsigned int maxInstancesPerApp, unsigned int poolIdleTime,
		const string &analyticsLogDir)
		: serverInstanceDir(webServerPid, tempDir, false)
	{
		vector<string> args;
		string messageSocketPassword;
		string loggingSocketPassword;
		
		TRACE_POINT();
		this->feedbackFd    = feedbackFd;
		this->userSwitching = userSwitching;
		this->defaultUser   = defaultUser;
		feedbackChannel     = MessageChannel(feedbackFd);
		numberOfThreads     = maxPoolSize * 4;
		
		UPDATE_TRACE_POINT();
		if (!feedbackChannel.read(args)) {
			throw IOException("The watchdog unexpectedly closed the connection.");
		}
		if (args[0] != "passwords") {
			throw IOException("Unexpected input message '" + args[0] + "'");
		}
		requestSocketPassword = Base64::decode(args[1]);
		messageSocketPassword = Base64::decode(args[2]);
		loggingSocketPassword = Base64::decode(args[3]);
		generation = serverInstanceDir.getGeneration(generationNumber);
		startListening();
		accountsDatabase = AccountsDatabase::createDefault(generation, userSwitching, defaultUser);
		accountsDatabase->add("_web_server", messageSocketPassword, false, Account::EXIT);
		messageServer = ptr(new MessageServer(generation->getPath() + "/socket", accountsDatabase));
		
		if (geteuid() == 0 && !userSwitching) {
			lowerPrivilege(defaultUser);
		}
		
		UPDATE_TRACE_POINT();
		txnLogger = ptr(new TxnLogger(analyticsLogDir,
			generation->getPath() + "/logging.socket",
			"logging", loggingSocketPassword));
		
		pool = ptr(new ApplicationPool::Pool(
			findSpawnServer(passengerRoot.c_str()), generation,
			accountsDatabase->get("_backend"), rubyCommand
		));
		pool->setMax(maxPoolSize);
		pool->setMaxPerApp(maxInstancesPerApp);
		pool->setMaxIdleTime(poolIdleTime);
		
		messageServer->addHandler(ptr(new ApplicationPool::Server(pool)));
		messageServer->addHandler(ptr(new BacktracesServer()));
		messageServer->addHandler(ptr(new ExitHandler(exitEvent)));
		
		UPDATE_TRACE_POINT();
		feedbackChannel.write("initialized",
			getRequestSocketFilename().c_str(),
			messageServer->getSocketFilename().c_str(),
			NULL);
	}
	
	~Server() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		oxt::thread *threads[clients.size()];
		set<ClientPtr>::iterator it;
		unsigned int i = 0;
		
		P_DEBUG("Shutting down helper server...");
		if (messageServerThread != NULL) {
			messageServerThread->interrupt_and_join();
		}
		
		for (it = clients.begin(); it != clients.end(); it++, i++) {
			ClientPtr client = *it;
			threads[i] = client->getThread();
		}
		oxt::thread::interrupt_and_join_multiple(threads, clients.size());
		clients.clear();
		
		P_TRACE(2, "All threads have been shut down.");
	}
	
	void mainLoop() {
		TRACE_POINT();
		
		startClientHandlerThreads();
		messageServerThread = ptr(new oxt::thread(
			boost::bind(&MessageServer::mainLoop, messageServer.get()),
			"MessageServer thread", MESSAGE_SERVER_THREAD_STACK_SIZE
		));
		
		/* Wait until the watchdog closes the feedback fd (meaning it
		 * was killed) or until we receive an exit message.
		 */
		this_thread::disable_syscall_interruption dsi;
		fd_set fds;
		int largestFd;
		
		FD_ZERO(&fds);
		FD_SET(feedbackFd, &fds);
		FD_SET(exitEvent.fd(), &fds);
		largestFd = (feedbackFd > exitEvent.fd()) ? (int) feedbackFd : exitEvent.fd();
		UPDATE_TRACE_POINT();
		if (syscalls::select(largestFd + 1, &fds, NULL, NULL, NULL) == -1) {
			int e = errno;
			throw SystemException("select() failed", e);
		}
		
		if (FD_ISSET(feedbackFd, &fds)) {
			/* If the watchdog has been killed then we'll kill all descendant
			 * processes and exit. There's no point in keeping this helper
			 * server running because we can't detect when the web server exits,
			 * and because this helper server doesn't own the server instance
			 * directory. As soon as passenger-status is run, the server
			 * instance directory will be cleaned up, making this helper server
			 * inaccessible.
			 */
			syscalls::killpg(getpgrp(), SIGKILL);
			_exit(2); // In case killpg() fails.
		} else {
			/* We received an exit command. We want to exit 5 seconds after
			 * all worker threads have become inactive.
			 */
			resetWorkerThreadInactivityTimers();
			while (minWorkerThreadInactivityTime() < 5000) {
				syscalls::usleep(250000);
			}
		}
	}
};

/**
 * Ignores the SIGPIPE signal that in general is raised when a computer program attempts
 * to write to a pipe without a processes connected to the other end. This is used to
 * prevent Nginx from getting killed by the default signal handler when it attempts to
 * write the server password to the HelperServer in the situation that the HelperServer
 * failed to start.
 */
static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

/**
 * Initializes and starts the helper server that is responsible for handling communication
 * between Nginx and the backend Rails processes.
 *
 * @see Server
 * @see Client
 */
int
main(int argc, char *argv[]) {
	TRACE_POINT();
	try {
		/* Become the process group leader so that the watchdog can kill the
		 * HelperServer as well as all descendant processes. */
		setpgrp();
		
		ignoreSigpipe();
		setup_syscall_interruption_support();
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
		
		unsigned int   logLevel   = atoi(argv[1]);
		FileDescriptor feedbackFd = atoi(argv[2]);
		pid_t   webServerPid  = (pid_t) atoll(argv[3]);
		string  tempDir       = argv[4];
		bool    userSwitching = strcmp(argv[5], "true") == 0;
		string  defaultUser   = argv[6];
		uid_t   workerUid     = (uid_t) atoll(argv[7]);
		gid_t   workerGid     = (uid_t) atoll(argv[8]);
		string  passengerRoot = argv[9];
		string  rubyCommand   = argv[10];
		unsigned int generationNumber   = atoll(argv[11]);
		unsigned int maxPoolSize        = atoi(argv[12]);
		unsigned int maxInstancesPerApp = atoi(argv[13]);
		unsigned int poolIdleTime       = atoi(argv[14]);
		string  analyticsLogDir = argv[15];
		
		// Change process title.
		strncpy(argv[0], "PassengerHelperServer", strlen(argv[0]));
		for (int i = 1; i < argc; i++) {
			memset(argv[i], '\0', strlen(argv[i]));
		}
		
		UPDATE_TRACE_POINT();
		setLogLevel(logLevel);
		Server server(feedbackFd, webServerPid, tempDir,
			userSwitching, defaultUser, workerUid, workerGid,
			passengerRoot, rubyCommand, generationNumber,
			maxPoolSize, maxInstancesPerApp, poolIdleTime,
			analyticsLogDir);
		P_DEBUG("Passenger helper server started on PID " << getpid());
		
		UPDATE_TRACE_POINT();
		server.mainLoop();
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
