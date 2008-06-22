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
#ifndef _PASSENGER_MESSAGE_CHANNEL_H_
#define _PASSENGER_MESSAGE_CHANNEL_H_

#include <oxt/system_calls.hpp>

#include <algorithm>
#include <string>
#include <list>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <cstdarg>
#ifdef __OpenBSD__
	// OpenBSD needs this for 'struct iovec'. Apparently it isn't
	// always included by unistd.h and sys/types.h.
	#include <sys/uio.h>
#endif

#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace oxt;

/**
 * Convenience class for I/O operations on file descriptors.
 *
 * This class provides convenience methods for:
 *  - sending and receiving raw data over a file descriptor.
 *  - sending and receiving messages over a file descriptor.
 *  - file descriptor passing over a Unix socket.
 * All of these methods use exceptions for error reporting.
 *
 * There are two kinds of messages:
 *  - Array messages. These are just a list of strings, and the message
 *    itself has a specific length. The contained strings may not
 *    contain NUL characters (<tt>'\\0'</tt>). Note that an array message
 *    must have at least one element.
 *  - Scalar messages. These are byte strings which may contain arbitrary
 *    binary data. Scalar messages also have a specific length.
 * The protocol is designed to be low overhead, easy to implement and
 * easy to parse.
 *
 * MessageChannel is to be wrapped around a file descriptor. For example:
 * @code
 *    int p[2];
 *    pipe(p);
 *    MessageChannel channel1(p[0]);
 *    MessageChannel channel2(p[1]);
 *    
 *    // Send an array message.
 *    channel2.write("hello", "world !!", NULL);
 *    list<string> args;
 *    channel1.read(args);    // args now contains { "hello", "world !!" }
 *
 *    // Send a scalar message.
 *    channel2.writeScalar("some long string which can contain arbitrary binary data");
 *    string str;
 *    channel1.readScalar(str);
 * @endcode
 *
 * The life time of a MessageChannel is independent from that of the
 * wrapped file descriptor. If a MessageChannel object is destroyed,
 * the file descriptor is not automatically closed. Call close()
 * if you want to close the file descriptor.
 *
 * @note I/O operations are not buffered.
 * @note Be careful with mixing the sending/receiving of array messages,
 *    scalar messages and file descriptors. If you send a collection of any
 *    of these in a specific order, then the receiving side must receive them
 *    in the exact some order. So suppose you first send a message, then a
 *    file descriptor, then a scalar, then the receiving side must first
 *    receive a message, then a file descriptor, then a scalar. If the
 *    receiving side does things in the wrong order then bad things will
 *    happen.
 * @note MessageChannel is not thread-safe, but is reentrant.
 *
 * @ingroup Support
 */
class MessageChannel {
private:
	const static char DELIMITER = '\0';
	int fd;
	
	#ifdef __OpenBSD__
		typedef u_int32_t uint32_t;
		typedef u_int16_t uint16_t;
	#endif

public:
	/**
	 * Construct a new MessageChannel with no underlying file descriptor.
	 * Thus the resulting MessageChannel object will not be usable.
	 * This constructor exists to allow one to declare an "empty"
	 * MessageChannel variable which is to be initialized later.
	 */
	MessageChannel() {
		this->fd = -1;
	}

	/**
	 * Construct a new MessageChannel with the given file descriptor.
	 */
	MessageChannel(int fd) {
		this->fd = fd;
	}
	
	/**
	 * Close the underlying file descriptor. If this method is called multiple
	 * times, the file descriptor will only be closed the first time.
	 *
	 * @throw SystemException
	 * @throw boost::thread_interrupted
	 */
	void close() {
		if (fd != -1) {
			int ret = syscalls::close(fd);
			if (ret == -1) {
				throw SystemException("Cannot close file descriptor", errno);
			}
			fd = -1;
		}
	}

	/**
	 * Send an array message, which consists of the given elements, over the underlying
	 * file descriptor.
	 *
	 * @param args The message elements.
	 * @throws SystemException An error occured while writing the data to the file descriptor.
	 * @throws boost::thread_interrupted
	 * @pre None of the message elements may contain a NUL character (<tt>'\\0'</tt>).
	 * @see read(), write(const char *, ...)
	 */
	void write(const list<string> &args) {
		list<string>::const_iterator it;
		string data;
		uint16_t dataSize = 0;

		for (it = args.begin(); it != args.end(); it++) {
			dataSize += it->size() + 1;
		}
		data.reserve(dataSize + sizeof(dataSize));
		dataSize = htons(dataSize);
		data.append((const char *) &dataSize, sizeof(dataSize));
		for (it = args.begin(); it != args.end(); it++) {
			data.append(*it);
			data.append(1, DELIMITER);
		}
		
		writeRaw(data);
	}
	
