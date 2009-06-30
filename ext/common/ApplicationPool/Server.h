/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_SERVER_H_
#define _PASSENGER_APPLICATION_POOL_SERVER_H_

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/dynamic_thread_group.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

#include "ApplicationPool/Pool.h"
#include "FileDescriptor.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {
namespace ApplicationPool {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * ApplicationPoolServer exposes a StandardApplicationPool to external processes through
 * a Unix domain server socket. This allows one to use a StandardApplicationPool in a
 * multi-process environment. ApplicationPoolClient can be used to access a pool that's
 * exposed via ApplicationPoolServer.
 *
 * <h2>Usage</h2>
 * Construct an ApplicationPoolServer object and call the mainLoop() method on it.
 *
 * <h2>Concurrency model</h2>
 * Each client is handled by a seperate thread. This is necessary because we the current
 * algorithm for StandardApplicationPool::get() can block (in the case that the spawning
 * limit has been exceeded or when global queuing is used and all application instances
 * are busy). While it is possible to get around this problem without using threads, a
 * thread-based implementation is easier to write.
 *
 * <h2>Security model</h2>
 * Experience has shown that it is inadequate to protect the socket file with traditional
 * Unix filesystem permissions. The web server may consist of multiple worker processes,
 * each running as a seperate user. We want each web server worker process to be able to
 * connect to the ApplicationPool server. Although ACLs allow this, not every platform
 * supports ACLs by default.
 *
 * Instead, we make the server socket world-writable, and depend on a username/password
 * authentication model for security instead. When creating Server you must also supply
 * an accounts database. Each account has a username, password, and a set of rights.
 *
 * @ingroup Support
 */
class Server {
private:
	static const unsigned int CLIENT_THREAD_STACK_SIZE = 128 * 1024;
	
	/**
	 * This exception indicates that something went wrong while comunicating with the client.
	 * Only used within EnvironmentVariablesFetcher.
	 */
	class ClientCommunicationError: public oxt::tracable_exception {
	private:
		string briefMessage;
		string systemMessage;
		string fullMessage;
		int m_code;
	public:
		/**
		 * Create a new ClientCommunicationError.
		 *
		 * @param briefMessage A brief message describing the error.
		 * @param errorCode An optional error code, i.e. the value of errno right after the error occured, if applicable.
		 * @note A system description of the error will be appended to the given message.
		 *    For example, if <tt>errorCode</tt> is <tt>EBADF</tt>, and <tt>briefMessage</tt>
		 *    is <em>"Something happened"</em>, then what() will return <em>"Something happened: Bad
		 *    file descriptor (10)"</em> (if 10 is the number for EBADF).
		 * @post code() == errorCode
		 * @post brief() == briefMessage
		 */
		ClientCommunicationError(const string &briefMessage, int errorCode = -1) {
			if (errorCode != -1) {
				stringstream str;
				
				str << strerror(errorCode) << " (" << errorCode << ")";
				systemMessage = str.str();
			}
			setBriefMessage(briefMessage);
			m_code = errorCode;
		}

		virtual ~ClientCommunicationError() throw() {}

		virtual const char *what() const throw() {
			return fullMessage.c_str();
		}

		void setBriefMessage(const string &message) {
			briefMessage = message;
			if (systemMessage.empty()) {
				fullMessage = briefMessage;
			} else {
				fullMessage = briefMessage + ": " + systemMessage;
			}
		}

		/**
		 * The value of <tt>errno</tt> at the time the error occured.
		 */
		int code() const throw() {
			return m_code;
		}

		/**
		 * Returns a brief version of the exception message. This message does
		 * not include the system error description, and is equivalent to the
		 * value of the <tt>message</tt> parameter as passed to the constructor.
		 */
		string brief() const throw() {
			return briefMessage;
		}

		/**
		 * Returns the system's error message. This message contains both the
		 * content of <tt>strerror(errno)</tt> and the errno number itself.
		 *
		 * @post if code() == -1: result.empty()
		 */
		string sys() const throw() {
			return systemMessage;
		}
	};
	
