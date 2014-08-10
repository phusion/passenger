/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010, 2011, 2012 Phusion
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

#include <Account.h>
#include <AccountsDatabase.h>
#include <Constants.h>
#include <FileDescriptor.h>
#include <Logging.h>
#include <Exceptions.h>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/VariantMap.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


/* This source file follows the security guidelines written in Account.h. */

/**
 * Simple pluggable request/response messaging server framework.
 *
 * MessageServer implements a server with the following properties:
 * - It listens on a Unix socket. Socket creation and destruction is automatically handled.
 *   The socket is world-writable because a username/password authentication scheme is
 *   used to enforce security.
 * - Multithreaded: 1 thread per client.
 * - Designed for simple request/response cycles. That is, a client sends a request, and
 *   the server may respond with arbitrary data. The server does not respond sporadically,
 *   i.e. it only responds after a request.
 * - Requests are MessageIO array messages.
 * - Connections are authenticated. Connecting clients must send a username and password,
 *   which are then checked against an accounts database. The associated account is known
 *   throughout the entire connection life time so that it's possible to implement
 *   authorization features.
 *
 * MessageServer does not process messages by itself. Instead, one registers handlers
 * which handle message processing. This framework allows one to seperate message
 * handling code by function, while allowing everything to listen on the same socket and
 * to use a common request parsing and dispatching codebase.
 *
 * A username/password authentication scheme was chosen over a file permission scheme because
 * experience has shown that the latter is inadequate. For example, the web server may
 * consist of multiple worker processes, each running as a different user. Although ACLs
 * can solve this problem as well, not every platform supports ACLs by default.
 *
 * <h2>Writing handlers</h2>
 * Handlers must inherit from MessageServer::Handler. They may implement newClient()
 * and must implement processMessage().
 *
 * When a new client is accepted, MessageServer will call newClient() on all handlers.
 * This method accepts one argument: a common client context object. This context object
 * contains client-specific information, such as its file descriptor. It cannot be
 * extended to store more information, but it is passed to every handler anyway,
 * hence the word "common" in its name.
 * newClient() is supposed to return a handler-specific client context object for storing
 * its own information, or a null pointer if it doesn't need to store anything.
 *
 * When a client sends a request, MessageServer iterates through all handlers and calls
 * processMessage() on each one, passing it the common client context and the
 * handler-specific client context. processMessage() may return either true or false;
 * true indicates that the handler processed the message, false indicates that
 * it did not. Iteration stops at the first handler that returns true.
 * If all handlers return false, i.e. the client sent a message that no handler recognizes,
 * then MessageServer will close the connection with the client.
 *
 * Handlers do not need to be thread-safe as long as they only operate on data in the
 * context objects. MessageServer ensures that context objects are not shared with other
 * threads.
 *
 * <h2>Usage example</h2>
 * This implements a simple ping server. Every time a "ping" request is sent, the
 * server responds with "pong" along with the number of times it had already sent
 * pong to the same client in the past.
 *
 * @code
 *   class PingHandler: public MessageServer::Handler {
 *   public:
 *       struct MyContext: public MessageServer::ClientContext {
 *           int count;
 *
 *           MyContext() {
 *               count = 0;
 *           }
 *       };
 *
 *       MessageServer::ClientContextPtr newClient(MessageServer::CommonClientContext &commonContext) {
 *           return boost::make_shared<MyContext>();
 *       }
 *
 *       bool processMessage(MessageServer::CommonClientContext &commonContext,
 *                           MessageServer::ClientContextPtr &specificContext,
 *                           const vector<string> &args)
 *       {
 *           if (args[0] == "ping") {
 *               MyContext *myContext = (MyContext *) specificContext.get();
 *               writeArrayMessage(commonContext.fd, "pong", toString(specificContext->count).c_str(), NULL);
 *               specificContext->count++;
 *               return true;
 *           } else {
 *               return false;
 *           }
 *       }
 *   };
 *
 *   ...
 *
 *   MessageServer server("server.sock");
 *   server.addHandler(MessageServer::HandlerPtr(new PingHandler()));
 *   server.addHandler(MessageServer::HandlerPtr(new PingHandler()));
 *   server.mainLoop();
 * @endcode
 *
 * @ingroup Support
 */
class MessageServer {
public:
	static const unsigned int CLIENT_THREAD_STACK_SIZE = 1024 * 128;

	/** Interface for client context objects. */
	class ClientContext {
	public:
		virtual ~ClientContext() { }
	};

