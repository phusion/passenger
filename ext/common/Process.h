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
#ifndef _PASSENGER_PROCESS_H_
#define _PASSENGER_PROCESS_H_

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <string>

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "Session.h"
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Logging.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * Represents a single application process, as spawned by SpawnManager
 * or by ApplicationPool::Interface::get().
 *
 * @ingroup Support
 */
class Process {
private:
	string appRoot;
	pid_t pid;
	string listenSocketName;
	string listenSocketType;
	int ownerPipe;
	
public:
	/**
	 * Construct a new Process object.
	 *
	 * @param theAppRoot The application root of an application.
	 *             This must be a valid directory, but the path does not have to be absolute.
	 * @param pid The process ID of this application process.
	 * @param listenSocketName The name of the listener socket of this application process.
	 * @param listenSocketType The type of the listener socket, e.g. "unix" for Unix
	 *                         domain sockets.
	 * @param ownerPipe The owner pipe of this application process.
	 * @post getAppRoot() == theAppRoot && getPid() == pid
	 */
	Process(const string &theAppRoot, pid_t pid, const string &listenSocketName,
	            const string &listenSocketType, int ownerPipe) {
		appRoot = theAppRoot;
		this->pid = pid;
		this->listenSocketName = listenSocketName;
		this->listenSocketType = listenSocketType;
		this->ownerPipe = ownerPipe;
		P_TRACE(3, "Application process " << this << ": created.");
	}
	
	virtual ~Process() {
		TRACE_POINT();
		int ret;
		
		if (ownerPipe != -1) {
			do {
				ret = close(ownerPipe);
			} while (ret == -1 && errno == EINTR);
		}
		if (listenSocketType == "unix") {
			do {
				ret = unlink(listenSocketName.c_str());
			} while (ret == -1 && errno == EINTR);
		}
		P_TRACE(3, "Application process " << this << ": destroyed.");
	}
	
	/**
	 * Returns the application root for this application process. See
	 * the constructor for information about the application root.
	 */
	string getAppRoot() const {
		return appRoot;
	}
	
	/**
	 * Returns the process ID of this application process.
	 */
	pid_t getPid() const {
		return pid;
	}
	
	/**
	 * Request a new session from this application process. This session
	 * represents the life time of a single request/response pair, and can
	 * be used to send the request data to the application process, as
	 * well as receiving the response data.
	 *
	 * The use of connect() is demonstrated in the following example.
	 * @code
	 *   // Request a new session from the process.
	 *   SessionPtr session = process->newSession(...);
	 *   
	 *   // Send the request headers and request body data.
	 *   session->sendHeaders(...);
	 *   session->sendBodyBlock(...);
	 *   // Done sending data, so we close the writer channel.
	 *   session->shutdownWriter();
	 *
	 *   // Now read the HTTP response.
	 *   string responseData = readAllDataFromSocket(session->getReader());
	 *   // Done reading data, so we close the reader channel.
	 *   session->shutdownReader();
	 *
	 *   // This session has now finished, so we close the session by resetting
	 *   // the smart pointer to NULL (thereby destroying the Session object).
	 *   session.reset();
	 *
	 *   // We can connect to a Process multiple times. Just make sure
	 *   // the previous session is closed.
	 *   session = process->newSession(...);
	 * @endcode
	 *
	 * You <b>must</b> close a session when you no longer need it. If you
	 * call connect() without having properly closed a previous session,
	 * you might cause a deadlock because the application process may be
	 * waiting for you to close the previous session.
	 *
	 * @param closeCallback A function which will be called when the session has been closed.
	 * @param initiateNow Whether the session should be initiated immediately.
	 *                    If set to false then you must call <tt>initiate()</tt> on
	 *                    the session before it's usable.
	 * @return A smart pointer to a Session object, which represents the created session.
	 * @post result->initiated() == initiateNow
	 * @throws SystemException Something went wrong during session initiation.
	 * @throws IOException Something went wrong during session initiation.
	 * @throws boost::thread_interrupted
	 */
	SessionPtr newSession(const function<void()> &closeCallback, bool initiateNow = true) {
		SessionPtr session(new StandardSession(pid, closeCallback, listenSocketType, listenSocketName));
		if (initiateNow) {
			session->initiate();
		}
		return session;
	}
};

/** Convenient alias for Process smart pointer. */
typedef shared_ptr<Process> ProcessPtr;

} // namespace Passenger

#endif /* _PASSENGER_PROCESS_H_ */
