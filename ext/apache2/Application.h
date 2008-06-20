/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_APPLICATION_H_
#define _PASSENGER_APPLICATION_H_

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <ctime>
#include <cstring>

#include "MessageChannel.h"
#include "Exceptions.h"
#include "Logging.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * Represents a single Ruby on Rails or Rack application instance.
 *
 * @ingroup Support
 */
class Application {
public:
	class Session;
	/** Convenient alias for Session smart pointer. */
	typedef shared_ptr<Session> SessionPtr;
	
	/**
	 * Represents the life time of a single request/response pair of a
	 * Ruby on Rails or Rack application.
	 *
	 * Session is used to forward a single HTTP request to a Ruby on Rails/Rack
	 * application. A Session has two communication channels: one for reading data
	 * from the application, and one for writing data to the application.
	 *
	 * In general, a session object is to be used in the following manner:
	 *
	 *  -# Convert the HTTP request headers into a string, as expected by sendHeaders().
	 *     Then send that string by calling sendHeaders().
	 *  -# In case of a POST of PUT request, send the HTTP request body by calling
	 *     sendBodyBlock(), possibly multiple times.
	 *  -# Shutdown the writer channel since you're now done sending data.
	 *  -# The HTTP response can now be read through the reader channel (getStream()).
	 *  -# When the HTTP response has been read, the session must be closed.
	 *     This is done by destroying the Session object.
	 *
	 * A usage example is shown in Application::connect(). 
	 */
	class Session {
	public:
		/**
		 * Implementing classes might throw arbitrary exceptions.
		 */
		virtual ~Session() {}
		
		/**
		 * Send HTTP request headers to the application. The HTTP headers must be
		 * converted into CGI headers, and then encoded into a string that matches this grammar:
		 *
		   @verbatim
		   headers ::= header*
		   header ::= name NUL value NUL
		   name ::= notnull+
		   value ::= notnull+
		   notnull ::= "\x01" | "\x02" | "\x02" | ... | "\xFF"
		   NUL = "\x00"
		   @endverbatim
		 *
		 * This method should be the first one to be called during the lifetime of a Session
		 * object. Otherwise strange things may happen.
		 *
		 * @param headers The HTTP request headers, converted into CGI headers and encoded as
		 *                a string, according to the description.
		 * @param size The size, in bytes, of <tt>headers</tt>.
		 * @pre headers != NULL
		 * @throws IOException The writer channel has already been closed.
		 * @throws SystemException Something went wrong during writing.
		 * @throws boost::thread_interrupted
		 */
		virtual void sendHeaders(const char *headers, unsigned int size) {
			TRACE_POINT();
			int stream = getStream();
			if (stream == -1) {
				throw IOException("Cannot write headers to the request handler "
					"because the writer stream has already been closed.");
			}
			try {
				MessageChannel(stream).writeScalar(headers, size);
			} catch (SystemException &e) {
				e.setBriefMessage("An error occured while writing headers "
					"to the request handler");
				throw;
			}
		}
		
		/**
		 * Convenience shortcut for sendHeaders(const char *, unsigned int)
		 * @param headers
		 * @throws IOException The writer channel has already been closed.
		 * @throws SystemException Something went wrong during writing.
		 * @throws boost::thread_interrupted
		 */
		virtual void sendHeaders(const string &headers) {
			sendHeaders(headers.c_str(), headers.size());
		}
		
		/**
		 * Send a chunk of HTTP request body data to the application.
		 * You can call this method as many times as is required to transfer
		 * the entire HTTP request body.
		 *
		 * This method should only be called after a sendHeaders(). Otherwise
		 * strange things may happen.
		 *
		 * @param block A block of HTTP request body data to send.
		 * @param size The size, in bytes, of <tt>block</tt>.
		 * @throws IOException The writer channel has already been closed.
		 * @throws SystemException Something went wrong during writing.
		 * @throws boost::thread_interrupted
		 */
		virtual void sendBodyBlock(const char *block, unsigned int size) {
			TRACE_POINT();
			int stream = getStream();
			if (stream == -1) {
				throw IOException("Cannot write request body block to the "
					"request handler because the writer stream has "
					"already been closed.");
			}
			try {
				MessageChannel(stream).writeRaw(block, size);
			} catch (SystemException &e) {
				e.setBriefMessage("An error occured while sending the "
					"request body to the request handler");
				throw;
			}
		}
		
		/**
		 * Get the I/O stream's file descriptor. This steam is full-duplex,
		 * and will be automatically closed upon Session's destruction,
		 * unless discardStream() is called.
		 *
		 * @pre The stream has not been fully closed.
		 */
		virtual int getStream() const = 0;
		
