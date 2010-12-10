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

#ifndef _GNU_SOURCE
	// Needed for IOV_MAX on Linux:
	// https://bugzilla.redhat.com/show_bug.cgi?id=165427
	#define _GNU_SOURCE
#endif

#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <oxt/macros.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath>

#ifdef __linux__
	#include <sys/syscall.h>
#endif

#include "Timer.h"
#include "IOUtils.h"
#include "StrIntUtils.h"
#include "../Exceptions.h"

namespace Passenger {

using namespace std;
using namespace oxt;

// Urgh, Solaris :-(
#ifndef AF_LOCAL
	#define AF_LOCAL AF_UNIX
#endif
#ifndef PF_LOCAL
	#define PF_LOCAL PF_UNIX
#endif

static WritevFunction writevFunction = syscalls::writev;


ServerAddressType
getSocketAddressType(const StaticString &address) {
	const char *data = address.c_str();
	size_t len = address.size();
	
	if (len > sizeof("unix:") - 1 && memcmp(data, "unix:", sizeof("unix:") - 1) == 0) {
		return SAT_UNIX;
	} else if (len > sizeof("tcp://") - 1 && memcmp(data, "tcp://", sizeof("tcp://") - 1) == 0) {
		return SAT_TCP;
	} else {
		return SAT_UNKNOWN;
	}
}

string
parseUnixSocketAddress(const StaticString &address) {
	if (getSocketAddressType(address) != SAT_UNIX) {
		throw ArgumentException("Not a valid Unix socket address");
	}
	return string(address.c_str() + sizeof("unix:") - 1, address.size() - sizeof("unix:") + 1);
}

void
parseTcpSocketAddress(const StaticString &address, string &host, unsigned short &port) {
	if (getSocketAddressType(address) != SAT_TCP) {
		throw ArgumentException("Not a valid TCP socket address");
	}
	
	vector<string> args;
	string begin(address.c_str() + sizeof("tcp://") - 1, address.size() - sizeof("tcp://") + 1);
	split(begin, ':', args);
	if (args.size() != 2) {
		throw ArgumentException("Not a valid TCP socket address");
	} else {
		host = args[0];
		port = atoi(args[1].c_str());
	}
}

bool
isLocalSocketAddress(const StaticString &address) {
	switch (getSocketAddressType(address)) {
	case SAT_UNIX:
		return true;
	case SAT_TCP: {
		string host;
		unsigned short port;
		
		parseTcpSocketAddress(address, host, port);
		return host == "127.0.0.1" || host == "::1" || host == "localhost";
	}
	default:
		throw ArgumentException("Unsupported socket address type");
	}
}

void
setNonBlocking(int fd) {
	int flags, ret;
	
	do {
		flags = fcntl(fd, F_GETFL);
	} while (flags == -1 && errno == EINTR);
	if (flags == -1) {
		int e = errno;
		throw SystemException("Cannot set socket to non-blocking mode: "
			"cannot get socket flags",
			e);
	}
	do {
		ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		int e = errno;
		throw SystemException("Cannot set socket to non-blocking mode: "
			"cannot set socket flags",
			e);
	}
}

int
tryAccept4(int sock, struct sockaddr *addr, socklen_t *addr_len, int options) {
	#if defined(__linux__) && defined(__x86_64__)
		int ret;
		do {
			ret = syscall(288, sock, addr, addr_len, options);
		} while (ret == -1 && errno == EINTR);
		return ret;
	#elif defined(__linux__) && defined(__i386__)
		int ret;
		do {
			ret = syscall(__NR_socketcall, 18,
				sock, addr, addr_len, options);
		} while (ret == -1 && errno == EINTR);
		return ret;
	#elif defined(SYS_ACCEPT4)
		int ret;
		do {
			ret = ::accept4(sock, addr, addr_len, options);
		} while (ret == -1 && errno == EINTR);
		return ret;
	#else
		errno = ENOSYS;
		return -1;
	#endif
}

vector<string>
resolveHostname(const string &hostname, unsigned int port, bool shuffle) {
	string portString = toString(port);
	struct addrinfo hints, *res, *current;
	vector<string> result;
	int ret;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(hostname.c_str(), (port == 0) ? NULL : portString.c_str(),
		&hints, &res);
	if (ret != 0) {
		return result;
	}
	
	for (current = res; current != NULL; current = current->ai_next) {
		char host[NI_MAXHOST];
		
		ret = getnameinfo(current->ai_addr, current->ai_addrlen,
			host, sizeof(host) - 1,
			NULL, 0,
			NI_NUMERICHOST);
		if (ret == 0) {
			result.push_back(host);
		}
	}
	freeaddrinfo(res);
	if (shuffle) {
		random_shuffle(result.begin(), result.end());
	}
	return result;
}

int
createServer(const StaticString &address, unsigned int backlogSize, bool autoDelete) {
	TRACE_POINT();
	switch (getSocketAddressType(address)) {
	case SAT_UNIX:
		return createUnixServer(parseUnixSocketAddress(address),
			backlogSize, autoDelete);
	case SAT_TCP: {
		string host;
		unsigned short port;
		
		parseTcpSocketAddress(address, host, port);
		return createTcpServer(host.c_str(), port, backlogSize);
	}
	default:
		throw ArgumentException(string("Unknown address type for '") + address + "'");
	}
}

int
createUnixServer(const StaticString &filename, unsigned int backlogSize, bool autoDelete) {
	struct sockaddr_un addr;
	int fd, ret;
	
	if (filename.size() > sizeof(addr.sun_path) - 1) {
		string message = "Cannot create Unix socket '";
		message.append(filename.toString());
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	fd = syscalls::socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
		int e = errno;
		throw SystemException("Cannot create a Unix socket file descriptor", e);
	}
	
	addr.sun_family = AF_LOCAL;
	strncpy(addr.sun_path, filename.c_str(), filename.size());
	addr.sun_path[filename.size()] = '\0';
	
	if (autoDelete) {
		do {
			ret = unlink(filename.c_str());
		} while (ret == -1 && errno == EINTR);
	}
	
	try {
		ret = syscalls::bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot bind Unix socket '";
		message.append(filename.toString());
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	if (backlogSize == 0) {
		backlogSize = 1024;
	}
	try {
		ret = syscalls::listen(fd, backlogSize);
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot listen on Unix socket '";
		message.append(filename.toString());
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

int
createTcpServer(const char *address, unsigned short port, unsigned int backlogSize) {
	struct sockaddr_in addr;
	int fd, ret, optval;
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	ret = inet_pton(AF_INET, address, &addr.sin_addr.s_addr);
	if (ret < 0) {
		int e = errno;
		string message = "Cannot parse the IP address '";
		message.append(address);
		message.append("'");
		throw SystemException(message, e);
	} else if (ret == 0) {
		string message = "Cannot parse the IP address '";
		message.append(address);
		message.append("'");
		throw ArgumentException(message);
	}
	addr.sin_port = htons(port);
	
	fd = syscalls::socket(PF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		int e = errno;
		throw SystemException("Cannot create a TCP socket file descriptor", e);
	}
	
	try {
		ret = syscalls::bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot bind a TCP socket on address '";
		message.append(address);
		message.append("' port ");
		message.append(toString(port));
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	optval = 1;
	try {
		if (syscalls::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			&optval, sizeof(optval)) == -1) {
				printf("so_reuseaddr failed: %s\n", strerror(errno));
			}
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	// Ignore SO_REUSEPORT error, it's not fatal.
	
	if (backlogSize == 0) {
		backlogSize = 1024;
	}
	try {
		ret = syscalls::listen(fd, backlogSize);
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot listen on TCP socket '";
		message.append(address);
		message.append("' port ");
		message.append(toString(port));
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

int
connectToServer(const StaticString &address) {
	TRACE_POINT();
	switch (getSocketAddressType(address)) {
	case SAT_UNIX:
		return connectToUnixServer(parseUnixSocketAddress(address));
	case SAT_TCP: {
		string host;
		unsigned short port;
		
		parseTcpSocketAddress(address, host, port);
		return connectToTcpServer(host, port);
	}
	default:
		throw ArgumentException(string("Unknown address type for '") + address + "'");
	}
}

int
connectToUnixServer(const StaticString &filename) {
	int fd, ret;
	struct sockaddr_un addr;
	
	if (filename.size() > sizeof(addr.sun_path) - 1) {
		string message = "Cannot connect to Unix socket '";
		message.append(filename.toString());
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	fd = syscalls::socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		int e = errno;
		throw SystemException("Cannot create a Unix socket file descriptor", e);
	}
	
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, filename.c_str(), filename.size());
	addr.sun_path[filename.size()] = '\0';
	
	bool retry = true;
	int counter = 0;
	while (retry) {
		try {
			ret = syscalls::connect(fd, (const sockaddr *) &addr, sizeof(addr));
		} catch (...) {
			do {
				ret = close(fd);
			} while (ret == -1 && errno == EINTR);
			throw;
		}
		if (ret == -1) {
			#if defined(sun) || defined(__sun)
				/* Solaris has this nice kernel bug where connecting to
				 * a newly created Unix socket which is obviously
				 * connectable can cause an ECONNREFUSED. So we retry
				 * in a loop.
				 */
				retry = errno == ECONNREFUSED;
			#else
				retry = false;
			#endif
			retry = retry && counter < 9;
			
			if (retry) {
				syscalls::usleep((useconds_t) (10000 * pow((double) 2, (double) counter)));
				counter++;
			} else {
				int e = errno;
				string message("Cannot connect to Unix socket '");
				message.append(filename.toString());
				message.append("'");
				do {
					ret = close(fd);
				} while (ret == -1 && errno == EINTR);
				throw SystemException(message, e);
			}
		} else {
			return fd;
		}
	}
	abort();   // Never reached.
	return -1; // Shut up compiler warning.
}

int
connectToTcpServer(const StaticString &hostname, unsigned int port) {
	struct addrinfo hints, *res;
	int ret, e, fd;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(hostname.c_str(), toString(port).c_str(), &hints, &res);
	if (ret != 0) {
		string message = "Cannot resolve IP address '";
		message.append(hostname.toString());
		message.append(":");
		message.append(toString(port));
		message.append("': ");
		message.append(gai_strerror(ret));
		throw IOException(message);
	}
	
	try {
		fd = syscalls::socket(PF_INET, SOCK_STREAM, 0);
	} catch (...) {
		freeaddrinfo(res);
		throw;
	}
	if (fd == -1) {
		e = errno;
		freeaddrinfo(res);
		throw SystemException("Cannot create a TCP socket file descriptor", e);
	}
	
	try {
		ret = syscalls::connect(fd, res->ai_addr, res->ai_addrlen);
	} catch (...) {
		freeaddrinfo(res);
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	e = errno;
	freeaddrinfo(res);
	if (ret == -1) {
		string message = "Cannot connect to TCP socket '";
		message.append(hostname.toString());
		message.append(":");
		message.append(toString(port));
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

SocketPair
createUnixSocketPair() {
	int fds[2];
	FileDescriptor sockets[2];
	
	if (syscalls::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
		int e = errno;
		throw SystemException("Cannot create a Unix socket pair", e);
	} else {
		sockets[0] = fds[0];
		sockets[1] = fds[1];
		return SocketPair(sockets[0], sockets[1]);
	}
}

Pipe
createPipe() {
	int fds[2];
	FileDescriptor p[2];
	
	if (syscalls::pipe(fds) == -1) {
		int e = errno;
		throw SystemException("Cannot create a pipe", e);
	} else {
		p[0] = fds[0];
		p[1] = fds[1];
		return Pipe(p[0], p[1]);
	}
}

static bool
waitUntilIOEvent(int fd, short event, unsigned long long *timeout) {
	struct pollfd pfd;
	int ret;
	
	pfd.fd = fd;
	pfd.events = event;
	pfd.revents = 0;
	
	Timer timer;
	ret = syscalls::poll(&pfd, 1, *timeout / 1000);
	if (ret == -1) {
		int e = errno;
		throw SystemException("poll() failed", e);
	} else {
		unsigned long long elapsed = timer.usecElapsed();
		if (elapsed > *timeout) {
			*timeout = 0;
		} else {
			*timeout -= elapsed;
		}
		return ret != 0;
	}
}

bool
waitUntilReadable(int fd, unsigned long long *timeout) {
	return waitUntilIOEvent(fd, POLLIN, timeout);
}

bool
waitUntilWritable(int fd, unsigned long long *timeout) {
	return waitUntilIOEvent(fd, POLLOUT | POLLHUP, timeout);
}

unsigned int
readExact(int fd, void *buf, unsigned int size, unsigned long long *timeout) {
	ssize_t ret;
	unsigned int alreadyRead = 0;
	
	while (alreadyRead < size) {
		if (timeout != NULL && !waitUntilReadable(fd, timeout)) {
			throw TimeoutException("Cannot read enough data within the specified timeout");
		}
		ret = syscalls::read(fd, (char *) buf + alreadyRead, size - alreadyRead);
		if (ret == -1) {
			int e = errno;
			throw SystemException("read() failed", e);
		} else if (ret == 0) {
			return alreadyRead;
		} else {
			alreadyRead += ret;
		}
	}
	return alreadyRead;
}

void
writeExact(int fd, const void *data, unsigned int size, unsigned long long *timeout) {
	ssize_t ret;
	unsigned int written = 0;
	while (written < size) {
		if (timeout != NULL && !waitUntilWritable(fd, timeout)) {
			throw TimeoutException("Cannot write enough data within the specified timeout");
		}
		ret = syscalls::write(fd, (const char *) data + written, size - written);
		if (ret == -1) {
			int e = errno;
			throw SystemException("write() failed", e);
		} else {
			written += ret;
		}
	}
}

void
writeExact(int fd, const StaticString &data, unsigned long long *timeout) {
	writeExact(fd, data.c_str(), data.size(), timeout);
}

/**
 * Converts an array of StaticStrings to a corresponding array of iovec structures,
 * returning the size sum in bytes of all StaticStrings.
 */
static size_t
staticStringArrayToIoVec(const StaticString ary[], size_t count, struct iovec *vec, size_t &vecCount) {
	size_t total = 0;
	size_t i;
	for (i = 0, vecCount = 0; i < count; i++) {
		/* No idea whether all writev() implementations support iov_len == 0,
		 * but I'd rather not risk finding out.
		 */
		if (ary[i].size() > 0) {
			/* I know writev() doesn't write to iov_base, but on some
			 * platforms it's still defined as non-const char *
			 * :-(
			 */
			vec[vecCount].iov_base = (char *) ary[i].data();
			vec[vecCount].iov_len  = ary[i].size();
			total += ary[i].size();
			vecCount++;
		}
	}
	return total;
}

/**
 * Suppose that the given IO vectors are placed adjacent to each other
 * in a single contiguous block of memory. Given a position inside this
 * block of memory, this function will calculate the index in the IO vector
 * array and the offset inside that IO vector that corresponds with
 * the position.
 *
 * For example, given the following array of IO vectors:
 * { "AAA", "BBBB", "CC" }
 * Position 0 would correspond to the first item, offset 0.
 * Position 1 would correspond to the first item, offset 1.
 * Position 5 would correspond to the second item, offset 2.
 * And so forth.
 *
 * If the position is outside the bounds of the array, then index will be
 * set to count + 1 and offset to 0.
 */
static void
findDataPositionIndexAndOffset(struct iovec data[], size_t count,
	size_t position, size_t *index, size_t *offset)
{
	size_t i;
	size_t begin = 0;

	for (i = 0; i < count; i++) {
		size_t end = begin + data[i].iov_len;
		if (OXT_LIKELY(begin <= position)) {
			if (position < end) {
				*index = i;
				*offset = position - begin;
				return;
			} else {
				begin = end;
			}
		} else {
			// Never reached.
			abort();
		}
	}
	*index = count;
	*offset = 0;
}

ssize_t
gatheredWrite(int fd, const StaticString data[], unsigned int dataCount, string &restBuffer) {
	size_t totalSize, iovCount, i;
	ssize_t ret;
	
	if (restBuffer.empty()) {
		struct iovec iov[dataCount];
		
		totalSize = staticStringArrayToIoVec(data, dataCount, iov, iovCount);
		if (totalSize == 0) {
			errno = 0;
			return 0;
		}
		
		ret = writevFunction(fd, iov, std::min(iovCount, (size_t) IOV_MAX));
		if (ret == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// Nothing could be written without blocking, so put
				// everything in the rest buffer.
				int e = errno;
				restBuffer.reserve(totalSize);
				for (i = 0; i < iovCount; i++) {
					restBuffer.append((const char *) iov[i].iov_base,
						iov[i].iov_len);
				}
				errno = e;
				return 0;
			} else {
				return -1;
			}
		} else if ((size_t) ret < totalSize) {
			size_t index, offset;
			
			// Put all unsent data in the rest buffer.
			restBuffer.reserve(ret);
			findDataPositionIndexAndOffset(iov, iovCount, ret, &index, &offset);
			for (i = index; i < iovCount; i++) {
				if (i == index) {
					restBuffer.append(
						((const char *) iov[i].iov_base) + offset,
						iov[i].iov_len - offset);
				} else {
					restBuffer.append(
						(const char *) iov[i].iov_base,
						iov[i].iov_len);
				}
			}
			
			// TODO: we should call writev() again if iovCount > iovMax
			// in order to send out the rest of the data without
			// putting them in the rest buffer.
			
			return ret;
		} else {
			// Everything is sent, and the rest buffer was empty anyway, so
			// just return.
			return totalSize;
		}
	} else {
		struct iovec iov[dataCount + 1];
		
		iov[0].iov_base = (char *) restBuffer.data();
		iov[0].iov_len  = restBuffer.size();
		totalSize = staticStringArrayToIoVec(data, dataCount, iov + 1, iovCount);
		totalSize += restBuffer.size();
		iovCount++;
		
		ret = writevFunction(fd, iov, std::min(iovCount, (size_t) IOV_MAX));
		if (ret == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// Nothing could be written without blocking, so
				// append all data into the rest buffer.
				int e = errno;
				restBuffer.reserve(totalSize);
				for (i = 1; i < iovCount; i++) {
					restBuffer.append(
						(const char *) iov[i].iov_base,
						iov[i].iov_len);
				}
				errno = e;
				return 0;
			} else {
				return -1;
			}
		} else {
			string::size_type restBufferSize = restBuffer.size();
			size_t restBufferSent = std::min((size_t) ret, (size_t) restBufferSize);
			
			// Remove everything in the rest buffer that we've been able to send.
			restBuffer.erase(0, restBufferSent);
			if (restBuffer.empty()) {
				size_t index, offset;
				
				// Looks like everything in the rest buffer was sent.
				// Put all unsent data into the rest buffer.
				findDataPositionIndexAndOffset(iov, iovCount, ret,
					&index, &offset);
				for (i = index; i < iovCount; i++) {
					if (i == index) {
						restBuffer.append(
							((const char *) iov[i].iov_base) + offset,
							iov[i].iov_len - offset);
					} else {
						restBuffer.append(
							(const char *) iov[i].iov_base,
							iov[i].iov_len);
					}
				}
				
				// TODO: we should call writev() again if
				// iovCount > iovMax && ret < totalSize
				// in order to send out the rest of the data without
				// putting them in the rest buffer.
			} else {
				// The rest buffer could only be partially sent out, so
				// nothing in 'data' could be sent. Append everything
				// in 'data' into the rest buffer.
				restBuffer.reserve(totalSize - ret);
				for (i = 1; i < iovCount; i++) {
					restBuffer.append(
						(const char *) iov[i].iov_base,
						iov[i].iov_len);
				}
			}
			return ret;
		}
	}
}

static size_t
eraseBeginningOfIoVec(struct iovec *iov, size_t count, size_t index, size_t offset) {
	size_t i, newCount;
	for (i = index, newCount = 0; i < count; i++, newCount++) {
		if (newCount == 0) {
			iov[newCount].iov_base = (char *) iov[i].iov_base + offset;
			iov[newCount].iov_len  = iov[i].iov_len - offset;
		} else {
			iov[newCount].iov_base = iov[i].iov_base;
			iov[newCount].iov_len  = iov[i].iov_len;
		}
	}
	return newCount;
}

void
gatheredWrite(int fd, const StaticString data[], unsigned int count) {
	struct iovec iov[count];
	size_t total, iovCount;
	size_t written = 0;
	
	total = staticStringArrayToIoVec(data, count, iov, iovCount);
	
	while (written < total) {
		ssize_t ret = writevFunction(fd, iov, std::min(iovCount, (size_t) IOV_MAX));
		if (ret == -1) {
			int e = errno;
			throw SystemException("Unable to write all data", e);
		} else {
			size_t index, offset;
			
			written += ret;
			findDataPositionIndexAndOffset(iov, iovCount, ret, &index, &offset);
			iovCount = eraseBeginningOfIoVec(iov, iovCount, index, offset);
		}
	}
	assert(written == total);
}

void
setWritevFunction(WritevFunction func) {
	if (func != NULL) {
		writevFunction = func;
	} else {
		writevFunction = syscalls::writev;
	}
}

void
safelyClose(int fd) {
	if (syscalls::close(fd) == -1) {
		/* FreeBSD has a kernel bug which can cause close() to return ENOTCONN.
		 * This is harmless, ignore it. We check for this problem on all
		 * platforms because some OSes might borrow Unix domain socket
		 * code from FreeBSD.
		 * http://www.freebsd.org/cgi/query-pr.cgi?pr=79138
		 * http://www.freebsd.org/cgi/query-pr.cgi?pr=144061
		 */
		if (errno != ENOTCONN) {
			int e = errno;
			throw SystemException("Cannot close file descriptor", e);
		}
	}
}


} // namespace Passenger