	/**
	 * A StringListCreator which fetches its items from the client.
	 * Used as an optimization for ApplicationPoolServer::processGet():
	 * environment variables are only serialized by the client process
	 * if a new backend process is being spawned.
	 */
	class EnvironmentVariablesFetcher: public StringListCreator {
	private:
		MessageChannel &channel;
		PoolOptions &options;
	public:
		EnvironmentVariablesFetcher(MessageChannel &theChannel, PoolOptions &theOptions)
			: channel(theChannel),
			  options(theOptions)
		{ }
		
		/**
		 * @throws ClientCommunicationError
		 */
		virtual const StringListPtr getItems() const {
			string data;
			
			/* If an I/O error occurred while communicating with the client,
			 * then throw a ClientCommunicationException, which will bubble
			 * all the way up to the thread main loop, where the connection
			 * with the client will be broken.
			 */
			try {
				channel.write("getEnvironmentVariables", NULL);
			} catch (const SystemException &e) {
				throw ClientCommunicationError(
					"Unable to send a 'getEnvironmentVariables' request to the client",
					e.code());
			}
			try {
				if (!channel.readScalar(data)) {
					throw ClientCommunicationError("Unable to read a reply from the client for the 'getEnvironmentVariables' request.");
				}
			} catch (const SystemException &e) {
				throw ClientCommunicationError(
					"Unable to read a reply from the client for the 'getEnvironmentVariables' request",
					e.code());
			}
			
			if (!data.empty()) {
				SimpleStringListCreator list(data);
				return list.getItems();
			} else {
				return ptr(new StringList());
			}
		}
	};
	
	/**
	 * Each client handled by the server has an associated ClientContext,
	 * which stores state associated with the client.
	 */
	struct ClientContext {
		/** The client's socket file descriptor. */
		FileDescriptor fd;
		
		/** The channel that's associated with the client's socket. */
		MessageChannel channel;
		
		/** The account with which the client has authenticated. */
		AccountPtr account;
		
		/**
		 * Maps session ID to sessions created by ApplicationPool::get(). Session IDs
		 * are sent back to the ApplicationPool client. This allows the ApplicationPool
		 * client to tell us which of the multiple sessions it wants to close, later on.
		 */
		map<int, Application::SessionPtr> sessions;
		
		/** Last used session ID. */
		int lastSessionID;
		
		ClientContext(FileDescriptor &theFd, AccountPtr theAccount)
			: fd(theFd),
			  channel(fd),
			  account(theAccount)
		{
			lastSessionID = 0;
		}
		
		/** Returns a string representation for this client. */
		string name() {
			return toString(channel.fileno());
		}
	};
	
	/** The filename of the server socket on which this ApplicationPoolServer is listening. */
	string socketFilename;
	/** An accounts database, used for authenticating clients. */
	AccountsDatabasePtr accountsDatabase;
	/** The StandardApplicationPool that's being exposed through the socket. */
	PoolPtr pool;
	
	/** The client threads. */
	dynamic_thread_group threadGroup;
	/** The server socket's file descriptor.
	 * @invariant serverFd >= 0
	 */
	int serverFd;
	
	
	/*********************************************
	 * Message handler methods
	 *********************************************/
	