		/**
		 * Set the timeout value for reading data from the I/O stream.
		 * If no data can be read within the timeout period, then the
		 * read call will fail with error EAGAIN or EWOULDBLOCK.
		 *
		 * @param msec The timeout, in milliseconds. If 0 is given,
		 *             there will be no timeout.
		 * @throws SystemException Cannot set the timeout.
		 */
		virtual void setReaderTimeout(unsigned int msec) = 0;
		
		/**
		 * Set the timeout value for writing data from the I/O stream.
		 * If no data can be written within the timeout period, then the
		 * write call will fail with error EAGAIN or EWOULDBLOCK.
		 *
		 * @param msec The timeout, in milliseconds. If 0 is given,
		 *             there will be no timeout.
		 * @throws SystemException Cannot set the timeout.
		 */
		virtual void setWriterTimeout(unsigned int msec) = 0;
		
		/**
		 * Indicate that we don't want to read data anymore from the I/O stream.
		 * Calling this method after closeStream() is called will have no effect.
		 *
		 * @throws SystemException Something went wrong.
		 * @throws boost::thread_interrupted
		 */
		virtual void shutdownReader() = 0;
		
		/**
		 * Indicate that we don't want to write data anymore to the I/O stream.
		 * Calling this method after closeStream() is called will have no effect.
		 *
		 * @throws SystemException Something went wrong.
		 * @throws boost::thread_interrupted
		 */
		virtual void shutdownWriter() = 0;
		
		/**
		 * Close the I/O stream.
		 *
		 * @throws SystemException Something went wrong.
		 * @throws boost::thread_interrupted
		 */
		virtual void closeStream() = 0;
		
		/**
		 * Discard the I/O stream's file descriptor, so that Session won't automatically
		 * closed it upon Session's destruction.
		 */
		virtual void discardStream() = 0;
		
		/**
		 * Get the process ID of the application instance that belongs to this session.
		 */
		virtual pid_t getPid() const = 0;
	};

private:
	/**
	 * A "standard" implementation of Session.
	 */
	class StandardSession: public Session {
	protected:
		function<void()> closeCallback;
		int fd;
		pid_t pid;
		
	public:
		StandardSession(pid_t pid,
		                const function<void()> &closeCallback,
		                int fd) {
			this->pid = pid;
			this->closeCallback = closeCallback;
			this->fd = fd;
		}
	
		virtual ~StandardSession() {
			TRACE_POINT();
			closeStream();
			closeCallback();
		}
		
		virtual int getStream() const {
			return fd;
		}
		
		virtual void setReaderTimeout(unsigned int msec) {
			MessageChannel(fd).setReadTimeout(msec);
		}
		
		virtual void setWriterTimeout(unsigned int msec) {
			MessageChannel(fd).setWriteTimeout(msec);
		}
		
		virtual void shutdownReader() {
			TRACE_POINT();
			if (fd != -1) {
				int ret = syscalls::shutdown(fd, SHUT_RD);
				if (ret == -1) {
					throw SystemException("Cannot shutdown the writer stream",
						errno);
				}
			}
		}
		
		virtual void shutdownWriter() {
			TRACE_POINT();
			if (fd != -1) {
				int ret = syscalls::shutdown(fd, SHUT_WR);
				if (ret == -1) {
					throw SystemException("Cannot shutdown the writer stream",
						errno);
				}
			}
		}
		
		virtual void closeStream() {
			TRACE_POINT();
			if (fd != -1) {
				int ret = syscalls::close(fd);
				if (ret == -1) {
					throw SystemException("Cannot close the session stream",
						errno);
				}
				fd = -1;
			}
		}
		
		virtual void discardStream() {
			fd = -1;
		}
		
		virtual pid_t getPid() const {
			return pid;
		}
	};

	string appRoot;
	pid_t pid;
	string listenSocketName;
	bool usingAbstractNamespace;
	int ownerPipe;

public:
	/**
	 * Construct a new Application object.
	 *
	 * @param theAppRoot The application root of an application. In case of a Rails application,
	 *             this is the folder that contains 'app/', 'public/', 'config/', etc.
	 *             This must be a valid directory, but the path does not have to be absolute.
	 * @param pid The process ID of this application instance.
	 * @param listenSocketName The name of the listener socket of this application instance.
	 * @param usingAbstractNamespace Whether <tt>listenSocketName</tt> refers to a Unix
	 *        socket on the abstract namespace. Note that listenSocketName must not
	 *        contain the leading null byte, even if it's an abstract namespace socket.
	 * @param ownerPipe The owner pipe of this application instance.
	 * @post getAppRoot() == theAppRoot && getPid() == pid
	 */
	Application(const string &theAppRoot, pid_t pid, const string &listenSocketName,
	            bool usingAbstractNamespace, int ownerPipe) {
		appRoot = theAppRoot;
		this->pid = pid;
		this->listenSocketName = listenSocketName;
		this->usingAbstractNamespace = usingAbstractNamespace;
		this->ownerPipe = ownerPipe;
		P_TRACE(3, "Application " << this << ": created.");
	}
	
