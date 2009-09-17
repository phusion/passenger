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
#ifndef _PASSENGER_APPLICATION_POOL_CLIENT_H_
#define _PASSENGER_APPLICATION_POOL_CLIENT_H_

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>

#include "Interface.h"
#include "../Application.h"
#include "../Exceptions.h"
#include "../MessageChannel.h"
#include "../Utils.h"

namespace Passenger {
namespace ApplicationPool {

using namespace std;
using namespace oxt;
using namespace boost;


/* This source file follows the security guidelines written in Account.h. */

/**
 * Allows one to access an ApplicationPool exposed through a socket by
 * ApplicationPool::Server.
 *
 * ApplicationPool::Client connects to an ApplicationPool server, and behaves
 * just as specified by ApplicationPool::Interface. It is *not* thread-safe;
 * each thread should create a seperate ApplicationPool::Client object instead.
 *
 * A single ApplicationPool::Client should not be shared among multiple threads,
 * not even with synchronization, because it can result in deadlocks. The server
 * handles each client connection with one server thread. Consider the following
 * scenario:
 * - Clients A and B share the same ApplicationPool::Client object, with
 *   synchronization.
 * - The pool size is 1.
 * - Client A calls get() and obtains a session.
 * - Client B calls get() with a different application root, and blocks, waiting
 *   until A is done. The server thread is also blocked on the same get() command.
 * - A is done and closes the session. This sends a 'session close' command to the
 *   server. The server thread is however blocked on B's get() and cannot respond
 *   to this session close command.
 * - As a result, the system is deadlocked.
 */
class Client: public ApplicationPool::Interface {
protected:
	/**
	 * Contains data shared between RemoteSession and ApplicationPool::Client.
	 * Since RemoteSession and ApplicationPool::Client have different life times,
	 * i.e. one may be destroyed before the other, they both use a smart pointer
	 * that points to a SharedData. This way, the SharedData object is only
	 * destroyed when both the RemoteSession and the ApplicationPool::Client object
	 * have been destroyed.
	 */
	struct SharedData {
		FileDescriptor fd;
		
		/**
		 * The socket connection to the ApplicationPool server.
		 */
		MessageChannel channel;
		
		SharedData(FileDescriptor _fd): fd(_fd), channel(fd) { }
		
		~SharedData() {
			TRACE_POINT();
			disconnect();
		}
		
		bool connected() const {
			return fd != -1;
		}
		
		/**
		 * Disconnect from the ApplicationPool server.
		 */
		void disconnect() {
			TRACE_POINT();
			this_thread::disable_syscall_interruption dsi;
			fd = FileDescriptor();
		}
	};
	
	typedef shared_ptr<SharedData> SharedDataPtr;
	
	/**
	 * A communication stub for the Session object on the ApplicationPool server.
	 * This class is not guaranteed to be thread-safe.
	 */
	class RemoteSession: public Application::Session {
	private:
		SharedDataPtr data;
		int id;
		int fd;
		pid_t pid;
	public:
		RemoteSession(SharedDataPtr data, pid_t pid, int id, int fd) {
			this->data = data;
			this->pid = pid;
			this->id = id;
			this->fd = fd;
		}
		
		virtual ~RemoteSession() {
			closeStream();
			data->channel.write("close", toString(id).c_str(), NULL);
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
			if (fd != -1) {
				int ret = syscalls::shutdown(fd, SHUT_RD);
				if (ret == -1) {
					throw SystemException("Cannot shutdown the reader stream",
						errno);
				}
			}
		}
		
		virtual void shutdownWriter() {
			if (fd != -1) {
				int ret = syscalls::shutdown(fd, SHUT_WR);
				if (ret == -1) {
					throw SystemException("Cannot shutdown the writer stream",
						errno);
				}
			}
		}
		
