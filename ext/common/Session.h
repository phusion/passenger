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
#ifndef _PASSENGER_SESSION_H_
#define _PASSENGER_SESSION_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <netdb.h>
#include <cerrno>
#include <cstring>

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/system_calls.hpp>

#include "MessageChannel.h"
#include "StaticString.h"
#include "Exceptions.h"
#include "Utils/StrIntUtils.h"
#include "Utils/IOUtils.h"

namespace Passenger {

using namespace boost;
using namespace oxt;
using namespace std;


/**
 * Represents a single request/response pair of an application process.
 *
 * Session is used to forward a single HTTP request to an application
 * process, and to read back the HTTP response. A Session object is to
 * be used in the following manner:
 *
 *  -# Serialize the HTTP request headers into a format as expected by
 *     sendHeaders(), then send that string by calling sendHeaders().
 *  -# In case of a POST of PUT request, send the HTTP request body by
 *     calling sendBodyBlock(), possibly multiple times.
 *  -# Shutdown the writer end of the session channel (shutdownWriter())
 *     since you're now done sending data.
 *  -# The HTTP response can now be read through the session channel (getStream()).
 *  -# When the HTTP response has been read, the session must be closed.
 *     This is done by destroying the Session object.
 *
 * A usage example is shown in Process::newSession().
 *
 * Session is an abstract base class. Concrete implementations can be found in
 * StandardSession and ApplicationPool::Client::RemoteSession.
 *
 * Session is not guaranteed to be thread-safe.
 */
class Session {
protected:
	string detachKey;
	string connectPassword;
	string gupid;
	
public:
	/**
	 * Concrete classes might throw arbitrary exceptions.
	 */
	virtual ~Session() {}
	
	/**
	 * Initiate the session by connecting to the associated process.
	 * A Session is not usable until it's initiated.
	 *
	 * @throws SystemException Something went wrong.
	 * @throws IOException Something went wrong.
	 * @throws boost::thread_interrupted
	 */
	virtual void initiate() = 0;
	
	/**
	 * Returns whether this session has been initiated (that is, whether
	 * initiate() had been called in the past).
	 */
	virtual bool initiated() const = 0;
	
	/**
	 * Returns the type of the socket that this session is served from,
	 * e.g. "unix" indicating a Unix socket.
	 *
	 * @post !result.empty()
	 */
	virtual string getSocketType() const = 0;
	
