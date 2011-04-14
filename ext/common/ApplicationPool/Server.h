/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#include <cassert>

#include "Pool.h"
#include "../MessageServer.h"
#include "../FileDescriptor.h"
#include "../Exceptions.h"
#include "../Utils.h"

namespace Passenger {
namespace ApplicationPool {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * ApplicationPool::Server exposes an application pool to external processes through
 * a MessageServer. This allows one to use an application pool in a multi-process
 * environment. ApplicationPool::Client can be used to access a pool that's exposed
 * via ApplicationPool::Server.
 *
 * <h2>Usage</h2>
 * Construct a MessageServer and register an ApplicationPool::Server object as handler,
 * then start the MessageServer by calling mainLoop() on it.
 *
 * <h2>Concurrency model</h2>
 * Each client is handled by a seperate thread. This is necessary because the current
 * algorithm for ApplicationPool::Pool::get() can block (in the case that the spawning
 * limit has been exceeded or when global queuing is used and all application processes
 * are busy). While it is possible to get around this problem without using threads, a
 * thread-based implementation is easier to write.
 *
 * This concurrency model is implemented in MessageServer.
 *
 * <h2>Authorization support</h2>
 * The account with which the client authenticated with dictates the actions that the
 * client may invoke on the underlying application pool object. See Account::Rights.
 *
 * @ingroup Support
 */
class Server: public MessageServer::Handler {
private:
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
	 * Used as an optimization for ApplicationPool::Server::processGet():
	 * environment variables are only serialized by the client process
	 * if a new backend process is being spawned.
	 */
	class EnvironmentVariablesFetcher: public StringListCreator {
	private:
		MessageChannel &channel;
		PoolOptions &options;
		mutable StringListPtr result;
	public:
		EnvironmentVariablesFetcher(MessageChannel &theChannel, PoolOptions &theOptions)
			: channel(theChannel),
			  options(theOptions)
		{ }
		
		/**
		 * @throws ClientCommunicationError
		 */
		virtual const StringListPtr getItems() const {
			if (result) {
				return result;
			}
			
			string data;
			
			/* If an I/O error occurred while communicating with the client,
			 * then throw a ClientCommunicationException, which will bubble
			 * all the way up to the MessageServer client thread main loop,
			 * where the connection with the client will be broken.
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
				result = list.getItems();
			} else {
				result.reset(new StringList());
			}
			return result;
		}
	};
	
	struct SpecificContext: public MessageServer::ClientContext {
		/**
		 * Maps session ID to sessions created by ApplicationPool::get(). Session IDs
		 * are sent back to the ApplicationPool client. This allows the ApplicationPool
		 * client to tell us which of the multiple sessions it wants to close, later on.
		 */
		map<int, SessionPtr> sessions;
		
		/** Last used session ID. */
		int lastSessionID;
		
		SpecificContext() {
			lastSessionID = 0;
		}
	};
	
	typedef MessageServer::CommonClientContext CommonClientContext;
	
	
	/** The application pool that's being exposed through the socket. */
	ApplicationPool::Ptr pool;
	
	AnalyticsLoggerPtr analyticsLogger;
	
	
	/*********************************************
	 * Message handler methods
	 *********************************************/
	
	void processGet(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		/* Historical note:
		 *
		 * There seems to be a bug in MacOS X Leopard w.r.t. Unix server
		 * sockets file descriptors that are passed to another process.
		 * Usually Unix server sockets work fine, but when they're passed
		 * to another process, then clients that connect to the socket
		 * can incorrectly determine that the client socket is closed,
		 * even though that's not actually the case. More specifically:
		 * recv()/read() calls on these client sockets can return 0 even
		 * when we know EOF is not reached.
		 *
		 * The ApplicationPool infrastructure used to connect to a backend
		 * process's Unix socket in the helper server process, and then
		 * pass the connection file descriptor to the web server, which
		 * triggers this kernel bug. We used to work around this by using
		 * TCP sockets instead of Unix sockets; TCP sockets can still fail
		 * with this fake-EOF bug once in a while, but not nearly as often
		 * as with Unix sockets.
		 *
		 * This problem no longer applies today. The client socket is now
		 * created directly in the web server (implemented by the code below),
		 * and the bug is no longer triggered.
		 */
		
		TRACE_POINT();
		SessionPtr session;
		bool failed = false;
		
		commonContext.requireRights(Account::GET);
		
		try {
			PoolOptions options(args, 1, analyticsLogger);
			options.environmentVariables = ptr(new EnvironmentVariablesFetcher(
				commonContext.channel, options));
			options.initiateSession = false;
			session = pool->get(options);
			specificContext->sessions[specificContext->lastSessionID] = session;
			specificContext->lastSessionID++;
		} catch (const SpawnException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			
			if (e.hasErrorPage()) {
				P_TRACE(3, "Client " << commonContext.name() << ": SpawnException "
					"occured (with error page)");
				commonContext.channel.write("SpawnException", e.what(), "true", NULL);
				commonContext.channel.writeScalar(e.getErrorPage());
			} else {
				P_TRACE(3, "Client " << commonContext.name() << ": SpawnException "
					"occured (no error page)");
				commonContext.channel.write("SpawnException", e.what(), "false", NULL);
			}
			failed = true;
		} catch (const BusyException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			commonContext.channel.write("BusyException", e.what(), NULL);
			failed = true;
		} catch (const IOException &e) {
			UPDATE_TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			commonContext.channel.write("IOException", e.what(), NULL);
			failed = true;
		}
		UPDATE_TRACE_POINT();
		if (!failed) {
			this_thread::disable_syscall_interruption dsi;
			try {
				UPDATE_TRACE_POINT();
				commonContext.channel.write("ok",
					toString(session->getPid()).c_str(),
					session->getSocketType().c_str(),
					session->getSocketName().c_str(),
					session->getDetachKey().c_str(),
					session->getConnectPassword().c_str(),
					session->getGupid().c_str(),
					toString(specificContext->lastSessionID - 1).c_str(),
					NULL);
				UPDATE_TRACE_POINT();
				session->closeStream();
			} catch (const std::exception &e) {
				P_TRACE(3, "Client " << commonContext.name() << ": could not send "
					"'ok' back to the ApplicationPool client: " <<
					e.what());
				specificContext->sessions.erase(specificContext->lastSessionID - 1);
				throw;
			}
		}
	}
	
