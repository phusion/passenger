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
#ifndef _PASSENGER_APPLICATION_POOL_CONTROLLER_H_
#define _PASSENGER_APPLICATION_POOL_CONTROLLER_H_

#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <oxt/thread.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/system_calls.hpp>

#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <cstdio>
#include <unistd.h>
#include <errno.h>

#include "StandardApplicationPool.h"
#include "MessageChannel.h"
#include "Exceptions.h"
#include "Logging.h"
#include "Utils.h"

namespace Passenger {

using namespace boost;
using namespace oxt;
using namespace std;

/**
 * An ApplicationPoolController allows external processes to read information
 * about a StandardApplicationPool and/or to manipulate it. For example, it
 * allows command line admin tools to inspect a pool's status. It does so by
 * creating a Unix socket in the Passenger temp folder, which tools can connect
 * to to query for information and to manipulate the pool.
 *
 * An ApplicationPoolController creates a background thread for handling
 * connections on the socket. This thread will be automatically cleaned up upon
 * destroying the ApplicationPoolController object.
 *
 * <h2>Historical notes</h2>
 * This class's functionality overlaps somewhat with ApplicationPool. The two
 * should probably be merged some time in the future.
 */
class ApplicationPoolController {
private:
	/**
	 * Wrapper class around a file descriptor integer, for RAII behavior.
	 *
	 * A FileDescriptor object behaves just like an int, so that you can pass it to
	 * system calls such as read(). It performs reference counting. When the last
	 * copy of a FileDescriptor has been destroyed, the underlying file descriptor
	 * will be automatically closed.
	 */
	class FileDescriptor {
	private:
		struct SharedData {
			int fd;

			/**
			 * Constructor to assign this file descriptor's handle.
			 */
			SharedData(int fd) {
				this->fd = fd;
			}

			/**
			 * Attempts to close this file descriptor. When created on the stack,
			 * this destructor will automatically be invoked as a result of C++
			 * semantics when exiting the scope this object was created in. This
			 * ensures that stack created objects with destructors like these will
			 * de-allocate their resources upon leaving their corresponding scope.
			 * This pattern is also known Resource Acquisition Is Initialization (RAII).
			 *
			 * @throws SystemException File descriptor could not be closed.
			 */
			~SharedData() {
				this_thread::disable_syscall_interruption dsi;
				if (syscalls::close(fd) == -1) {
					throw SystemException("Cannot close file descriptor", errno);
				}
			}
		};

		/* Shared pointer for reference counting on this file descriptor */
		shared_ptr<SharedData> data;

	public:
		FileDescriptor() {
			// Do nothing.
		}

		/**
		 * Creates a new FileDescriptor instance with the given fd as a handle.
		 */
		FileDescriptor(int fd) {
			data = ptr(new SharedData(fd));
		}

		/**
		 * Overloads the integer cast operator so that it will return the file
		 * descriptor handle as an integer.
		 *
		 * @return This file descriptor's handle as an integer.
		 */
		operator int () const {
			return data->fd;
		}
	};
	
	/** The application pool to monitor. */
	StandardApplicationPoolPtr pool;
	
	/** The socket's filename. */
	char filename[PATH_MAX];
	
	/** The socket's file descriptor. */
	int serverFd;
	
	/** The main thread. */
	oxt::thread *mainThread;
	
	/** The mutex which protects the 'threads' member. */
	boost::mutex threadsLock;
	
	/** A map which maps a client file descriptor to its handling thread. */
	map< int, shared_ptr<oxt::thread> > threads;
	
	void writeScalarAndIgnoreErrors(MessageChannel &channel, const string &data) {
		try {
			channel.writeScalar(data);
		} catch (const SystemException &e) {
			// Don't care about write errors.
		}
	}
	
	void mainThreadFunction() {
		TRACE_POINT();
		try {
			while (!this_thread::interruption_requested()) {
				UPDATE_TRACE_POINT();
				sockaddr_un addr;
				socklen_t addr_len = sizeof(addr);
				
				FileDescriptor fd(syscalls::accept(serverFd, (struct sockaddr *) &addr, &addr_len));
				if (fd == -1) {
					int e = errno;
					P_ERROR("Cannot accept new client on pool controller socket: " <<
						strerror(e) << " (" << e << ")");
					break;
				}
				
				boost::lock_guard<boost::mutex> l(threadsLock);
				this_thread::disable_syscall_interruption dsi;
				this_thread::disable_interruption di;
				shared_ptr<oxt::thread> thread(new oxt::thread(
					bind(&ApplicationPoolController::clientThreadFunction, this, fd),
					"Pool controller client thread " + toString(fd),
					1024 * 128
				));
				threads[fd] = thread;
			}
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "Pool controller main thread interrupted.");
		} catch (const exception &e) {
			P_ERROR("Error in pool controller main thread: " << e.what());
		}
	}
	
