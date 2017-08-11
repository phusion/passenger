/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_FILE_DESCRIPTOR_H_
#define _PASSENGER_FILE_DESCRIPTOR_H_

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/system_calls.hpp>

#include <sys/types.h>
#include <utility>
#include <unistd.h>
#include <cerrno>

#include <LoggingKit/LoggingKit.h>
#include <Exceptions.h>

namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;


#ifndef _PASSENGER_SAFELY_CLOSE_DEFINED_
	#define _PASSENGER_SAFELY_CLOSE_DEFINED_
	void safelyClose(int fd, bool ignoreErrors = false);
#endif


/**
 * Wrapper class around a file descriptor integer, for RAII behavior.
 *
 * A FileDescriptor object behaves just like an int, so that you can pass it to
 * system calls such as read(). It performs reference counting. When the last
 * copy of a FileDescriptor has been destroyed, the underlying file descriptor
 * will be automatically closed. In this case, any close() system call errors
 * are silently ignored. If you are interested in whether the close() system
 * call succeeded, then you should call FileDescriptor::close().
 *
 * This class is *not* thread-safe. It is safe to call system calls on the
 * underlying file descriptor from multiple threads, but it's not safe to
 * call FileDescriptor::close() from multiple threads if all those
 * FileDescriptor objects point to the same underlying file descriptor.
 */
class FileDescriptor {
private:
	struct SharedData {
		int fd;
		bool autoClose;

		SharedData(int fd, bool autoClose) {
			this->fd = fd;
			this->autoClose = autoClose;
		}

		~SharedData() {
			if (fd >= 0 && autoClose) {
				boost::this_thread::disable_syscall_interruption dsi;
				syscalls::close(fd);
				P_LOG_FILE_DESCRIPTOR_CLOSE(fd);
			}
		}

		void close(bool checkErrors = true) {
			if (fd >= 0) {
				boost::this_thread::disable_syscall_interruption dsi;
				int theFd = fd;
				fd = -1;
				safelyClose(theFd, !checkErrors);
				P_LOG_FILE_DESCRIPTOR_CLOSE(theFd);
			}
		}

		void detach() {
			fd = -1;
		}
	};

	/** Shared pointer for reference counting on this file descriptor */
	boost::shared_ptr<SharedData> data;

	/**
	 * Calling FileDescriptor(otherFileDescriptorObject, __FILE__, __LINE__)
	 * is always a mistake, so we declare the corresponding constructor as
	 * private in order to enforce a compiler error.
	 */
	FileDescriptor(const FileDescriptor &other, const char *file, unsigned int line, bool autoClose = true) {
		throw "never reached";
	}

public:
	/**
	 * Creates a new empty FileDescriptor instance that has no underlying
	 * file descriptor.
	 *
	 * @post *this == -1
	 */
	FileDescriptor() { }

	/**
	 * Creates a new FileDescriptor instance with the given fd as a handle.
	 *
	 * @post *this == fd
	 */
	explicit FileDescriptor(int fd, const char *file, unsigned int line, bool autoClose = true) {
		if (fd >= 0) {
			/* Make sure that the 'new' operator doesn't overwrite
			 * errno so that we can write code like this:
			 *
			 *    FileDescriptor fd = open(...);
			 *    if (fd == -1) {
			 *       print_error(errno);
			 *    }
			 */
			int e = errno;
			data = boost::make_shared<SharedData>(fd, autoClose);
			errno = e;
			if (file != NULL) {
				P_LOG_FILE_DESCRIPTOR_OPEN3(fd, file, line);
			}
		}
	}

	/**
	 * Close the underlying file descriptor. If it was already closed, then
	 * nothing will happen. If there are multiple copies of this FileDescriptor
	 * then the underlying file descriptor will be closed for every one of them.
	 *
	 * @params checkErrors Whether a SystemException should be thrown in case
	 *                     closing the file descriptor fails. If false, errors
	 *                     are silently ignored.
	 * @throws SystemException Something went wrong while closing
	 *                         the file descriptor. Only thrown if
	 *                         checkErrors is true.
	 * @post *this == -1
	 */
	void close(bool checkErrors = true) {
		if (data != NULL) {
			data->close(checkErrors);
			data.reset();
		}
	}

