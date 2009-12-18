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
#ifndef _PASSENGER_MESSAGE_CLIENT_H_
#define _PASSENGER_MESSAGE_CLIENT_H_

#include <boost/shared_ptr.hpp>
#include <string>

#include "StaticString.h"
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Utils.h"


namespace Passenger {

using namespace std;
using namespace boost;

class MessageClient {
protected:
	FileDescriptor fd;
	MessageChannel channel;
	
	/* sendUsername() and sendPassword() exist and are virtual in order to facilitate unit testing. */
	
	virtual void sendUsername(MessageChannel &channel, const string &username) {
		channel.writeScalar(username);
	}
	
	virtual void sendPassword(MessageChannel &channel, const StaticString &userSuppliedPassword) {
		channel.writeScalar(userSuppliedPassword.c_str(), userSuppliedPassword.size());
	}
	
	/**
	 * Authenticate to the server with the given username and password.
	 *
	 * @throws SystemException An error occurred while reading data from or sending data to the server.
	 * @throws IOException An error occurred while reading data from or sending data to the server.
	 * @throws SecurityException The server denied authentication.
	 * @throws boost::thread_interrupted
	 * @pre <tt>channel</tt> is connected.
	 */
	void authenticate(const string &username, const StaticString &userSuppliedPassword) {
		vector<string> args;
		
		sendUsername(channel, username);
		sendPassword(channel, userSuppliedPassword);
		
		if (!channel.read(args)) {
			throw IOException("The ApplicationPool server did not send an authentication response.");
		} else if (args.size() != 1) {
			throw IOException("The authentication response that the ApplicationPool server sent is not valid.");
		} else if (args[0] != "ok") {
			throw SecurityException("The ApplicationPool server denied authentication: " + args[0]);
		}
	}
	
	/** Closes the connection without throwing close exceptions. */
	void silentDisconnect() {
		fd = FileDescriptor();
		channel = MessageChannel();
	}
	
public:
	/**
	 * Create a new MessageClient object. It doesn't actually connect to the server until
	 * you call connect().
	 */
	MessageClient() {
		/* The reason why we don't connect right away is because we want to make
		 * certain methods virtual for unit testing purposes. We can't call
		 * virtual methods in the constructor. :-(
		 */
	}
	
	virtual ~MessageClient() { }
	
	/**
	 * Connect to the given MessageServer. If a connection was already established,
	 * then the old connection will be closed and a new connection will be established.
	 *
	 * If this MessageClient was in a connected state, and this method throws an exception,
	 * then old connection will be broken.
	 *
	 * @param socketFilename The filename of the server socket to connect to.
	 * @param username The username to use for authenticating with the server.
	 * @param userSuppliedPassword The password to use for authenticating with the server.
	 * @return this
	 * @throws SystemException Something went wrong while connecting to the server.
	 * @throws IOException Something went wrong while connecting to the server.
	 * @throws RuntimeException Something went wrong.
	 * @throws SecurityException Unable to authenticate to the server with the given username and password.
	 *                           You may call connect() again with a different username/password.
	 * @throws boost::thread_interrupted
	 * @post connected()
	 */
	MessageClient *connect(const string &socketFilename, const string &username, const StaticString &userSuppliedPassword) {
		try {
			fd = connectToUnixServer(socketFilename.c_str());
			channel = MessageChannel(fd);
			authenticate(username, userSuppliedPassword);
			return this;
		} catch (const RuntimeException &) {
			silentDisconnect();
			throw;
		} catch (const SystemException &) {
			silentDisconnect();
			throw;
		} catch (const IOException &) {
			silentDisconnect();
			throw;
		} catch (const boost::thread_interrupted &) {
			silentDisconnect();
			throw;
		}
	}
	
	void disconnect() {
		if (fd != -1) {
			fd.close();
			fd = FileDescriptor();
			channel = MessageChannel();
		}
	}
	
	bool connected() const {
		return fd != -1;
	}
	
	/**
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	bool read(vector<string> &args) {
		try {
			return channel.read(args);
		} catch (const SystemException &e) {
			silentDisconnect();
			throw;
		}
	}
	
	/**
	 * @throws SystemException
	 * @throws SecurityException
	 * @throws TimeoutException
	 * @throws boost::thread_interrupted
	 */
	bool readScalar(string &output, unsigned int maxSize = 0, unsigned long long *timeout = NULL) {
		try {
			return channel.readScalar(output, maxSize, timeout);
		} catch (const SystemException &) {
			silentDisconnect();
			throw;
		} catch (const SecurityException &) {
			silentDisconnect();
			throw;
		} catch (const TimeoutException &) {
			silentDisconnect();
			throw;
		}
	}
	
	/**
	 * @throws SystemExeption
	 * @throws IOException
	 * @throws boost::thread_interrupted
	 */
	int readFileDescriptor(bool negotiate = true) {
		try {
			return channel.readFileDescriptor(negotiate);
		} catch (const SystemException &) {
			silentDisconnect();
			throw;
		} catch (const IOException &) {
			silentDisconnect();
			throw;
		}
	}
	
	/**
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void write(const char *name, ...) {
		va_list ap;
		va_start(ap, name);
		try {
			try {
				channel.write(name, ap);
			} catch (const SystemException &) {
				silentDisconnect();
				throw;
			}
			va_end(ap);
		} catch (...) {
			va_end(ap);
			throw;
		}
	}
	
	/**
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void writeScalar(const char *data, unsigned int size) {
		try {
			channel.writeScalar(data, size);
		} catch (const SystemException &) {
			silentDisconnect();
			throw;
		}
	}
	
	/**
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void writeScalar(const StaticString &data) {
		try {
			channel.writeScalar(data.c_str(), data.size());
		} catch (const SystemException &) {
			silentDisconnect();
			throw;
		}
	}
	
	/**
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void writeFileDescriptor(int fileDescriptor, bool negotiate = true) {
		try {
			channel.writeFileDescriptor(fileDescriptor, negotiate);
		} catch (const SystemException &) {
			silentDisconnect();
			throw;
		}
	}
};

typedef shared_ptr<MessageClient> MessageClientPtr;

} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_CLIENT_H_ */
