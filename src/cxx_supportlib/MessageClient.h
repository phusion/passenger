/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#include <boost/bind.hpp>
#include <string>

#include <StaticString.h>
#include <Exceptions.h>
#include <Utils/MessageIO.h>
#include <Utils/IOUtils.h>
#include <Utils/ScopeGuard.h>


namespace Passenger {

using namespace std;
using namespace boost;

class MessageClient {
protected:
	FileDescriptor fd;
	bool shouldAutoDisconnect;

	/* sendUsername() and sendPassword() exist and are virtual in order to facilitate unit testing. */

	virtual void sendUsername(int fd, const StaticString &username, unsigned long long *timeout) {
		writeScalarMessage(fd, username);
	}

	virtual void sendPassword(int fd, const StaticString &userSuppliedPassword, unsigned long long *timeout) {
		writeScalarMessage(fd, userSuppliedPassword);
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
	void authenticate(const StaticString &username, const StaticString &userSuppliedPassword,
		unsigned long long *timeout)
	{
		vector<string> args;

		sendUsername(fd, username, timeout);
		sendPassword(fd, userSuppliedPassword, timeout);

		if (!readArrayMessage(fd, args, timeout)) {
			throw IOException("The message server did not send an authentication response");
		} else if (args.size() < 2 || args[0] != "status") {
			throw IOException("The authentication response that the message server sent is not valid");
		} else if (args[1] == "ok") {
			// Do nothing
		} else if (args[1] == "error") {
			if (args.size() >= 3) {
				throw SecurityException("The message server denied authentication: " + args[2]);
			} else {
				throw SecurityException("The message server denied authentication (no server message given)");
			}
		} else {
			throw IOException("The authentication response that the message server sent is not valid");
		}
	}

	void checkConnection() {
		if (!connected()) {
			throw IOException("Not connected");
		}
	}

	void autoDisconnect() {
		if (shouldAutoDisconnect) {
			fd.close(false);
		}
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
		shouldAutoDisconnect = true;
	}

	virtual ~MessageClient() { }

	/**
	 * Connect to the given MessageServer. If a connection was already established,
	 * then the old connection will be closed and a new connection will be established.
	 *
	 * If this MessageClient was in a connected state, and this method throws an exception,
	 * then old connection will be broken.
	 *
	 * @param serverAddress The address of the server to connect to, in the format
	 *                      as specified by getSocketAddressType().
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
	MessageClient *connect(const string &serverAddress, const StaticString &username,
		const StaticString &userSuppliedPassword)
	{
		TRACE_POINT();
		ScopeGuard g(boost::bind(&MessageClient::autoDisconnect, this));

		fd.assign(connectToServer(serverAddress.c_str(), __FILE__, __LINE__), NULL, 0);

		vector<string> args;
		if (!readArrayMessage(fd, args)) {
			throw IOException("The message server closed the connection before sending a version identifier.");
		}
		if (args.size() != 2 || args[0] != "version") {
			throw IOException("The message server didn't sent a valid version identifier.");
		}
		if (args[1] != "1") {
			string message = string("Unsupported message server protocol version ") +
				args[1] + ".";
			throw IOException(message);
		}

		authenticate(username, userSuppliedPassword, NULL);

		g.clear();
		return this;
	}

	void disconnect() {
		fd.close();
	}

	bool connected() const {
		return fd != -1;
	}

	void setAutoDisconnect(bool value) {
		shouldAutoDisconnect = value;
	}

	FileDescriptor getConnection() const {
		return fd;
	}

	/**
	 * @throws SystemException
	 * @throws TimeoutException
	 * @throws boost::thread_interrupted
	 */
	bool read(vector<string> &args, unsigned long long *timeout = NULL) {
		return readArray(args);
	}

	/**
	 * @throws SystemException
	 * @throws TimeoutException
	 * @throws boost::thread_interrupted
	 */
	bool readArray(vector<string> &args, unsigned long long *timeout = NULL) {
		checkConnection();
		ScopeGuard g(boost::bind(&MessageClient::autoDisconnect, this));
		bool result = readArrayMessage(fd, args, timeout);
		g.clear();
		return result;
	}

	/**
	 * @throws SystemException
	 * @throws SecurityException
	 * @throws TimeoutException
	 * @throws boost::thread_interrupted
	 */
	bool readScalar(string &output, unsigned int maxSize = 0, unsigned long long *timeout = NULL) {
		checkConnection();
		ScopeGuard g(boost::bind(&MessageClient::autoDisconnect, this));
		try {
			output = readScalarMessage(fd, maxSize, timeout);
			g.clear();
			return true;
		} catch (const EOFException &) {
			g.clear();
			return false;
		}
	}

	/**
	 * @throws SystemExeption
	 * @throws IOException
	 * @throws boost::thread_interrupted
	 */
	int readFileDescriptor(bool negotiate = true) {
		checkConnection();
		ScopeGuard g(boost::bind(&MessageClient::autoDisconnect, this));
		int result;
		if (negotiate) {
			result = Passenger::readFileDescriptorWithNegotiation(fd);
		} else {
			result = Passenger::readFileDescriptor(fd);
		}
		g.clear();
		return result;
	}

	/**
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void write(const char *name, ...) {
		checkConnection();
		va_list ap;
		va_start(ap, name);
		try {
			try {
				writeArrayMessageVA(fd, name, ap);
			} catch (const SystemException &) {
				autoDisconnect();
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
	 * @throws TimeoutException
	 * @throws boost::thread_interrupted
	 */
	void writeScalar(const char *data, unsigned int size, unsigned long long *timeout = NULL) {
		checkConnection();
		ScopeGuard g(boost::bind(&MessageClient::autoDisconnect, this));
		writeScalarMessage(fd, data, size, timeout);
		g.clear();
	}

	/**
	 * @throws SystemException
	 * @throws TimeoutException
	 * @throws boost::thread_interrupted
	 */
	void writeScalar(const StaticString &data, unsigned long long *timeout = NULL) {
		checkConnection();
		ScopeGuard g(boost::bind(&MessageClient::autoDisconnect, this));
		writeScalarMessage(fd, data, timeout);
		g.clear();
	}

	/**
	 * @throws SystemException
	 * @throws boost::thread_interrupted
	 */
	void writeFileDescriptor(int fileDescriptor, bool negotiate = true) {
		checkConnection();
		ScopeGuard g(boost::bind(&MessageClient::autoDisconnect, this));
		if (negotiate) {
			Passenger::writeFileDescriptorWithNegotiation(fd, fileDescriptor);
		} else {
			Passenger::writeFileDescriptor(fd, fileDescriptor);
		}
		g.clear();
	}
};

typedef boost::shared_ptr<MessageClient> MessageClientPtr;

} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_CLIENT_H_ */