	void processClose(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		specificContext->sessions.erase(atoi(args[1]));
	}
	
	void processDetach(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::DETACH);
		if (pool->detach(args[1])) {
			commonContext.channel.write("true", NULL);
		} else {
			commonContext.channel.write("false", NULL);
		}
	}
	
	void processClear(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::CLEAR);
		pool->clear();
	}
	
	void processSetMaxIdleTime(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::SET_PARAMETERS);
		pool->setMaxIdleTime(atoi(args[1]));
	}
	
	void processSetMax(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::SET_PARAMETERS);
		pool->setMax(atoi(args[1]));
	}
	
	void processGetActive(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::GET_PARAMETERS);
		commonContext.channel.write(toString(pool->getActive()).c_str(), NULL);
	}
	
	void processGetCount(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::GET_PARAMETERS);
		commonContext.channel.write(toString(pool->getCount()).c_str(), NULL);
	}
	
	void processGetGlobalQueueSize(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::GET_PARAMETERS);
		commonContext.channel.write(toString(pool->getGlobalQueueSize()).c_str(), NULL);
	}
	
	void processSetMaxPerApp(CommonClientContext &commonContext, SpecificContext *specificContext, unsigned int maxPerApp) {
		TRACE_POINT();
		commonContext.requireRights(Account::SET_PARAMETERS);
		pool->setMaxPerApp(maxPerApp);
	}
	
	void processGetSpawnServerPid(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::GET_PARAMETERS);
		commonContext.channel.write(toString(pool->getSpawnServerPid()).c_str(), NULL);
	}
	
	void processInspect(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::INSPECT_BASIC_INFO);
		commonContext.channel.writeScalar(pool->inspect());
	}
	
	void processToXml(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::INSPECT_BASIC_INFO);
		bool includeSensitiveInfo =
			commonContext.account->hasRights(Account::INSPECT_SENSITIVE_INFO) &&
			args[1] == "true";
		commonContext.channel.writeScalar(pool->toXml(includeSensitiveInfo));
	}
	
public:
	/**
	 * Creates a new ApplicationPool::Server object.
	 *
	 * @param pool The pool to expose.
	 */
	Server(const ApplicationPool::Ptr &pool, const AnalyticsLoggerPtr &analyticsLogger = AnalyticsLoggerPtr()) {
		this->pool = pool;
		this->analyticsLogger = analyticsLogger;
	}
	
	virtual MessageServer::ClientContextPtr newClient(CommonClientContext &commonContext) {
		return ptr(new SpecificContext());
	}
	
	virtual bool processMessage(CommonClientContext &commonContext,
	                            MessageServer::ClientContextPtr &_specificContext,
	                            const vector<string> &args)
	{
		SpecificContext *specificContext = (SpecificContext *) _specificContext.get();
		try {
			if (args[0] == "get") {
				processGet(commonContext, specificContext, args);
			} else if (args[0] == "close" && args.size() == 2) {
				processClose(commonContext, specificContext, args);
			} else if (args[0] == "detach" && args.size() == 2) {
				processDetach(commonContext, specificContext, args);
			} else if (args[0] == "clear" && args.size() == 1) {
				processClear(commonContext, specificContext, args);
			} else if (args[0] == "setMaxIdleTime" && args.size() == 2) {
				processSetMaxIdleTime(commonContext, specificContext, args);
			} else if (args[0] == "setMax" && args.size() == 2) {
				processSetMax(commonContext, specificContext, args);
			} else if (args[0] == "getActive" && args.size() == 1) {
				processGetActive(commonContext, specificContext, args);
			} else if (args[0] == "getCount" && args.size() == 1) {
				processGetCount(commonContext, specificContext, args);
			} else if (args[0] == "getGlobalQueueSize" && args.size() == 1) {
				processGetGlobalQueueSize(commonContext, specificContext, args);
			} else if (args[0] == "setMaxPerApp" && args.size() == 2) {
				processSetMaxPerApp(commonContext, specificContext, atoi(args[1]));
			} else if (args[0] == "getSpawnServerPid" && args.size() == 1) {
				processGetSpawnServerPid(commonContext, specificContext, args);
			} else if (args[0] == "inspect" && args.size() == 1) {
				processInspect(commonContext, specificContext, args);
			} else if (args[0] == "toXml" && args.size() == 2) {
				processToXml(commonContext, specificContext, args);
			} else {
				return false;
			}
		} catch (const SecurityException &) {
			/* Client does not have enough rights to perform a certain action.
			 * It has already been notified of this; ignore exception and move on.
			 */
		}
		return true;
	}
};

} // namespace ApplicationPool
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_SERVER_H_ */