		virtual void closeStream() {
			if (fd != -1) {
				int ret = syscalls::close(fd);
				fd = -1;
				if (ret == -1) {
					if (errno == EIO) {
						throw SystemException("A write operation on the session stream failed",
							errno);
					} else {
						throw SystemException("Cannot close the session stream",
							errno);
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
	
	/** @invariant data != NULL */
	SharedDataPtr data;
	
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
	 */
	void authenticate(const string &username, const StaticString &userSuppliedPassword) {
		MessageChannel &channel(data->channel);
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
	
	void checkConnection() const {
		if (data == NULL) {
			throw RuntimeException("connect() hasn't been called on this ApplicationPool::Client instance.");
		} else if (!data->connected()) {
			throw IOException("The connection to the ApplicationPool server is closed.");
		}
	}
	
	bool checkSecurityResponse() const {
		vector<string> args;
		
		if (data->channel.read(args)) {
			if (args[0] == "SecurityException") {
				throw SecurityException(args[1]);
			} else if (args[0] != "Passed security") {
				throw IOException("Invalid security response '" + args[0] + "'");
			} else {
				return true;
			}
		} else {
			return false;
		}
	}
	
public:
	/**
	 * Create a new ApplicationPool::Client object. It doesn't actually connect to the server until
	 * you call connect().
	 */
	Client() {
		/* The reason why we don't connect right away is because we want to make
		 * certain methods virtual for unit testing purposes. We can't call
		 * virtual methods in the constructor. :-(
		 */
	}
	
	/**
	 * Connect to the given ApplicationPool server. You may only call this method once per
	 * instance.
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
	Client *connect(const string &socketFilename, const string &username, const StaticString &userSuppliedPassword) {
		TRACE_POINT();
		FileDescriptor fd = connectToUnixServer(socketFilename.c_str());
		UPDATE_TRACE_POINT();
		data = ptr(new SharedData(fd));
		UPDATE_TRACE_POINT();
		authenticate(username, userSuppliedPassword);
		return this;
	}
	
	virtual bool connected() const {
		if (data == NULL) {
			throw RuntimeException("connect() hasn't been called on this ApplicationPool::Client instance.");
		}
		return data->connected();
	}
	
	virtual void clear() {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		try {
			channel.write("clear", NULL);
			checkSecurityResponse();
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual void setMaxIdleTime(unsigned int seconds) {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		try {
			channel.write("setMaxIdleTime", toString(seconds).c_str(), NULL);
			checkSecurityResponse();
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual void setMax(unsigned int max) {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		try {
			channel.write("setMax", toString(max).c_str(), NULL);
			checkSecurityResponse();
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual unsigned int getActive() const {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		vector<string> args;
		
		try {
			channel.write("getActive", NULL);
			checkSecurityResponse();
			channel.read(args);
			return atoi(args[0].c_str());
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual unsigned int getCount() const {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		vector<string> args;
		
		try {
			channel.write("getCount", NULL);
			checkSecurityResponse();
			channel.read(args);
			return atoi(args[0].c_str());
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual void setMaxPerApp(unsigned int max) {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		
		try {
			channel.write("setMaxPerApp", toString(max).c_str(), NULL);
			checkSecurityResponse();
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual pid_t getSpawnServerPid() const {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		vector<string> args;
		
		try {
			channel.write("getSpawnServerPid", NULL);
			checkSecurityResponse();
			channel.read(args);
			return atoi(args[0].c_str());
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual string inspect() const {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		string result;
		
		try {
			channel.write("inspect", NULL);
			checkSecurityResponse();
			// TODO: in many of these methods we do not check for EOF
			// out of laziness. Should probably be fixed in the future?
			channel.readScalar(result);
			return result;
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual string toXml(bool includeSensitiveInformation = true) const {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		string result;
		
		try {
			channel.write("toXml", includeSensitiveInformation ? "true" : "false", NULL);
			checkSecurityResponse();
			channel.readScalar(result);
			return result;
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			data->disconnect();
			throw;
		}
	}
	
	virtual Application::SessionPtr get(const PoolOptions &options) {
		TRACE_POINT();
		checkConnection();
		MessageChannel &channel(data->channel);
		vector<string> args;
		int stream;
		bool result;
		bool serverMightNeedEnvironmentVariables = true;
		
		/* Send a 'get' request to the ApplicationPool server.
		 * For efficiency reasons, we do not send the data for
		 * options.environmentVariables over the wire yet until
		 * it's necessary.
		 */
		try {
			vector<string> args;
			
			args.push_back("get");
			options.toVector(args, false);
			channel.write(args);
		} catch (const SystemException &e) {
			this_thread::disable_syscall_interruption dsi;
			UPDATE_TRACE_POINT();
			data->disconnect();
			
			string message("Could not send the 'get' command to the ApplicationPool server: ");
			message.append(e.brief());
			throw SystemException(message, e.code());
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			UPDATE_TRACE_POINT();
			data->disconnect();
			throw;
		}
		
		/* Now check the security response... */
		UPDATE_TRACE_POINT();
		try {
			checkSecurityResponse();
		} catch (const SystemException &e) {
			this_thread::disable_syscall_interruption dsi;
			UPDATE_TRACE_POINT();
			data->disconnect();
			
			string message("Could not read security response for the 'get' command from the ApplicationPool server: ");
			message.append(e.brief());
			throw SystemException(message, e.code());
		} catch (const SecurityException &) {
			// Don't disconnect.
			throw;
		} catch (...) {
			this_thread::disable_syscall_interruption dsi;
			UPDATE_TRACE_POINT();
			data->disconnect();
			throw;
		}
		
		/* After the security response, the first few replies from the server might
		 * be for requesting environment variables in the pool options object, so
		 * keep handling these requests until we receive a different reply.
		 */
		while (serverMightNeedEnvironmentVariables) {
			try {
				result = channel.read(args);
			} catch (const SystemException &e) {
				this_thread::disable_syscall_interruption dsi;
				UPDATE_TRACE_POINT();
				data->disconnect();
				throw SystemException("Could not read a response from "
					"the ApplicationPool server for the 'get' command", e.code());
			} catch (...) {
				this_thread::disable_syscall_interruption dsi;
				UPDATE_TRACE_POINT();
				data->disconnect();
				throw;
			}
			if (!result) {
				this_thread::disable_syscall_interruption dsi;
				UPDATE_TRACE_POINT();
				data->disconnect();
				throw IOException("The ApplicationPool server unexpectedly "
					"closed the connection while we're reading a response "
					"for the 'get' command.");
			}
			
			if (args[0] == "getEnvironmentVariables") {
				try {
					if (options.environmentVariables) {
						UPDATE_TRACE_POINT();
						channel.writeScalar(options.serializeEnvironmentVariables());
					} else {
						UPDATE_TRACE_POINT();
						channel.writeScalar("");
					}
				} catch (const SystemException &e) {
					this_thread::disable_syscall_interruption dsi;
					data->disconnect();
					throw SystemException("Could not send a response "
						"for the 'getEnvironmentVariables' request "
						"to the ApplicationPool server",
						e.code());
				} catch (...) {
					UPDATE_TRACE_POINT();
					this_thread::disable_syscall_interruption dsi;
					data->disconnect();
					throw;
				}
			} else {
				serverMightNeedEnvironmentVariables = false;
			}
		}
		
		/* We've now received a reply other than "getEnvironmentVariables".
		 * Handle this...
		 */
		if (args[0] == "ok") {
			UPDATE_TRACE_POINT();
			pid_t pid = (pid_t) atol(args[1]);
			int sessionID = atoi(args[2]);
			
			try {
				stream = channel.readFileDescriptor();
			} catch (...) {
				this_thread::disable_syscall_interruption dsi;
				UPDATE_TRACE_POINT();
				data->disconnect();
				throw;
			}
			
			return ptr(new RemoteSession(data, pid, sessionID, stream));
		} else if (args[0] == "SpawnException") {
			UPDATE_TRACE_POINT();
			if (args[2] == "true") {
				string errorPage;
				
				try {
					result = channel.readScalar(errorPage);
				} catch (...) {
					this_thread::disable_syscall_interruption dsi;
					data->disconnect();
					throw;
				}
				if (!result) {
					throw IOException("The ApplicationPool server "
						"unexpectedly closed the connection while "
						"we're reading the error page data.");
				} else {
					throw SpawnException(args[1], errorPage);
				}
			} else {
				throw SpawnException(args[1]);
			}
		} else if (args[0] == "BusyException") {
			UPDATE_TRACE_POINT();
			throw BusyException(args[1]);
		} else if (args[0] == "IOException") {
			this_thread::disable_syscall_interruption dsi;
			UPDATE_TRACE_POINT();
			data->disconnect();
			throw IOException(args[1]);
		} else {
			this_thread::disable_syscall_interruption dsi;
			UPDATE_TRACE_POINT();
			data->disconnect();
			throw IOException("The ApplicationPool server returned "
				"an unknown message: " + toString(args));
		}
	}
};

typedef shared_ptr<Client> ClientPtr;


} // namespace ApplicationPool
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_CLIENT_H_ */