	typedef boost::shared_ptr<ClientContext> ClientContextPtr;

	/**
	 * A common client context, containing client-specific information
	 * used by MessageServer itself.
	 */
	class CommonClientContext: public ClientContext {
	public:
		/** The client's socket file descriptor. */
		FileDescriptor fd;

		/** The account with which the client authenticated. */
		AccountPtr account;


		CommonClientContext(FileDescriptor &_fd, AccountPtr &_account)
			: fd(_fd),
			  account(_account)
			{ }

		/** Returns a string representation for this client context. */
		string name() {
			return toString(fd);
		}

		/**
		 * Checks whether this client has all of the rights in `rights`. The
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
				writeArrayMessage(fd, "SecurityException", "Insufficient rights to execute this command.", NULL);
				throw SecurityException("Insufficient rights to execute this command.");
			} else {
				passSecurity();
			}
		}

		/** Announce to the client that it has passed the security checks.
		 *
		 * @throws SystemException Something went wrong while communicating with the client.
		 * @throws boost::thread_interrupted
		 */
		void passSecurity() {
			writeArrayMessage(fd, "Passed security", NULL);
		}
	};

	/**
	 * An abstract message handler class.
	 *
	 * The methods defined in this class are allowed to throw arbitrary exceptions.
	 * Such exceptions are caught and logged, after which the connection to the
	 * client is closed.
	 */
	class Handler {
	protected:
		/** Utility function for checking whether the command name equals `command`,
		 * and whether it has exactly `nargs` arguments (excluding command name).
		 */
		bool isCommand(const vector<string> &args, const string &command,
			unsigned int nargs = 0) const
		{
			return args.size() == nargs + 1 && args[0] == command;
		}

		/** Utility function for checking whether the command name equals `command`,
		 * and whether it has at least `minargs` and at most `maxargs` arguments
		 * (excluding command name), inclusive.
		 */
		bool isCommand(const vector<string> &args, const string &command,
			unsigned int minargs, unsigned int maxargs) const
		{
			return args.size() >= minargs + 1 && args.size() <= maxargs + 1 && args[0] == command;
		}

		/** Utility function for converting arguments (starting from the given index)
		 * into a VariantMap.
		 *
		 * @throws ArgumentException The number of arguments isn't an even number.
		 */
		VariantMap argsToOptions(const vector<string> &args, unsigned int startIndex = 1) const {
			VariantMap map;
			vector<string>::const_iterator it = args.begin() + startIndex, end = args.end();
			while (it != end) {
				const string &key = *it;
				it++;
				if (it == end) {
					throw ArgumentException("Invalid options");
				}
				const string &value = *it;
				map.set(key, value);
				it++;
			}
			return map;
		}

	public:
		virtual ~Handler() { }

		/**
		 * Called when a new client has connected to the MessageServer.
		 *
		 * This method is called after the client has authenticated itself.
		 *
		 * @param context Contains common client-specific information.
		 * @return A client context object for storing handler-specific client
		 *         information, or null. The default implementation returns null.
		 */
		virtual ClientContextPtr newClient(CommonClientContext &context) {
			return ClientContextPtr();
		}

		/**
		 * Called when a client has disconnected from the MessageServer. The
		 * default implementation does nothing.
		 *
		 * This method is called even if processMessage() throws an exception.
		 * It is however not called if newClient() throws an exception.
		 *
		 * @param commonContext Contains common client-specific information.
		 * @param handlerSpecificContext The client context object as was returned
		 *     earlier by newClient().
		 */
		virtual void clientDisconnected(MessageServer::CommonClientContext &context,
		                                MessageServer::ClientContextPtr &handlerSpecificContext)
		{ }

		/**
		 * Called then a client has sent a request message.
		 *
		 * This method is called after newClient() is called.
		 *
		 * @param commonContext Contains common client-specific information.
		 * @param handlerSpecificContext The client context object as was returned
		 *     earlier by newClient().
		 * @param args The request message's contents.
		 * @return Whether this handler has processed the message. Return false
		 *         if the message is unrecognized.
		 */
		virtual bool processMessage(CommonClientContext &commonContext,
		                            ClientContextPtr &handlerSpecificContext,
		                            const vector<string> &args) = 0;
	};

	typedef boost::shared_ptr<Handler> HandlerPtr;

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


	/** Calls clientDisconnected() on all handlers when destroyed. */
	struct DisconnectEventBroadcastGuard {
		vector<HandlerPtr> &handlers;
		CommonClientContext &commonContext;
		vector<ClientContextPtr> &handlerSpecificContexts;