	void processGet(ClientContext &context, const vector<string> &args) {
		TRACE_POINT();
		Application::SessionPtr session;
		bool failed = false;
		
		requireRights(context, Account::GET);
		
		try {
			PoolOptions options(args, 1);
			options.environmentVariables = ptr(new EnvironmentVariablesFetcher(
				context.channel, options));
			session = pool->get(options);
			context.sessions[context.lastSessionID] = session;
			context.lastSessionID++;
		} catch (const SpawnException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			
			if (e.hasErrorPage()) {
				P_TRACE(3, "Client " << context.name() << ": SpawnException "
					"occured (with error page)");
				context.channel.write("SpawnException", e.what(), "true", NULL);
				context.channel.writeScalar(e.getErrorPage());
			} else {
				P_TRACE(3, "Client " << context.name() << ": SpawnException "
					"occured (no error page)");
				context.channel.write("SpawnException", e.what(), "false", NULL);
			}
			failed = true;
		} catch (const BusyException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			context.channel.write("BusyException", e.what(), NULL);
			failed = true;
		} catch (const IOException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			context.channel.write("IOException", e.what(), NULL);
			failed = true;
		}
		UPDATE_TRACE_POINT();
		if (!failed) {
			this_thread::disable_syscall_interruption dsi;
			try {
				UPDATE_TRACE_POINT();
				context.channel.write("ok", toString(session->getPid()).c_str(),
					toString(context.lastSessionID - 1).c_str(), NULL);
				UPDATE_TRACE_POINT();
				context.channel.writeFileDescriptor(session->getStream());
				UPDATE_TRACE_POINT();
				session->closeStream();
			} catch (const exception &e) {
				P_TRACE(3, "Client " << context.name() << ": could not send "
					"'ok' back to the ApplicationPool client: " <<
					e.what());
				context.sessions.erase(context.lastSessionID - 1);
				throw;
			}
		}
	}
	
	void processClose(ClientContext &context, const vector<string> &args) {
		TRACE_POINT();
		requireRights(context, Account::GET);
		context.sessions.erase(atoi(args[1]));
	}
	
	void processClear(const vector<string> &args) {
		TRACE_POINT();
		requireRights(context, Account::CLEAR);
		pool->clear();
	}
	
	void processSetMaxIdleTime(const vector<string> &args) {
		TRACE_POINT();
		requireRights(context, Account::SET_PARAMETERS);
		pool->setMaxIdleTime(atoi(args[1]));
	}
	
	void processSetMax(const vector<string> &args) {
		TRACE_POINT();
		requireRights(context, Account::PARAMETER_TWEAKING);
		pool->setMax(atoi(args[1]));
	}
	
	void processGetActive(ClientContext &context, const vector<string> &args) {
		TRACE_POINT();
		requireRights(context, Account::GET_PARAMETERS);
		context.channel.write(toString(pool->getActive()).c_str(), NULL);
	}
	
	void processGetCount(ClientContext &context, const vector<string> &args) {
		TRACE_POINT();
		requireRights(context, Account::GET_PARAMETERS);
		context.channel.write(toString(pool->getCount()).c_str(), NULL);
	}
	
	void processSetMaxPerApp(unsigned int maxPerApp) {
		TRACE_POINT();
		requireRights(context, Account::SET_PARAMETERS);
		pool->setMaxPerApp(maxPerApp);
	}
	
	void processGetSpawnServerPid(ClientContext &context, const vector<string> &args) {
		TRACE_POINT();
		requireRights(context, Account::GET_PARAMETERS);
		context.channel.write(toString(pool->getSpawnServerPid()).c_str(), NULL);
	}
	
