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
#include "../Utils/MessageIO.h"

namespace Passenger {
namespace ApplicationPool {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * ApplicationPool::Server exposes an application pool to external processes through
 * a MessageServer. This allows one to query application pool information and to execute
 * application pool actions in a multi-process environment.
 *
 * <h2>Usage</h2>
 * Construct a MessageServer and register an ApplicationPool::Server object as handler,
 * then start the MessageServer by calling mainLoop() on it.
 *
 * <h2>Concurrency model</h2>
 * Each client is handled by a seperate thread. This concurrency model is implemented
 * in MessageServer.
 *
 * <h2>Authorization support</h2>
 * The account with which the client authenticated with dictates the actions that the
 * client may invoke on the underlying application pool object. See Account::Rights.
 *
 * @ingroup Support
 */
class Server: public MessageServer::Handler {
private:
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
	
	void processDetach(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::DETACH);
		if (pool->detach(args[1])) {
			writeArrayMessage(commonContext.fd, "true");
		} else {
			writeArrayMessage(commonContext.fd, "false");
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
		writeArrayMessage(commonContext.fd, toString(pool->getActive()).c_str());
	}
	
	void processGetCount(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::GET_PARAMETERS);
		writeArrayMessage(commonContext.fd, toString(pool->getCount()).c_str());
	}
	
	void processGetGlobalQueueSize(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::GET_PARAMETERS);
		writeArrayMessage(commonContext.fd, toString(pool->getGlobalQueueSize()).c_str());
	}
	
	void processSetMaxPerApp(CommonClientContext &commonContext, SpecificContext *specificContext, unsigned int maxPerApp) {
		TRACE_POINT();
		commonContext.requireRights(Account::SET_PARAMETERS);
		pool->setMaxPerApp(maxPerApp);
	}
	
	void processInspect(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::INSPECT_BASIC_INFO);
		writeScalarMessage(commonContext.fd, pool->inspect());
	}
	
	void processToXml(CommonClientContext &commonContext, SpecificContext *specificContext, const vector<string> &args) {
		TRACE_POINT();
		commonContext.requireRights(Account::INSPECT_BASIC_INFO);
		bool includeSensitiveInfo =
			commonContext.account->hasRights(Account::INSPECT_SENSITIVE_INFO) &&
			args[1] == "true";
		writeScalarMessage(commonContext.fd, pool->toXml(includeSensitiveInfo));
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
			if (args[0] == "detach" && args.size() == 2) {
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
