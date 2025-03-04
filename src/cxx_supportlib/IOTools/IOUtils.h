/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#ifndef _PASSENGER_IOTOOLS_IO_UTILS_H_
#define _PASSENGER_IOTOOLS_IO_UTILS_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <cstdio>
#include <cstddef>
#include <unistd.h>
#include <netdb.h>
#include <string>
#include <utility>
#include <oxt/macros.hpp>
#include <StaticString.h>
#include <FileDescriptor.h>

namespace Passenger {

using namespace std;

enum ServerAddressType {
	SAT_UNIX,
	SAT_TCP,
	SAT_UNKNOWN
};

typedef ssize_t (*WritevFunction)(int fildes, const struct iovec *iov, int iovcnt);

bool purgeStdio(FILE *f);

/**
 * Accepts a server address in one of the following formats, and returns which one it is:
 *
 * Unix domain sockets
 *    Format: "unix:/path/to/a/socket"
 *    Returns: SAT_UNIX
 *
 * TCP sockets
 *    Format: "tcp://host:port"
 *    Returns: SAT_TCP
 *
 * Other
 *    Returns: SAT_UNKNOWN
 */
ServerAddressType getSocketAddressType(const StaticString &address);

/**
 * Parses a Unix domain socket address, as accepted by getSocketAddressType(),
 * and returns the socket filename.
 *
 * @throw ArgumentException <tt>address</tt> is not a valid Unix domain socket address.
 */
string parseUnixSocketAddress(const StaticString &address);

/**
 * Parses a TCP socket address, as accepted by getSocketAddressType(),
 * and returns the host and port.
 *
 * @throw ArgumentException <tt>address</tt> is not a valid TCP socket address.
 */
void parseTcpSocketAddress(const StaticString & restrict_ref address,
	string & restrict_ref host,
	unsigned short & restrict_ref port);

/**
 * Returns whether the given socket address (as accepted by getSocketAddressType())
 * is an address that can only refer to a server on the local system.
 *
 * @throw ArgumentException <tt>address</tt> is not a valid TCP socket address.
 */
bool isLocalSocketAddress(const StaticString &address);

/**
 * Sets a socket in blocking mode.
 *
 * @throws SystemException Something went wrong.
 * @ingroup Support
 */
void setBlocking(int fd);

/**
 * Sets a socket in non-blocking mode.
 *
 * @throws SystemException Something went wrong.
 * @ingroup Support
 */
void setNonBlocking(int fd);

/**
 * Try to call the Linux accept4() system call. If the system call is
 * not available, then -1 is returned and errno is set to ENOSYS.
 */
int callAccept4(int sock,
	struct sockaddr * restrict addr,
	socklen_t * restrict addr_len,
	int options);

/**
 * Create a new Unix or TCP server socket, depending on the address type.
 *
 * @param address An address as defined by getSocketAddressType().
 * @param backlogSize The size of the socket's backlog. Specify 0 to use
 *                    the paltform's maximum allowed backlog size.
 * @param autoDelete If <tt>address</tt> is a Unix socket that already exists,
 *                   whether that should be deleted. Otherwise this argument
 *                   is ignored.
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @return The file descriptor of the newly created server socket.
 * @throws ArgumentException The given address cannot be parsed.
 * @throws RuntimeException Something went wrong.
 * @throws SystemException Something went wrong while creating the Unix server socket.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
int createServer(const StaticString &address,
	unsigned int backlogSize = 0,
	bool autoDelete = true,
	const char *file = __FILE__,
	unsigned int line = __LINE__);

/**
 * Create a new Unix server socket which is bounded to <tt>filename</tt>.
 *
 * @param filename The filename to bind the socket to.
 * @param backlogSize The size of the socket's backlog. Specify 0 to use the
 *                    platform's maximum allowed backlog size.
 * @param autoDelete Whether <tt>filename</tt> should be deleted, if it already exists.
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @return The file descriptor of the newly created Unix server socket.
 * @throws RuntimeException Something went wrong.
 * @throws SystemException Something went wrong while creating the Unix server socket.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
int createUnixServer(const StaticString &filename,
	unsigned int backlogSize = 0,
	bool autoDelete = true,
	const char *file = __FILE__,
	unsigned int line = __LINE__);

/**
 * Create a new TCP server socket which is bounded to the given address and port.
 * SO_REUSEADDR will be set on the socket.
 *
 * @param address The IP address to bind the socket to.
 * @param port The port to bind the socket to, or 0 to have the OS automatically
 *             select a free port.
 * @param backlogSize The size of the socket's backlog. Specify 0 to use the
 *                    platform's maximum allowed backlog size.
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @return The file descriptor of the newly created server socket.
 * @throws SystemException Something went wrong while creating the server socket.
 * @throws ArgumentException The given address cannot be parsed.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
int createTcpServer(const char *address = "0.0.0.0",
	unsigned short port = 0,
	unsigned int backlogSize = 0,
	const char *file = __FILE__,
	unsigned int line = __LINE__);

/**
 * Connect to a server at the given address in a blocking manner.
 *
 * @param address An address as accepted by getSocketAddressType().
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @return The file descriptor of the connected client socket.
 * @throws ArgumentException Unknown address type.
 * @throws RuntimeException Something went wrong.
 * @throws SystemException Something went wrong while connecting to the server.
 * @throws IOException Something went wrong while connecting to the server.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
int connectToServer(const StaticString &address, const char *file,
	unsigned int line);

/**
 * Connect to a Unix server socket at <tt>filename</tt> in a blocking manner.
 *
 * @param filename The filename of the socket to connect to.
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @return The file descriptor of the connected client socket.
 * @throws RuntimeException Something went wrong.
 * @throws SystemException Something went wrong while connecting to the Unix server.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
int connectToUnixServer(const StaticString &filename, const char *file,
	unsigned int line);

/**
 * Connect to a TCP server socket at the given host name and port in a blocking manner.
 *
 * @param hostname The host name of the TCP server.
 * @param port The port number of the TCP server.
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @return The file descriptor of the connected client socket.
 * @throws IOException Something went wrong while connecting to the Unix server.
 * @throws SystemException Something went wrong while connecting to the server.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
int connectToTcpServer(const StaticString &hostname, unsigned int port,
	const char *file, unsigned int line);


/** State structure for non-blocking connectToUnixServer(). */
struct NUnix_State {
	FileDescriptor fd;
	string filename;
};

/** State structure for non-blocking connectToTcpServer(). */
struct NTCP_State {
	FileDescriptor fd;
	struct addrinfo hints, *res;
	string hostname;
	int port;