	/**
	 * Send an array message, which consists of the given strings, over the underlying
	 * file descriptor.
	 *
	 * @param name The first element of the message to send.
	 * @param ... Other elements of the message. These *must* be strings, i.e. of type char*.
	 *            It is also required to terminate this list with a NULL.
	 * @throws SystemException An error occured while writing the data to the file descriptor.
	 * @throws boost::thread_interrupted
	 * @pre None of the message elements may contain a NUL character (<tt>'\\0'</tt>).
	 * @see read(), write(const list<string> &)
	 */
	void write(const char *name, ...) {
		list<string> args;
		args.push_back(name);
		
		va_list ap;
		va_start(ap, name);
		while (true) {
			const char *arg = va_arg(ap, const char *);
			if (arg == NULL) {
				break;
			} else {
				args.push_back(arg);
			}
		}
		va_end(ap);
		write(args);
	}
	
	/**
	 * Send a scalar message over the underlying file descriptor.
	 *
	 * @param str The scalar message's content.
	 * @throws SystemException An error occured while writing the data to the file descriptor.
	 * @throws boost::thread_interrupted
	 * @see readScalar(), writeScalar(const char *, unsigned int)
	 */
	void writeScalar(const string &str) {
		writeScalar(str.c_str(), str.size());
	}
	
	/**
	 * Send a scalar message over the underlying file descriptor.
	 *
	 * @param data The scalar message's content.
	 * @param size The number of bytes in <tt>data</tt>.
	 * @pre <tt>data != NULL</tt>
	 * @throws SystemException An error occured while writing the data to the file descriptor.
	 * @throws boost::thread_interrupted
	 * @see readScalar(), writeScalar(const string &)
	 */
	void writeScalar(const char *data, unsigned int size) {
		uint32_t l = htonl(size);
		writeRaw((const char *) &l, sizeof(uint32_t));
		writeRaw(data, size);
	}
	
	/**
	 * Send a block of data over the underlying file descriptor.
	 * This method blocks until everything is sent.
	 *
	 * @param data The data to send.
	 * @param size The number of bytes in <tt>data</tt>.
	 * @pre <tt>data != NULL</tt>
	 * @throws SystemException An error occured while writing the data to the file descriptor.
	 * @throws boost::thread_interrupted
	 * @see readRaw()
	 */
	void writeRaw(const char *data, unsigned int size) {
		ssize_t ret;
		unsigned int written = 0;
		do {
			ret = syscalls::write(fd, data + written, size - written);
			if (ret == -1) {
				throw SystemException("write() failed", errno);
			} else {
				written += ret;
			}
		} while (written < size);
	}
	
	/**
	 * Send a block of data over the underlying file descriptor.
	 * This method blocks until everything is sent.
	 *
	 * @param data The data to send.
	 * @pre <tt>data != NULL</tt>
	 * @throws SystemException An error occured while writing the data to the file descriptor.
	 * @throws boost::thread_interrupted
	 */
	void writeRaw(const string &data) {
		writeRaw(data.c_str(), data.size());
	}
	
