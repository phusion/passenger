#ifndef _PASSENGER_FILE_DESCRIPTOR_H_
#define _PASSENGER_FILE_DESCRIPTOR_H_

#include <boost/shared_ptr.hpp>
#include <oxt/system_calls.hpp>

#include <unistd.h>
#include <cerrno>

#include "Exceptions.h"

namespace Passenger {

using namespace boost;
using namespace oxt;


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
		
		SharedData(int fd) {
			this->fd = fd;
		}
		
		~SharedData() {
			if (fd != -1) {
				this_thread::disable_syscall_interruption dsi;
				syscalls::close(fd);
			}
		}
		
		void close() {
			if (fd != -1) {
				this_thread::disable_syscall_interruption dsi;
				int theFd = fd;
				fd = -1;
				if (syscalls::close(theFd) == -1) {
					throw SystemException("Cannot close file descriptor", errno);
				}
			}
		}
	};

	/** Shared pointer for reference counting on this file descriptor */
	shared_ptr<SharedData> data;

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
	FileDescriptor(int fd) {
		data.reset(new SharedData(fd));
	}
	
	/**
	 * Close the underlying file descriptor. If it was already closed, then
	 * nothing will happen.
	 *
	 * @throws SystemException Something went wrong while closing
	 *                         the file descriptor.
	 * @post *this == -1
	 */
	void close() {
		if (data != NULL) {
			data->close();
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
};

} // namespace Passenger

#endif /* _PASSENGER_FILE_DESCRIPTOR_H_ */