	void clientThreadFunction(FileDescriptor fd) {
		TRACE_POINT();
		MessageChannel channel(fd);
		
		try {
			while (!this_thread::interruption_requested()) {
				vector<string> args;
				
				UPDATE_TRACE_POINT();
				if (!channel.read(args) || args.size() < 1) {
					break;
				}
				
				if (args[0] == "backtraces") {
					UPDATE_TRACE_POINT();
					writeScalarAndIgnoreErrors(channel, oxt::thread::all_backtraces());
				} else if (args[0] == "status") {
					UPDATE_TRACE_POINT();
					writeScalarAndIgnoreErrors(channel, pool->toString());
				} else if (args[0] == "status_xml") {
					UPDATE_TRACE_POINT();
					writeScalarAndIgnoreErrors(channel, pool->toXml());
				} else {
					P_ERROR("Error in pool controller client thread: unknown query '" <<
						args[0] << "'.");
				}
			}
		} catch (const boost::thread_interrupted &) {
			P_TRACE(2, "Pool controller client thread " << fd << " interrupted.");
		} catch (const exception &e) {
			P_ERROR("Error in pool controller client thread: " << e.what());
		}
		
		boost::lock_guard<boost::mutex> l(threadsLock);
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		threads.erase(fd);
	}

public:
	/**
	 * Creates a new ApplicationPoolController.
	 *
	 * @param pool The application pool to monitor.
	 * @param userSwitching Whether user switching is enabled. This is used
	 *                      for determining the optimal permissions for the
	 *                      FIFO file and the temp directory that might get
	 *                      created.
	 * @param permissions The permissions with which the FIFO should
	 *                    be created.
	 * @param uid The UID of the user who should own the FIFO file, or
	 *            -1 if the current user should be set as owner.
	 * @param gid The GID of the user who should own the FIFO file, or
	 *            -1 if the current group should be set as group.
	 * @throws RuntimeException An error occurred.
	 * @throws SystemException An error occurred while creating the server socket.
	 * @throws boost::thread_resource_error Something went wrong during
	 *     creation of the thread.
	 * @throws boost::thread_interrupted A system call has been interrupted.
	 */
	ApplicationPoolController(StandardApplicationPoolPtr &pool,
	                          bool userSwitching,
	                          mode_t permissions = S_IRUSR | S_IWUSR,
	                          uid_t uid = -1, gid_t gid = -1) {
		int ret;
		
		this->pool = pool;
		
		createPassengerTempDir(getSystemTempDir(), userSwitching,
			"nobody", geteuid(), getegid());
		
		snprintf(filename, sizeof(filename) - 1, "%s/master/pool_controller.socket",
			getPassengerTempDir().c_str());
		filename[PATH_MAX - 1] = '\0';
		serverFd = createUnixServer(filename, 10);
		
		/* Set the socket file's permissions... */
		do {
			ret = chmod(filename, permissions);
		} while (ret == -1 && errno == EINTR);
		
		/* ...and ownership. */
		if (uid != (uid_t) -1 && gid != (gid_t) -1) {
			do {
				ret = chown(filename, uid, gid);
			} while (ret == -1 && errno == EINTR);
			if (errno == -1) {
				int e = errno;
				char message[1024];
				
				snprintf(message, sizeof(message) - 1,
					"Cannot set the owner for socket file '%s' to %lld and its group to %lld",
					filename, (long long) uid, (long long) gid);
				message[sizeof(message) - 1] = '\0';
				do {
					ret = close(serverFd);
				} while (ret == -1 && errno == EINTR);
				throw SystemException(message, e);
			}
		}
		
		try {
			mainThread = new oxt::thread(
				bind(&ApplicationPoolController::mainThreadFunction, this),
				"Pool controller main thread",
				1024 * 128
			);
		} catch (...) {
			do {
				ret = close(serverFd);
			} while (ret == -1 && errno == EINTR);
			throw;
		}
	}
	
	~ApplicationPoolController() {
		this_thread::disable_syscall_interruption dsi;
		this_thread::disable_interruption di;
		int ret;
		
		do {
			ret = unlink(filename);
		} while (ret == -1 && errno == EINTR);
		
		mainThread->interrupt_and_join();
		delete mainThread;
		
		do {
			ret = close(serverFd);
		} while (ret == -1 && errno == EINTR);
		
		/* We make a copy of the data structure here to avoid deadlocks. */
		map< int, shared_ptr<oxt::thread> > threadsCopy;
		{
			boost::lock_guard<boost::mutex> l(threadsLock);
			threadsCopy = threads;
		}
		map< int, shared_ptr<oxt::thread> >::iterator it;
		for (it = threadsCopy.begin(); it != threadsCopy.end(); it++) {
			it->second->interrupt_and_join();
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_CONTROLLER_H_ */