	/**
	 * Pass a file descriptor. This only works if the underlying file
	 * descriptor is a Unix socket.
	 *
	 * @param fileDescriptor The file descriptor to pass.
	 * @throws SystemException Something went wrong during file descriptor passing.
	 * @throws boost::thread_interrupted
	 * @pre <tt>fileDescriptor >= 0</tt>
	 * @see readFileDescriptor()
	 */
	void writeFileDescriptor(int fileDescriptor) {
		struct msghdr msg;
		struct iovec vec;
		char dummy[1];
		#ifdef __APPLE__
			struct {
				struct cmsghdr header;
				int fd;
			} control_data;
		#else
			char control_data[CMSG_SPACE(sizeof(int))];
		#endif
		struct cmsghdr *control_header;
		int ret;
	
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
	
		/* Linux and Solaris require msg_iov to be non-NULL. */
		dummy[0]       = '\0';
		vec.iov_base   = dummy;
		vec.iov_len    = sizeof(dummy);
		msg.msg_iov    = &vec;
		msg.msg_iovlen = 1;
	
		msg.msg_control    = (caddr_t) &control_data;
		msg.msg_controllen = sizeof(control_data);
		msg.msg_flags      = 0;
		
		control_header = CMSG_FIRSTHDR(&msg);
		control_header->cmsg_level = SOL_SOCKET;
		control_header->cmsg_type  = SCM_RIGHTS;
		#ifdef __APPLE__
			control_header->cmsg_len = sizeof(control_data);
			control_data.fd = fileDescriptor;
		#else
			control_header->cmsg_len = CMSG_LEN(sizeof(int));
			memcpy(CMSG_DATA(control_header), &fileDescriptor, sizeof(int));
		#endif
		
		ret = syscalls::sendmsg(fd, &msg, 0);
		if (ret == -1) {
			throw SystemException("Cannot send file descriptor with sendmsg()", errno);
		}
	}
	
	/**
	 * Read an array message from the underlying file descriptor.
	 *
	 * @param args The message will be put in this variable.
	 * @return Whether end-of-file has been reached. If so, then the contents
	 *         of <tt>args</tt> will be undefined.
	 * @throws SystemException If an error occured while receiving the message.
	 * @throws boost::thread_interrupted
	 * @see write()
	 */
	bool read(vector<string> &args) {
		uint16_t size;
		int ret;
		unsigned int alreadyRead = 0;
		
		do {
			ret = syscalls::read(fd, (char *) &size + alreadyRead, sizeof(size) - alreadyRead);
			if (ret == -1) {
				throw SystemException("read() failed", errno);
			} else if (ret == 0) {
				return false;
			}
			alreadyRead += ret;
		} while (alreadyRead < sizeof(size));
		size = ntohs(size);
		
		string buffer;
		args.clear();
		buffer.reserve(size);
		while (buffer.size() < size) {
			char tmp[1024 * 8];
			ret = syscalls::read(fd, tmp, min(size - buffer.size(), sizeof(tmp)));
			if (ret == -1) {
				throw SystemException("read() failed", errno);
			} else if (ret == 0) {
				return false;
			}
			buffer.append(tmp, ret);
		}
		
		if (!buffer.empty()) {
			string::size_type start = 0, pos;
			const string &const_buffer(buffer);
			while ((pos = const_buffer.find('\0', start)) != string::npos) {
				args.push_back(const_buffer.substr(start, pos - start));
				start = pos + 1;
			}
		}
		return true;
	}
	
	/**
	 * Read a scalar message from the underlying file descriptor.
	 *
	 * @param output The message will be put in here.
	 * @returns Whether end-of-file was reached during reading.
	 * @throws SystemException An error occured while writing the data to the file descriptor.
	 * @throws boost::thread_interrupted
	 * @see writeScalar()
	 */
	bool readScalar(string &output) {
		uint32_t size;
		unsigned int remaining;
		
		if (!readRaw(&size, sizeof(uint32_t))) {
			return false;
		}
		size = ntohl(size);
		
		output.clear();
		output.reserve(size);
		remaining = size;
		while (remaining > 0) {
			char buf[1024 * 32];
			unsigned int blockSize = min((unsigned int) sizeof(buf), remaining);
			
			if (!readRaw(buf, blockSize)) {
				return false;
			}
			output.append(buf, blockSize);
			remaining -= blockSize;
		}
		return true;
	}
	
	/**
	 * Read exactly <tt>size</tt> bytes of data from the underlying file descriptor,
	 * and put the result in <tt>buf</tt>. If end-of-file has been reached, or if
	 * end-of-file was encountered before <tt>size</tt> bytes have been read, then
	 * <tt>false</tt> will be returned. Otherwise (i.e. if the read was successful),
	 * <tt>true</tt> will be returned.
	 *
	 * @param buf The buffer to place the read data in. This buffer must be at least
	 *            <tt>size</tt> bytes long.
	 * @param size The number of bytes to read.
	 * @return Whether reading was successful or whether EOF was reached.
	 * @pre buf != NULL
	 * @throws SystemException Something went wrong during reading.
	 * @throws boost::thread_interrupted
	 * @see writeRaw()
	 */
	bool readRaw(void *buf, unsigned int size) {
		ssize_t ret;
		unsigned int alreadyRead = 0;
		
		while (alreadyRead < size) {
			ret = syscalls::read(fd, (char *) buf + alreadyRead, size - alreadyRead);
			if (ret == -1) {
				throw SystemException("read() failed", errno);
			} else if (ret == 0) {
				return false;
			} else {
				alreadyRead += ret;
			}
		}
		return true;
	}
	
