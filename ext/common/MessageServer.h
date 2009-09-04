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
#ifndef _PASSENGER_MESSAGE_SERVER_H_
#define _PASSENGER_MESSAGE_SERVER_H_

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/dynamic_thread_group.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cassert>

#include "Account.h"
#include "AccountsDatabase.h"
#include "FileDescriptor.h"
#include "MessageChannel.h"
#include "Logging.h"
#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/* This source file follows the security guidelines written in Account.h. */

/**
 * @ingroup Support
 */
class MessageServer {
public:
	static const unsigned int CLIENT_THREAD_STACK_SIZE = 64 * 1024;
	
	class ClientContext {
	public:
		virtual ~ClientContext() { }
	};
	
	typedef shared_ptr<ClientContext> ClientContextPtr;
	
	/**
	 * Each client handled by the server has an associated ClientContext,
	 * which stores client-specific state.
	 */
	class CommonClientContext: public ClientContext {
	public:
		/** The client's socket file descriptor. */
		FileDescriptor fd;
		
		/** The channel that's associated with the client's socket. */
		MessageChannel channel;
		
		/** The account with which the client authenticated. */
		AccountPtr account;
		
		
		CommonClientContext(FileDescriptor &theFd, AccountPtr &theAccount)
			: fd(theFd), channel(theFd), account(theAccount)
		{ }
		
		/** Returns a string representation for this client context. */
		string name() {
			return toString(channel.fileno());
		}
		
		/**
		 * Checks whether this client has all of the rights in <tt>rights</tt>. The
		 * client will be notified about the result of this check, by sending it a
		 * message.
		 *
		 * @throws SecurityException The client doesn't have one of the required rights.
		 * @throws SystemException Something went wrong while communicating with the client.
		 * @throws boost::thread_interrupted
		 */
		void requireRights(Account::Rights rights) {
			if (!account->hasRights(rights)) {
				P_TRACE(2, "Security error: insufficient rights to execute this command.");
				channel.write("SecurityException", "Insufficient rights to execute this command.", NULL);
				throw SecurityException("Insufficient rights to execute this command.");
			} else {
				channel.write("Passed security", NULL);
			}
		}
	};
	
	class Handler {
	public:
		virtual ~Handler() { }
		
		virtual ClientContextPtr newClient(CommonClientContext &context) {
			return ClientContextPtr();
		}
		
		virtual bool processMessage(CommonClientContext &commonContext,
		                            ClientContextPtr &handlerSpecificContext,
		                            const vector<string> &args) = 0;
	};
	
	typedef shared_ptr<Handler> HandlerPtr;
	
protected:
	/** The filename of the server socket on which this MessageServer is listening. */
	string socketFilename;
	
	/** An accounts database, used for authenticating clients. */
	AccountsDatabasePtr accountsDatabase;
	
	/** The registered message handlers. */
	vector<HandlerPtr> handlers;
	
	/** The maximum number of milliseconds that client may spend on logging in.
	 * Clients that take longer are disconnected.
	 *
	 * @invariant loginTimeout != 0
	 */
	unsigned long long loginTimeout;
	
	/** The client threads. */
	dynamic_thread_group threadGroup;
	
	/** The server socket's file descriptor.
	 * @invariant serverFd >= 0
	 */
	int serverFd;
	
	
	/**
	 * Create a server socket and set it up for listening. This socket will
	 * be world-writable.
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
	
	/**
	 * Authenticate the given client and returns its account information.
	 *
	 * @return A smart pointer to an Account object, or NULL if authentication failed.
	 */
	AccountPtr authenticate(FileDescriptor &client) {
		MessageChannel channel(client);
		string username, password;
		MemZeroGuard passwordGuard(password);
		unsigned long long timeout = loginTimeout;
		
		try {
			try {
				if (!channel.readScalar(username, 50, &timeout)) {
					return AccountPtr();
				}
			} catch (const SecurityException &) {
				channel.write("The supplied username is too long.", NULL);
				return AccountPtr();
			}
			
			try {
				if (!channel.readScalar(password, 100, &timeout)) {
					return AccountPtr();
				}
			} catch (const SecurityException &) {
				channel.write("The supplied password is too long.", NULL);
				return AccountPtr();
			}
			
			AccountPtr account = accountsDatabase->authenticate(username, password);
			passwordGuard.zeroNow();
			if (account == NULL) {
				channel.write("Invalid username or password.", NULL);
				return AccountPtr();
			} else {
				channel.write("ok", NULL);
				return account;
			}
		} catch (const SystemException &) {
			return AccountPtr();
		} catch (const TimeoutException &) {
			return AccountPtr();
		}
	}
	