		DisconnectEventBroadcastGuard(vector<HandlerPtr> &_handlers,
		                              CommonClientContext &_commonContext,
		                              vector<ClientContextPtr> &_handlerSpecificContexts)
		: handlers(_handlers),
		  commonContext(_commonContext),
		  handlerSpecificContexts(_handlerSpecificContexts)
		{ }

		~DisconnectEventBroadcastGuard() {
			vector<HandlerPtr>::iterator handler_iter;
			vector<ClientContextPtr>::iterator context_iter;

			for (handler_iter = handlers.begin(), context_iter = handlerSpecificContexts.begin();
			     handler_iter != handlers.end();
			     handler_iter++, context_iter++) {
				(*handler_iter)->clientDisconnected(commonContext, *context_iter);
			}
		}
	};


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
	AccountPtr authenticate(const FileDescriptor &client) {
		string username, password;
		MemZeroGuard passwordGuard(password);
		unsigned long long timeout = loginTimeout;

		try {
			writeArrayMessage(client, &timeout, "version", "1", NULL);

			try {
				if (!readScalarMessage(client, username, MESSAGE_SERVER_MAX_USERNAME_SIZE, &timeout)) {
					return AccountPtr();
				}
			} catch (const SecurityException &) {
				writeArrayMessage(client, &timeout, "The supplied username is too long.", NULL);
				return AccountPtr();
			}

			try {
				if (!readScalarMessage(client, password, MESSAGE_SERVER_MAX_PASSWORD_SIZE, &timeout)) {
					return AccountPtr();
				}
			} catch (const SecurityException &) {
				writeArrayMessage(client, &timeout, "The supplied password is too long.", NULL);
				return AccountPtr();
			}

			AccountPtr account = accountsDatabase->authenticate(username, password);
			passwordGuard.zeroNow();
			if (account == NULL) {
				writeArrayMessage(client, &timeout, "Invalid username or password.", NULL);
				return AccountPtr();
			} else {
				writeArrayMessage(client, &timeout, "ok", NULL);
				return account;
			}
		} catch (const SystemException &) {
			return AccountPtr();
		} catch (const TimeoutException &) {
			P_WARN("Login timeout");
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
	void clientHandlingMainLoop(FileDescriptor client) {
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
			DisconnectEventBroadcastGuard dguard(handlers, commonContext, handlerSpecificContexts);

			while (!this_thread::interruption_requested()) {
				UPDATE_TRACE_POINT();
				if (!readArrayMessage(commonContext.fd, args)) {
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

			P_TRACE(4, "MessageServer client thread " << (int) client << " exited.");
			client.close();
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "MessageServer client thread " << (int) client << " interrupted.");
		} catch (const tracable_exception &e) {
			P_TRACE(2, "An error occurred in a MessageServer client thread " << (int) client << ":\n"
				<< "   message: " << toString(args) << "\n"
				<< "   exception: " << e.what() << "\n"
				<< "   backtrace:\n" << e.backtrace());
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
	MessageServer(const string &socketFilename, AccountsDatabasePtr accountsDatabase) {
		this->socketFilename   = socketFilename;
		this->accountsDatabase = accountsDatabase;
		loginTimeout = 2000000;
		startListening();
	}

	~MessageServer() {
		this_thread::disable_syscall_interruption dsi;
		syscalls::close(serverFd);
		syscalls::unlink(socketFilename.c_str());
	}

	string getSocketFilename() const {
		return socketFilename;
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

			boost::function<void ()> func(boost::bind(&MessageServer::clientHandlingMainLoop,
				this, fd));
			string name = "MessageServer client thread ";
			name.append(toString(fd));
			threadGroup.create_thread(func, name, CLIENT_THREAD_STACK_SIZE);
		}
	}

	/**
	 * Registers a new handler.
	 *
	 * @pre The main loop isn't running.
	 */
	void addHandler(HandlerPtr handler) {
		handlers.push_back(handler);
	}

	/**
	 * Sets the maximum number of microseconds that clients may spend on logging in.
	 * Clients that take longer are disconnected.
	 *
	 * @pre timeout != 0
	 * @pre The main loop isn't running.
	 */
	void setLoginTimeout(unsigned long long timeout) {
		assert(timeout != 0);
		loginTimeout = timeout;
	}
};

typedef boost::shared_ptr<MessageServer> MessageServerPtr;

} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_SERVER_H_ */
