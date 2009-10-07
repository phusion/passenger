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
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <ctime>
#include <cstring>

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
	
	SessionPtr connectToUnixServer(const function<void()> &closeCallback) const {
		TRACE_POINT();
		int fd = Passenger::connectToUnixServer(listenSocketName.c_str());
		return ptr(new StandardSession(pid, closeCallback, fd));
	}
	
	SessionPtr connectToTcpServer(const function<void()> &closeCallback) const {
		TRACE_POINT();
		int fd, ret;
		vector<string> args;
		
		split(listenSocketName, ':', args);
		if (args.size() != 2 || atoi(args[1]) == 0) {
			UPDATE_TRACE_POINT();
			throw IOException("Invalid TCP/IP address '" + listenSocketName + "'");
		}
		
		struct addrinfo hints, *res;
		
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_INET;
		hints.ai_socktype = SOCK_STREAM;
		ret = getaddrinfo(args[0].c_str(), args[1].c_str(), &hints, &res);
		if (ret != 0) {
			UPDATE_TRACE_POINT();
			int e = errno;
			throw IOException("Cannot resolve address '" + listenSocketName +
				"': " + gai_strerror(e));
		}
		
		do {
			fd = socket(PF_INET, SOCK_STREAM, 0);
		} while (fd == -1 && errno == EINTR);
		if (fd == -1) {
			UPDATE_TRACE_POINT();
			int e = errno;
			freeaddrinfo(res);
			throw SystemException("Cannot create a new unconnected TCP socket", e);
		}
		
		do {
			ret = ::connect(fd, res->ai_addr, res->ai_addrlen);
		} while (ret == -1 && errno == EINTR);
		freeaddrinfo(res);
		if (ret == -1) {
			UPDATE_TRACE_POINT();
			int e = errno;
			string message("Cannot connect to TCP server '");
			message.append(listenSocketName);
			message.append("'");
			do {
				ret = close(fd);
			} while (ret == -1 && errno == EINTR);
			throw SystemException(message, e);
		}
		
		return ptr(new StandardSession(pid, closeCallback, fd));
	}

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
	 * Connect to this application process with the purpose of sending
	 * a request to the process. Once connected, a new session will
	 * be opened. This session represents the life time of a single
	 * request/response pair, and can be used to send the request
	 * data to the application process, as well as receiving the response
	 * data.
	 *
	 * The use of connect() is demonstrated in the following example.
	 * @code
	 *   // Connect to the process and get the newly opened session.
	 *   SessionPtr session(app->connect("/home/webapps/foo"));
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
	 *   session = app->connect("/home/webapps/bar")
	 * @endcode
	 *
	 * You <b>must</b> close a session when you no longer need it. If you
	 * call connect() without having properly closed a previous session,
	 * you might cause a deadlock because the application process may be
	 * waiting for you to close the previous session.
	 *
	 * @return A smart pointer to a Session object, which represents the created session.
	 * @param closeCallback A function which will be called when the session has been closed.
	 * @post this->getSessions() == old->getSessions() + 1
	 * @throws SystemException Something went wrong during the connection process.
	 * @throws IOException Something went wrong during the connection process.
	 * @throws boost::thread_interrupted
	 */
	SessionPtr connect(const function<void()> &closeCallback) const {
		TRACE_POINT();
		if (listenSocketType == "unix") {
			return connectToUnixServer(closeCallback);
		} else if (listenSocketType == "tcp") {
			return connectToTcpServer(closeCallback);
		} else {
			throw IOException("Unsupported socket type '" + listenSocketType + "'");
		}
	}
};

/** Convenient alias for Process smart pointer. */
typedef shared_ptr<Process> ProcessPtr;

} // namespace Passenger

#endif /* _PASSENGER_PROCESS_H_ */