	/**
	 * Returns the address of the socket that this session is served
	 * from. This can be a Unix socket filename or a TCP host:port string
	 * like "127.0.0.1:1234".
	 *
	 * @post !result.empty()
	 */
	virtual string getSocketName() const = 0;
	
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
	 * There must be a header with the name "PASSWORD_CONNECT_PASSWORD", and it must
	 * have the same value as the string returned by getConnectPassword().
	 *
	 * This method should be the first one to be called during the lifetime of a Session
	 * object, otherwise strange things may happen.
	 *
	 * @param headers The HTTP request headers, converted into CGI headers and encoded as
	 *                a string, according to the description.
	 * @param size The size, in bytes, of <tt>headers</tt>.
	 * @pre headers != NULL
	 * @pre initiated()
	 * @throws IOException The I/O channel has already been closed or discarded.
	 * @throws SystemException Something went wrong during writing.
	 * @throws boost::thread_interrupted
	 */
	virtual void sendHeaders(const char *headers, unsigned int size) {
		TRACE_POINT();
		int stream = getStream();
		if (stream == -1) {
			throw IOException("Cannot write headers to the request handler "
				"because the I/O stream has already been closed or discarded.");
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
	 * @param headers The headers
	 * @pre initiated()
	 * @throws IOException The I/O channel has already been closed or discarded.
	 * @throws SystemException Something went wrong during writing.
	 * @throws boost::thread_interrupted
	 */
	virtual void sendHeaders(const StaticString &headers) {
		sendHeaders(headers.c_str(), headers.size());
	}
	
	/**
	 * Send a chunk of HTTP request body data to the application.
	 * You can call this method as many times as is required to transfer
	 * the entire HTTP request body.
	 *
	 * This method must only be called after a sendHeaders(), otherwise
	 * strange things may happen.
	 *
	 * @param block A block of HTTP request body data to send.
	 * @param size The size, in bytes, of <tt>block</tt>.
	 * @pre initiated()
	 * @throws IOException The I/O channel has already been closed or discarded.
	 * @throws SystemException Something went wrong during writing.
	 * @throws boost::thread_interrupted
	 */
	virtual void sendBodyBlock(const char *block, unsigned int size) {
		TRACE_POINT();
		int stream = getStream();
		if (stream == -1) {
			throw IOException("Cannot write request body block to the "
				"request handler because the I/O channel has "
				"already been closed or discarded.");
		}
		try {
			writeExact(stream, block, size);
		} catch (SystemException &e) {
			e.setBriefMessage("An error occured while sending the "
				"request body to the request handler");
			throw;
		}
	}
	
	/**
	 * Returns this session's channel's file descriptor. This stream is
	 * full-duplex, and will be automatically closed upon Session's
	 * destruction, unless discardStream() is called.
	 *
	 * @pre initiated()
	 * @returns The file descriptor, or -1 if the I/O channel has already
	 *          been closed or discarded.
	 */
	virtual int getStream() const = 0;
	
	/**
	 * Set the timeout value for reading data from the I/O channel.
	 * If no data can be read within the timeout period, then the
	 * read call will fail with error EAGAIN or EWOULDBLOCK.
	 *
	 * @pre The I/O channel hasn't been closed or discarded.
	 * @pre initiated()
	 * @param msec The timeout, in milliseconds. If 0 is given,
	 *             there will be no timeout.
	 * @throws SystemException Cannot set the timeout.
	 */
	virtual void setReaderTimeout(unsigned int msec) = 0;
	
	/**
	 * Set the timeout value for writing data from the I/O channel.
	 * If no data can be written within the timeout period, then the
	 * write call will fail with error EAGAIN or EWOULDBLOCK.
	 *
	 * @pre The I/O channel hasn't been closed or discarded.
	 * @pre initiated()
	 * @param msec The timeout, in milliseconds. If 0 is given,
	 *             there will be no timeout.
	 * @throws SystemException Cannot set the timeout.
	 */
	virtual void setWriterTimeout(unsigned int msec) = 0;
	
	/**
	 * Indicate that we don't want to read data anymore from the I/O channel.
	 * Calling this method after closeStream()/discardStream() is called will
	 * have no effect.
	 *
	 * @pre initiated()
	 * @throws SystemException Something went wrong.
	 * @throws boost::thread_interrupted
	 */
	virtual void shutdownReader() = 0;
	
	/**
	 * Indicate that we don't want to write data anymore to the I/O channel.
	 * Calling this method after closeStream()/discardStream() is called will
	 * have no effect.
	 *
	 * @pre initiated()
	 * @throws SystemException Something went wrong.
	 * @throws boost::thread_interrupted
	 */
	virtual void shutdownWriter() = 0;
	
	/**
	 * Close the I/O stream.
	 *
	 * @throws SystemException Something went wrong.
	 * @throws boost::thread_interrupted
	 * @pre initiated()
	 * @post getStream() == -1
	 */
	virtual void closeStream() = 0;
	
	/**
	 * Discard the I/O channel's file descriptor, so that the destructor
	 * won't automatically close it.
	 *
	 * @pre initiated()
	 * @post getStream() == -1
	 */
	virtual void discardStream() = 0;
	
	/**
	 * Get the process ID of the application process that this session
	 * belongs to.
	 */
	virtual pid_t getPid() const = 0;
	
	const string getDetachKey() const {
		return detachKey;
	}
	
	/**
	 * Returns this session's process's connect password. This password is
	 * guaranteed to be valid ASCII.
	 */
	const string getConnectPassword() const {
		return connectPassword;
	}
	
	const string getGupid() const {
		return gupid;
	}
};

typedef shared_ptr<Session> SessionPtr;


/**
 * A "standard" implementation of Session.
 */
class StandardSession: public Session {
public:
	typedef function<void (const StandardSession *)> CloseCallback;
	
protected:
	pid_t pid;
	CloseCallback closeCallback;
	string socketType;
	string socketName;
	
	/** The session connection file descriptor. */
	int fd;
	bool isInitiated;
	
public:
	StandardSession(pid_t pid,
	                const CloseCallback &closeCallback,
	                const string &socketType,
	                const string &socketName,
	                const string &detachKey,
	                const string &connectPassword,
	                const string &gupid)
	{
		TRACE_POINT();
		if (socketType != "unix" && socketType != "tcp") {
			throw IOException("Unsupported socket type '" + socketType + "'");
		}
		this->pid = pid;
		this->closeCallback = closeCallback;
		this->socketType    = socketType;
		this->socketName    = socketName;
		this->detachKey     = detachKey;
		this->connectPassword = connectPassword;
		this->gupid         = gupid;
		fd = -1;
		isInitiated = false;
	}
	
	virtual ~StandardSession() {
		TRACE_POINT();
		closeStream();
		if (closeCallback != NULL) {
			closeCallback(this);
		}
	}
	
	virtual void initiate() {
		TRACE_POINT();
		if (socketType == "unix") {
			fd = connectToUnixServer(socketName.c_str());
		} else {
			vector<string> args;
			
			split(socketName, ':', args);
			if (args.size() != 2 || atoi(args[1]) == 0) {
				UPDATE_TRACE_POINT();
				throw IOException("Invalid TCP/IP address '" + socketName + "'");
			}
			fd = connectToTcpServer(args[0].c_str(), atoi(args[1]));
		}
		isInitiated = true;
	}
	
	virtual bool initiated() const {
		return isInitiated;
	}
	
	virtual string getSocketType() const {
		return socketType;
	}
	
	virtual string getSocketName() const {
		return socketName;
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
				int e = errno;
				// ENOTCONN is harmless here.
				if (e != ENOTCONN) {
					throw SystemException("Cannot shutdown the reader stream",
						e);
				}
			}
		}
	}
	
	virtual void shutdownWriter() {
		TRACE_POINT();
		if (fd != -1) {
			int ret = syscalls::shutdown(fd, SHUT_WR);
			if (ret == -1) {
				int e = errno;
				// ENOTCONN is harmless here.
				if (e != ENOTCONN) {
					throw SystemException("Cannot shutdown the writer stream",
						e);
				}
			}
		}
	}
	
	virtual void closeStream() {
		TRACE_POINT();
		if (fd != -1) {
			int ret = syscalls::close(fd);
			fd = -1;
			if (ret == -1) {
				int e = errno;
				if (errno == EIO) {
					throw SystemException(
						"A write operation on the session stream failed",
						e);
				} else {
					throw SystemException(
						"Cannot close the session stream",
						e);
				}
			}
		}
	}
	
	virtual void discardStream() {
		fd = -1;
	}
	
	virtual pid_t getPid() const {
		return pid;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_SESSION_H_ */