	NTCP_State() {
		memset(&hints, 0, sizeof(hints));
		res = NULL;
		port = 0;
	}

	~NTCP_State() {
		if (res != NULL) {
			freeaddrinfo(res);
		}
	}
};

/**
 * Setup a Unix domain socket for non-blocking connecting. When done,
 * the file descriptor can be accessed through <tt>state.fd</tt>.
 *
 * @param state A state structure.
 * @param filename The filename of the socket to connect to.
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @throws SystemException Something went wrong.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
void setupNonBlockingUnixSocket(NUnix_State & restrict_ref state,
	const StaticString & restrict_ref filename, const char *file,
	unsigned int line);

/**
 * Connect a Unix domain socket in non-blocking mode.
 *
 * @param state A state structure.
 * @return True if the socket was successfully connected, false if the socket isn't
 *         ready yet, in which case the caller should select() on the socket until it's writable.
 * @throws RuntimeException Something went wrong.
 * @throws SystemException Something went wrong while connecting to the Unix server.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
bool connectToUnixServer(NUnix_State &state);

/**
 * Setup a TCP socket for non-blocking connecting. When done,
 * the file descriptor can be accessed through <tt>state.fd</tt>.
 *
 * @param state A state structure.
 * @param hostname The host name of the TCP server.
 * @param port The port number of the TCP server.
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @throws IOException Something went wrong.
 * @throws SystemException Something went wrong.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
void setupNonBlockingTcpSocket(NTCP_State & restrict_ref state,
	const StaticString & restrict_ref hostname,
	int port, const char *file, unsigned int line);

/**
 * Connect a TCP socket in non-blocking mode.
 *
 * @param state A state structure.
 * @return True if the socket was successfully connected, false if the socket isn't
 *         ready yet, in which case the caller should select() on the socket until it's writable.
 * @throws SystemException Something went wrong while connecting to the server.
 * @throws boost::thread_interrupted A system call has been interrupted.
 * @ingroup Support
 */
bool connectToTcpServer(NTCP_State &state);

struct NConnect_State {
private:
	NUnix_State s_unix;
	NTCP_State s_tcp;

public:
	ServerAddressType type;