	virtual ~Application() {
		TRACE_POINT();
		int ret;
		
		if (ownerPipe != -1) {
			do {
				ret = close(ownerPipe);
			} while (ret == -1 && errno == EINTR);
		}
		if (!usingAbstractNamespace) {
			do {
				ret = unlink(listenSocketName.c_str());
			} while (ret == -1 && errno == EINTR);
		}
		P_TRACE(3, "Application " << this << ": destroyed.");
	}
	
	/**
	 * Returns the application root for this application. See the constructor
	 * for information about the application root.
	 */
	string getAppRoot() const {
		return appRoot;
	}
	
	/**
	 * Returns the process ID of this application instance.
	 */
	pid_t getPid() const {
		return pid;
	}
	
	/**
	 * Connect to this application instance with the purpose of sending
	 * a request to the application. Once connected, a new session will
	 * be opened. This session represents the life time of a single
	 * request/response pair, and can be used to send the request
	 * data to the application instance, as well as receiving the response
	 * data.
	 *
	 * The use of connect() is demonstrated in the following example.
	 * @code
	 *   // Connect to the application and get the newly opened session.
	 *   Application::SessionPtr session(app->connect("/home/webapps/foo"));
	 *   
	 *   // Send the request headers and request body data.
	 *   session->sendHeaders(...);
	 *   session->sendBodyBlock(...);
	 *   // Done sending data, so we close the writer channel.
	 *   session->closeWriter();
	 *
	 *   // Now read the HTTP response.
	 *   string responseData = readAllDataFromSocket(session->getReader());
	 *   // Done reading data, so we close the reader channel.
	 *   session->closeReader();
	 *
	 *   // This session has now finished, so we close the session by resetting
	 *   // the smart pointer to NULL (thereby destroying the Session object).
	 *   session.reset();
	 *
	 *   // We can connect to an Application multiple times. Just make sure
	 *   // the previous session is closed.
	 *   session = app->connect("/home/webapps/bar")
	 * @endcode
	 *
	 * Note that a RoR application instance can only process one
	 * request at the same time, and thus only one session at the same time.
	 * It's unspecified whether Rack applications can handle multiple simultanous sessions.
	 *
	 * You <b>must</b> close a session when you no longer need if. If you
	 * call connect() without having properly closed a previous session,
	 * you might cause a deadlock because the application instance may be
	 * waiting for you to close the previous session.
	 *
	 * @return A smart pointer to a Session object, which represents the created session.
	 * @param closeCallback A function which will be called when the session has been closed.
	 * @post this->getSessions() == old->getSessions() + 1
	 * @throws SystemException Something went wrong during the connection process.
	 * @throws IOException Something went wrong during the connection process.
	 */
	SessionPtr connect(const function<void()> &closeCallback) const {
		TRACE_POINT();
		int fd, ret;
		
		do {
			fd = socket(PF_UNIX, SOCK_STREAM, 0);
		} while (fd == -1 && errno == EINTR);
		if (fd == -1) {
			throw SystemException("Cannot create a new unconnected Unix socket", errno);
		}
		
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		if (usingAbstractNamespace) {
			strncpy(addr.sun_path + 1, listenSocketName.c_str(), sizeof(addr.sun_path) - 1);
			addr.sun_path[0] = '\0';
		} else {
			strncpy(addr.sun_path, listenSocketName.c_str(), sizeof(addr.sun_path));
		}
		addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
		do {
			ret = ::connect(fd, (const sockaddr *) &addr, sizeof(addr));
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			int e = errno;
			string message("Cannot connect to Unix socket '");
			message.append(listenSocketName);
			message.append("' on the abstract namespace");
			throw SystemException(message, e);
		}
		
		return ptr(new StandardSession(pid, closeCallback, fd));
	}
};

/** Convenient alias for Application smart pointer. */
typedef shared_ptr<Application> ApplicationPtr;

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_H_ */