	void processUnknownMessage(ClientContext &content, const vector<string> &args) {
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
	
	
	/*********************************************
	 * Other methods
	 *********************************************/
	
	/**
	 * Create a server socket and set it up for listening.
	 *
	 * @throws RuntimeException
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void startListening() {
		TRACE_POINT();
		int ret;
		
		serverFd = createUnixServer(socketFilename.c_str());
		do {
			ret = chmod(socketFilename.c_str(),
				S_ISVTX |
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IWGRP | S_IXGRP |
				S_IROTH | S_IWOTH | S_IXOTH);
		} while (ret == -1 && errno == EINTR);
	}
	
	AccountPtr authenticate(FileDescriptor &client) {
		// TODO
	}
	
	void requireRights(ClientContext &context, Account::Rights rights) {
		if (!context.account->hasRights(rights)) {
			throw SecurityException("TODO");
		}
	}
	
	/**
	 * The main function for a thread which handles a client.
	 */
	void clientHandlingMainLoop(FileDescriptor &client) {
		TRACE_POINT();
		vector<string> args;
		
		try {
			AccountPtr account(authenticate(client));
			if (account == NULL) {
				return;
			}
			
			ClientContext context(client, account);
			
			while (!this_thread::interruption_requested()) {
				UPDATE_TRACE_POINT();
				if (!channel.read(args)) {
					// Client closed connection.
					break;
				}
				
				P_TRACE(4, "Client " << this << ": received message: " <<
					toString(args));
				
				UPDATE_TRACE_POINT();
				if (args[0] == "get") {
					processGet(context, args);
				} else if (args[0] == "close" && args.size() == 2) {
					processClose(context, args);
				} else if (args[0] == "clear" && args.size() == 1) {
					processClear(context, args);
				} else if (args[0] == "setMaxIdleTime" && args.size() == 2) {
					processSetMaxIdleTime(context, args);
				} else if (args[0] == "setMax" && args.size() == 2) {
					processSetMax(context, args);
				} else if (args[0] == "getActive" && args.size() == 1) {
					processGetActive(context, args);
				} else if (args[0] == "getCount" && args.size() == 1) {
					processGetCount(context, args);
				} else if (args[0] == "setMaxPerApp" && args.size() == 2) {
					processSetMaxPerApp(context, atoi(args[1]));
				} else if (args[0] == "getSpawnServerPid" && args.size() == 1) {
					processGetSpawnServerPid(context, args);
				} else {
					processUnknownMessage(context, args);
					break;
				}
				args.clear();
			}
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "Client thread " << this << " interrupted.");
		} catch (const tracable_exception &e) {
			P_TRACE(2, "An error occurred in an ApplicationPoolServer client thread:\n"
				<< "   message: " << toString(args) << "\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace:\n" << e.backtrace());
		} catch (const exception &e) {
			P_TRACE(2, "An error occurred in an ApplicationPoolServer client thread:\n"
				<< "   message: " << toString(args) << "\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace: not available");
		} catch (...) {
			P_TRACE(2, "An unknown exception occurred in an ApplicationPool client thread.");
		}
	}
	
public:
	/**
	 * Creates a new ApplicationPoolServer object.
	 * The actual server main loop is not started until you call mainLoop().
	 *
	 * @param socketFilename The socket filename on which this ApplicationPoolServer
	 *                       should be listening.
	 * @param accountsDatabase An accounts database for this server, used for
	 *                         authenticating clients.
	 * @param pool The pool to expose through the server socket.
	 */
	Server(const string &socketFilename,
	       AccountsDatabasePtr accountsDatabase,
	       PoolPtr pool) {
		this->socketFilename   = socketFilename;
		this->accountsDatabase = accountsDatabase;
		this->pool = pool;
		startListening();
	}
	
	~Server() {
		this_thread::disable_syscall_interruption dsi;
		syscalls::close(serverFd);
		syscalls::unlink(socketFilename.c_str());
	}
	
	/**
	 * Starts the server main loop. This method will loop forever until some
	 * other thread interrupts the calling thread, or until an exception is raised.
	 *
	 * @throws SystemException Unable to accept a new connection. If this is a
	 *                         non-fatal error then you may call mainLoop() again
	 *                         to restart the server main loop.
	 * @throws boost::thread_resource_error Unable to create a new thread.
	 * @throws boost::thread_interrupted The calling thread has been interrupted.
	 */
	void mainLoop() {
		TRACE_POINT();
		while (true) {
			this_thread::interruption_point();
			sockaddr_un addr;
			socklen_t len = sizeof(addr);
			FileDescriptor fd;
			
			UPDATE_TRACE_POINT();
			fd = syscalls::accept(serverFd, (struct sockaddr *) &addr, &len);
			if (fd == -1) {
				throw SystemException("Unable to accept a new client", errno);
			}
			
			UPDATE_TRACE_POINT();
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			
			function<void ()> func(boost::bind(&ApplicationPoolServer::clientHandlingMainLoop,
				this, fd));
			string name = "ApplicationPoolServer client thread ";
			name.append(toString(fd));
			threadGroup.create_thread(func, name, CLIENT_THREAD_STACK_SIZE);
		}
	}
};

} // namespace ApplicationPool
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_H_ */