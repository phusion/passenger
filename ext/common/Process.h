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
#ifndef _PASSENGER_PROCESS_H_
#define _PASSENGER_PROCESS_H_

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <string>
#include <map>

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
public:
	struct SocketInfo {
		string address;
		string type;
		
		SocketInfo() {}
		
		SocketInfo(const string &address, const string &type) {
			this->address = address;
			this->type    = type;
		}
	};
	
	typedef map<string, SocketInfo> SocketInfoMap;
	
private:
	string appRoot;
	pid_t pid;
	int ownerPipe;
	string detachKey;
	string connectPassword;
	string gupid;
	SocketInfoMap serverSockets;
	SocketInfo *mainServerSocket;
	function<void ()> destructionCallback;
	
public:
	/**
	 * Construct a new Process object.
	 *
	 * @param appRoot The application root of an application.
	 *             This must be a valid directory, but the path does not have to be absolute.
	 * @param pid The process ID of this application process.
	 * @param ownerPipe The owner pipe of this application process.
	 * @param serverSockets All the server sockets that this process listens on.
	 *                      There must a server socket with the name 'main'.
	 * @param detachKey A detach key. Used by the ApplicationPool algorithm.
	 * @param connectPassword The password to use when connecting to this process.
	 *                        Must be valid ASCII.
	 * @param gupid A string which uniquely identifies this process.
	 * @param destructionCallback A callback to be called when this Process is destroyed.
	 * @throws ArgumentException If serverSockets has no socket named 'main'.
	 */
	Process(const string &appRoot, pid_t pid, int ownerPipe, const SocketInfoMap &serverSockets,
	        const string &detachKey, const string &connectPassword, const string &gupid,
	        const function<void ()> &destructionCallback = function<void ()>())
	{
		this->appRoot         = appRoot;
		this->pid             = pid;
		this->ownerPipe       = ownerPipe;
		this->serverSockets   = serverSockets;
		this->detachKey       = detachKey;
		this->connectPassword = connectPassword;
		this->gupid           = gupid;
		this->destructionCallback = destructionCallback;
		if (serverSockets.find("main") == serverSockets.end()) {
			TRACE_POINT();
			throw ArgumentException("There must be a server socket named 'main'.");
		}
		mainServerSocket = &this->serverSockets["main"];
		P_TRACE(3, "Application process " << pid << " (" << this << "): created.");
	}
	
	virtual ~Process() {
		TRACE_POINT();
		SocketInfoMap::const_iterator it;
		int ret;
		
		if (ownerPipe != -1) {
			do {
				ret = close(ownerPipe);
			} while (ret == -1 && errno == EINTR);
		}
		for (it = serverSockets.begin(); it != serverSockets.end(); it++) {
			const SocketInfo &info = it->second;
			if (info.type == "unix") {
				do {
					ret = unlink(info.address.c_str());
				} while (ret == -1 && errno == EINTR);
			}
		}
		P_TRACE(3, "Application process " << pid << " (" << this << "): destroyed.");
		
		if (destructionCallback) {
			destructionCallback();
		}
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
	 * Returns this process's detach key.
	 */
	string getDetachKey() const {
		return detachKey;
	}
	
	/**
	 * Returns this process's connect password. This password is
	 * guaranteed to be valid ASCII.
	 */
	string getConnectPassword() const {
		return connectPassword;
	}
	
	/**
	 * Returns this process's gupid. This is like a PID, but does not rotate
	 * and is even unique over multiple servers.
	 */
	string getGupid() const {
		return gupid;
	}
	
	/**
	 * Returns a map containing all server sockets that this process
	 * listens on.
	 */
	const SocketInfoMap *getServerSockets() const {
		return &serverSockets;
	}
	
	/**
	 * Request a new session from this application process by connecting to its
	 * main server socket. This session represents the life time of a single
	 * request/response pair, and can be used to send the request data to the
	 * application process, as well as receiving the response data.
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
	SessionPtr newSession(const StandardSession::CloseCallback &closeCallback = StandardSession::CloseCallback(),
	                      bool initiateNow = true)
	{
		SessionPtr session(new StandardSession(pid, closeCallback,
			mainServerSocket->type, mainServerSocket->address,
			detachKey, connectPassword, gupid));
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