	/**
	 * Setup a socket for non-blocking connecting to the given address.
	 *
	 * @param address An address as accepted by getSocketAddressType().
	 * @param file The name of the source file that called this function,
	 *             for file descriptor logging purposes.
	 * @param line The line in the source file that called this function.
	 * @throws ArgumentException Unknown address type.
	 * @throws RuntimeException Something went wrong.
	 * @throws SystemException Something went wrong.
	 * @throws IOException Something went wrong.
	 * @throws boost::thread_interrupted A system call has been interrupted.
	 */
	NConnect_State(const StaticString & restrict_ref address, const char *file, unsigned int line);

	NUnix_State &asNUnix_State();
	NTCP_State &asNTCP_State();

	/**
	 * Connect a socket in non-blocking mode.
	 *
	 * @return True if the socket was successfully connected, false if the socket isn't
	 *         ready yet, in which case the caller should select() on the socket until it's writable.
	 * @throws RuntimeException Something went wrong.
	 * @throws SystemException Something went wrong.
	 * @throws boost::thread_interrupted A system call has been interrupted.
	 */
	bool connectToServer();

	/**
	 * Gets the connection file descriptor.
	 *
	 * @pre \c connectToServer() was called
	 */
	FileDescriptor &getFd();
};

/**
 * Checks whether the given TCP server is connectable. Because this check
 * can take (in theory) an arbitrary amount of time, you must also supply
 * a timeout. When the operation is done, the amount of time taken will be
 * deducted from the `*timeout` value. A timeout of 100000 microseconds is
 * recommended for most use cases.
 *
 * @throws IOException Something went wrong.
 * @throws SystemException Something went wrong.
 * @throws boost::thread_interrupted A system call has been interrupted.
 */
bool pingTcpServer(const StaticString &host, unsigned int port, unsigned long long *timeout);

/**
 * Creates a Unix domain socket pair.
 *
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
SocketPair createUnixSocketPair(const char *file, unsigned int line);

/**
 * Creates a pipe.
 *
 * @param file The name of the source file that called this function,
 *             for file descriptor logging purposes.
 * @param line The line in the source file that called this function.
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
Pipe createPipe(const char *file, unsigned int line);

/**
 * Waits at most <tt>*timeout</tt> microseconds for the file descriptor to become readable.
 * Returns true if it become readable within the timeout, false if the timeout expired.
 *
 * <tt>*timeout</tt> may be 0, in which case this method will check whether the file
 * descriptor is readable, and immediately returns without waiting.
 *
 * If no exception is thrown, this method deducts the number of microseconds that has
 * passed from <tt>*timeout</tt>.
 *
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
bool waitUntilReadable(int fd, unsigned long long *timeout);

/**
 * Waits at most <tt>*timeout</tt> microseconds for the file descriptor to become writable.
 * Returns true if it become writable within the timeout, false if the timeout expired.
 *
 * <tt>*timeout</tt> may be 0, in which case this method will check whether the file
 * descriptor is writable, and immediately returns without waiting.
 *
 * If no exception is thrown, this method deducts the number of microseconds that has
 * passed from <tt>*timeout</tt>.
 *
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
bool waitUntilWritable(int fd, unsigned long long *timeout);

/**
 * Attempts to read exactly `size` bytes of data, putting the result in `buf`.
 * This is in contrast to read(), which may read less than `size` bytes even
 * when EOF is not reached.
 *
 * On non-blocking sockets, this function blocks by poll()ing the socket.
 *
 * @param buf The buffer to place the read data in. This buffer must be at least
 *            <tt>size</tt> bytes long.
 * @param size The number of bytes to read.
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on reading the <tt>size</tt> bytes
 *                of data. If the timeout expired then TimeoutException will be
 *                thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on reading will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @return The number of bytes read. This is exactly equal to <em>size</em>,
 *         except when EOF is encountered prematurely.
 * @pre buf != NULL
 * @throws SystemException
 * @throws TimeoutException Unable to read <tt>size</tt> bytes of data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
unsigned int readExact(int fd, void * restrict buf, unsigned int size, unsigned long long * restrict timeout = NULL);

/**
 * Writes a block of data to the given file descriptor and blocks until everything
 * is written, even for non-blocking sockets. If not everything can be written (e.g.
 * because the peer closed the connection before accepting everything) then an
 * exception will be thrown.
 *
 * @note Security guarantee: this method will not copy the data in memory,
 *       so it's safe to use this method to write passwords to the underlying
 *       file descriptor.
 *
 * @param data The data to send.
 * @param size The number of bytes in <tt>data</tt>.
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on writing the <tt>size</tt> bytes
 *                of data. If the timeout expired then TimeoutException will be
 *                thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @pre data != NULL
 * @throws SystemException
 * @throws TimeoutException Unable to write <tt>size</tt> bytes of data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
void writeExact(int fd, const void * restrict data, unsigned int size, unsigned long long * restrict timeout = NULL);
void writeExact(int fd, const StaticString & restrict_ref data, unsigned long long * restrict timeout = NULL);

/**
 * Writes a bunch of data to the given file descriptor using a gathering I/O interface.
 * Instead of accepting a single buffer, this function accepts multiple buffers plus
 * a special 'rest' buffer. The rest buffer is written out first, and the data buffers
 * are then written out in the order as they appear. This all is done with a single
 * writev() system call without concatenating all data into a single buffer.
 *
 * This function is designed for use with non-blocking sockets. It returns the number
 * of bytes that have been written, and ensures that restBuffer will contain all data
 * that has not been written, i.e. should be written out as soon as the file descriptor
 * is writeable again. If everything has been successfully written out then restBuffer
 * will be empty.
 * A return value of 0 indicates that nothing could be written without blocking.
 *
 * Returns -1 if an error occurred other than one which indicates blocking. In this
 * case, <tt>errno</tt> is set appropriately.
 *
 * This function also takes care of all the stupid writev() limitations such as
 * IOV_MAX. It ensures that no more than IOV_MAX items will be passed to writev().
 *
 * @param fd The file descriptor to write to.
 * @param data The data to write.
 * @param dataCount Number of elements in <tt>data</tt>.
 * @param restBuffer The rest buffer, as documented above.
 * @return The number of bytes that have been written out, or -1 on any error that
 *         isn't related to non-blocking writes.
 * @throws boost::thread_interrupted
 */
ssize_t gatheredWrite(int fd, const StaticString * restrict data, unsigned int dataCount, string & restrict_ref restBuffer);

/**
 * Writes a bunch of data to the given file descriptor using a gathering I/O interface.
 * Instead of accepting a single buffer, this function accepts multiple buffers
 * which are all written out in the order as they appear. This is done with a single
 * system call without concatenating all data into a single buffer.
 *
 * This method is a convenience wrapper around writev() but it blocks until all data
 * has been written and takes care of handling system limits (IOV_MAX) for you.
 *
 * This version is designed for blocking sockets so do not use it on non-blocking ones.
 *
 * @param fd The file descriptor to write to.
 * @param data An array of buffers to be written.
 * @param count Number of items in <em>data</em>.
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on writing all the data.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to write all given data within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
void    gatheredWrite(int fd, const StaticString * restrict data, unsigned int dataCount, unsigned long long * restrict timeout = NULL);

/**
 * Sets a writev-emulating function that gatheredWrite() should call instead of the real writev().
 * Useful for unit tests. Pass NULL to restore back to the real writev().
 */
void setWritevFunction(WritevFunction func);

/**
 * Receive a file descriptor over the given Unix domain socket.
 * This is a low-level function that directly wraps the Unix file
 * descriptor passing system calls. You should not use this directly;
 * instead you should use readFileDescriptorWithNegotiation() from MessageIO.h
 * which is safer. See MessageIO.h for more information about the
 * negotiation protocol for file descriptor passing.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on receiving the file descriptor.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on receiving will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @return The received file descriptor.
 * @throws SystemException Something went wrong.
 * @throws IOException Whatever was received doesn't seem to be a
 *                     file descriptor.
 * @throws TimeoutException Unable to receive a file descriptor within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
int readFileDescriptor(int fd, unsigned long long *timeout = NULL);

/**
 * Pass the file descriptor 'fdToSend' over the Unix socket 'fd'.
 * This is a low-level function that directly wraps the Unix file
 * descriptor passing system calls. You should not use this directly;
 * instead you should use writeFileDescriptorWithNegotiation() from MessageIO.h
 * which is safer. See MessageIO.h for more information about the
 * negotiation protocol for file descriptor passing.
 *
 * @param timeout A pointer to an integer, which specifies the maximum number of
 *                microseconds that may be spent on trying to pass the file descriptor.
 *                If the timeout expired then TimeoutException will be thrown.
 *                If this function returns without throwing an exception, then the
 *                total number of microseconds spent on writing will be deducted
 *                from <tt>timeout</tt>.
 *                Pass NULL if you do not want to enforce a timeout.
 * @throws SystemException Something went wrong.
 * @throws TimeoutException Unable to pass the file descriptor within
 *                          <tt>timeout</tt> microseconds.
 * @throws boost::thread_interrupted
 */
void writeFileDescriptor(int fd, int fdToSend, unsigned long long *timeout = NULL);

/**
 * Return the effective UID and GID of the peer connected to a Unix domain socket.
 *
 * @throws SystemException Something went wrong. If reading credentials over a Unix
 *                         domain socket is not supported for the current platform,
 *                         then the error code is ENOSYS. If the socket does not
 *                         support credentials passing, then the error code is
 *                         EPROTONOSUPPORT.
 */
void readPeerCredentials(int sock, uid_t *uid, gid_t *gid);

/**
 * Closes the given file descriptor and throws an exception if anything goes wrong.
 * This function also works around certain close() bugs and quirks on certain
 * operating systems, such as the FreeBSD ENOTCONN-on-close bug and the fact that
 * when close() returns EINTR the state of the file descriptor is unspecified.
 * See IOUtils.cpp and ext/oxt/system_calls.cpp for details.
 *
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
#ifndef _PASSENGER_SAFELY_CLOSE_DEFINED_
	#define _PASSENGER_SAFELY_CLOSE_DEFINED_
	void safelyClose(int fd, bool ignoreErrors = false);
#endif

/**
 * Read all data from the given file descriptor until EOF, or until `maxSize`
 * is reached.
 *
 * Returns a pair `(contents, eof)`.
 *
 *  - `contents` is the read file contents, which is at most `maxSize` bytes.
 *  - `eof` indicates whether the entire file has been read. If false, then it
 *    means the amount of data is larger than `maxSize`.
 *
 * @throws SystemException
 */
pair<string, bool> readAll(int fd, size_t maxSize);

} // namespace Passenger

#endif /* _PASSENGER_IOTOOLS_IO_UTILS_H_ */