	/**
	 * Receive a file descriptor, which had been passed over the underlying
	 * file descriptor.
	 *
	 * @return The passed file descriptor.
	 * @throws SystemException If something went wrong during the
	 *            receiving of a file descriptor. Perhaps the underlying
	 *            file descriptor isn't a Unix socket.
	 * @throws IOException Whatever was received doesn't seem to be a
	 *            file descriptor.
	 * @throws boost::thread_interrupted
	 */
	int readFileDescriptor() {
		struct msghdr msg;
		struct iovec vec;
		char dummy[1];
		#ifdef __APPLE__
			// File descriptor passing macros (CMSG_*) seem to be broken
			// on 64-bit MacOS X. This structure works around the problem.
			struct {
				struct cmsghdr header;
				int fd;
			} control_data;
			#define EXPECTED_CMSG_LEN sizeof(control_data)
		#else
			char control_data[CMSG_SPACE(sizeof(int))];
			#define EXPECTED_CMSG_LEN CMSG_LEN(sizeof(int))
		#endif
		struct cmsghdr *control_header;
		int ret;

		msg.msg_name    = NULL;
		msg.msg_namelen = 0;
		
		dummy[0]       = '\0';
		vec.iov_base   = dummy;
		vec.iov_len    = sizeof(dummy);
		msg.msg_iov    = &vec;
		msg.msg_iovlen = 1;

		msg.msg_control    = (caddr_t) &control_data;
		msg.msg_controllen = sizeof(control_data);
		msg.msg_flags      = 0;
		
		ret = syscalls::recvmsg(fd, &msg, 0);
		if (ret == -1) {
			throw SystemException("Cannot read file descriptor with recvmsg()", errno);
		}
		
		control_header = CMSG_FIRSTHDR(&msg);
		if (control_header->cmsg_len   != EXPECTED_CMSG_LEN
		 || control_header->cmsg_level != SOL_SOCKET
		 || control_header->cmsg_type  != SCM_RIGHTS) {
			throw IOException("No valid file descriptor received.");
		}
		#ifdef __APPLE__
			return control_data.fd;
		#else
			return *((int *) CMSG_DATA(control_header));
		#endif
	}
	
	/**
	 * Set the timeout value for reading data from this channel.
	 * If no data can be read within the timeout period, then a
	 * SystemException will be thrown by one of the read methods,
	 * with error code EAGAIN or EWOULDBLOCK.
	 *
	 * @param msec The timeout, in milliseconds. If 0 is given,
	 *             there will be no timeout.
	 * @throws SystemException Cannot set the timeout.
	 */
	void setReadTimeout(unsigned int msec) {
		// See the comment for setWriteTimeout().
		struct timeval tv;
		int ret;
		
		tv.tv_sec = msec / 1000;
		tv.tv_usec = msec % 1000 * 1000;
		ret = syscalls::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
			&tv, sizeof(tv));
		if (ret == -1) {
			throw SystemException("Cannot set read timeout for socket", errno);
		}
	}
	
	/**
	 * Set the timeout value for writing data to this channel.
	 * If no data can be written within the timeout period, then a
	 * SystemException will be thrown, with error code EAGAIN or
	 * EWOULDBLOCK.
	 *
	 * @param msec The timeout, in milliseconds. If 0 is given,
	 *             there will be no timeout.
	 * @throws SystemException Cannot set the timeout.
	 */
	void setWriteTimeout(unsigned int msec) {
		// People say that SO_RCVTIMEO/SO_SNDTIMEO are unreliable and
		// not well-implemented on all platforms.
		// http://www.developerweb.net/forum/archive/index.php/t-3439.html
		// That's why we use APR's timeout facilities as well (see Hooks.cpp).
		struct timeval tv;
		int ret;
		
		tv.tv_sec = msec / 1000;
		tv.tv_usec = msec % 1000 * 1000;
		ret = syscalls::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
			&tv, sizeof(tv));
		if (ret == -1) {
			throw SystemException("Cannot set read timeout for socket", errno);
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_CHANNEL_H_ */