	/**
	 * Detach from the underlying file descriptor without closing it.
	 * This FileDescriptor and all copies will no longer affect the
	 * underlying file descriptors.
	 *
	 * @return The underlying file descriptor, or -1 if already closed.
	 * @post *this == -1
	 */
	int detach() {
		if (data != NULL) {
			int fd = data->fd;
			data->detach();
			data.reset();
			return fd;
		} else {
			return -1;
		}
	}

	/**
	 * Overloads the integer cast operator so that it will return the underlying
	 * file descriptor handle as an integer.
	 *
	 * Returns -1 if FileDescriptor::close() was called.
	 */
	operator int () const {
		if (data == NULL) {
			return -1;
		} else {
			return data->fd;
		}
	}

	void assign(int fd, const char *file, unsigned int line) {
		/* Make sure that the 'new' and 'delete' operators don't
		 * overwrite errno so that we can write code like this:
		 *
		 *    FileDescriptor fd;
		 *    fd.assign(open(...));
		 *    if (fd == -1) {
		 *       print_error(errno);
		 *    }
		 */
		int e = errno;
		if (fd >= 0) {
			data = boost::make_shared<SharedData>(fd, true);
			if (file != NULL) {
				P_LOG_FILE_DESCRIPTOR_OPEN3(fd, file, line);
			}
		} else {
			data.reset();
		}
		errno = e;
	}

	FileDescriptor &operator=(const FileDescriptor &other) {
		/* Make sure that the 'delete' operator implicitly invoked by
		 * boost::shared_ptr doesn't overwrite errno so that we can write code
		 * like this:
		 *
		 *    FileDescriptor fd;
		 *    fd = other_file_descriptor_object;
		 *    if (fd == -1) {
		 *       print_error(errno);
		 *    }
		 */
		int e = errno;
		data = other.data;
		errno = e;
		return *this;
	}
};

/**
 * A structure containing two FileDescriptor objects. Behaves like a pair
 * and like a two-element array.
 */
class FileDescriptorPair: public pair<FileDescriptor, FileDescriptor> {
public:
	FileDescriptorPair() { }

	FileDescriptorPair(const FileDescriptor &a, const FileDescriptor &b)
		: pair<FileDescriptor, FileDescriptor>(a, b)
		{ }

	FileDescriptor &operator[](int index) {
		if (index == 0) {
			return first;
		} else if (index == 1) {
			return second;
		} else {
			throw ArgumentException("Index must be either 0 of 1");
		}
	}
};

// Convenience aliases.
typedef FileDescriptorPair Pipe;
typedef FileDescriptorPair SocketPair;

/**
 * A synchronization mechanism that's implemented with file descriptors,
 * and as such can be used in combination with select() and friends.
 *
 * One can wait for an event on an EventFd by select()ing it on read events.
 * Another thread can signal the EventFd by calling notify().
 */
class EventFd {
private:
	int reader;
	int writer;

public:
	EventFd(const char *file, unsigned int line, const char *purpose) {
		int fds[2];

		if (syscalls::pipe(fds) == -1) {
			int e = errno;
			throw SystemException("Cannot create a pipe", e);
		}
		reader = fds[0];
		writer = fds[1];
		P_LOG_FILE_DESCRIPTOR_OPEN4(fds[0], file, line, purpose);
		P_LOG_FILE_DESCRIPTOR_OPEN4(fds[1], file, line, purpose);
	}

	~EventFd() {
		boost::this_thread::disable_syscall_interruption dsi;
		syscalls::close(reader);
		syscalls::close(writer);
		P_LOG_FILE_DESCRIPTOR_CLOSE(reader);
		P_LOG_FILE_DESCRIPTOR_CLOSE(writer);
	}

	void notify() {
		ssize_t ret = syscalls::write(writer, "x", 1);
		if (ret == -1 && errno != EAGAIN) {
			int e = errno;
			throw SystemException("Cannot write notification data", e);
		}
	}

	int fd() const {
		return reader;
	}

	int writerFd() const {
		return writer;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_FILE_DESCRIPTOR_H_ */