	void broadcastNewClientEvent(CommonClientContext &context,
	                             vector<ClientContextPtr> &handlerSpecificContexts) {
		vector<HandlerPtr>::iterator it;
		
		for (it = handlers.begin(); it != handlers.end(); it++) {
			handlerSpecificContexts.push_back((*it)->newClient(context));
		}
	}
	
	bool processMessage(CommonClientContext &commonContext,
	                    vector<ClientContextPtr> &handlerSpecificContexts,
	                    const vector<string> &args) {
		vector<HandlerPtr>::iterator handler_iter;
		vector<ClientContextPtr>::iterator context_iter;
		
		for (handler_iter = handlers.begin(), context_iter = handlerSpecificContexts.begin();
		     handler_iter != handlers.end();
		     handler_iter++, context_iter++) {
			if ((*handler_iter)->processMessage(commonContext, *context_iter, args)) {
				return true;
			}
		}
		return false;
	}
	
	void processUnknownMessage(CommonClientContext &commonContext, const vector<string> &args) {
		TRACE_POINT();
		string name;
		if (args.empty()) {
			name = "(null)";
		} else {
			name = args[0];
		}
		P_TRACE(2, "A MessageServer client sent an invalid command: "
			<< name << " (" << args.size() << " elements)");
	}
	
	/**
	 * The main function for a thread which handles a client.
	 */
	void clientHandlingMainLoop(FileDescriptor &client) {
		TRACE_POINT();
		vector<string> args;
		
		P_TRACE(4, "MessageServer client thread " << (int) client << " started.");
		
		try {
			AccountPtr account(authenticate(client));
			if (account == NULL) {
				P_TRACE(4, "MessageServer client thread " << (int) client << " exited.");
				return;
			}
			
			CommonClientContext commonContext(client, account);
			vector<ClientContextPtr> handlerSpecificContexts;
			broadcastNewClientEvent(commonContext, handlerSpecificContexts);
			
			while (!this_thread::interruption_requested()) {
				UPDATE_TRACE_POINT();
				if (!commonContext.channel.read(args)) {
					// Client closed connection.
					break;
				}
				
				P_TRACE(4, "MessageServer client " << commonContext.name() <<
					": received message: " << toString(args));
				
				UPDATE_TRACE_POINT();
				if (!processMessage(commonContext, handlerSpecificContexts, args)) {
					processUnknownMessage(commonContext, args);
					break;
				}
				args.clear();
			}
			
			client.close();
			P_TRACE(4, "MessageServer client thread " << (int) client << " exited.");
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "MessageServer client thread " << (int) client << " interrupted.");
		} catch (const tracable_exception &e) {
			P_TRACE(2, "An error occurred in a MessageServer client thread " << (int) client << ":\n"
				<< "   message: " << toString(args) << "\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace:\n" << e.backtrace());
		} catch (const exception &e) {
			P_TRACE(2, "An error occurred in a MessageServer client thread " << (int) client <<":\n"
				<< "   message: " << toString(args) << "\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace: not available");
		} catch (...) {
			P_TRACE(2, "An unknown exception occurred in a MessageServer client thread.");
		}
	}
	
public:
	/**
	 * Creates a new MessageServer object.
	 * The actual server main loop is not started until you call mainLoop().
	 *
	 * @param socketFilename The socket filename on which this MessageServer
	 *                       should be listening.
	 * @param accountsDatabase An accounts database for this server, used for
	 *                         authenticating clients.
	 * @throws RuntimeException Something went wrong while setting up the server socket.
	 * @throws SystemException Something went wrong while setting up the server socket.
	 * @throws boost::thread_interrupted
	 */
	MessageServer(const string &socketFilename, AccountsDatabasePtr &accountsDatabase) {
		this->socketFilename   = socketFilename;
		this->accountsDatabase = accountsDatabase;
		loginTimeout = 2000;
		startListening();
	}
	
	~MessageServer() {
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
			
			function<void ()> func(boost::bind(&MessageServer::clientHandlingMainLoop,
				this, fd));
			string name = "MessageServer client thread ";
			name.append(toString(fd));
			threadGroup.create_thread(func, name, CLIENT_THREAD_STACK_SIZE);
		}
	}
	
	void addHandler(HandlerPtr handler) {
		handlers.push_back(handler);
	}
	
	/**
	 * Sets the maximum number of milliseconds that clients may spend on logging in.
	 * Clients that take longer are disconnected.
	 *
	 * @pre timeout != 0
	 */
	void setLoginTimeout(unsigned long long timeout) {
		assert(timeout != 0);
		loginTimeout = timeout;
	}
};

typedef shared_ptr<MessageServer> MessageServerPtr;

} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_SERVER_H_ */