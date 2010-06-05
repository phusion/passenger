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

#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
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
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "IOUtils.h"
#include "StrIntUtils.h"
#include "../Exceptions.h"

namespace Passenger {

using namespace std;
using namespace oxt;


ServerAddressType getSocketAddressType(const char *address) {
	size_t len = strlen(address);
	
	if (len > sizeof("unix:") - 1 && memcmp(address, "unix:", sizeof("unix:") - 1) == 0) {
		return SAT_UNIX;
	} else if (len > sizeof("tcp://") - 1 && memcmp(address, "tcp://", sizeof("tcp://") - 1) == 0) {
		return SAT_TCP;
	} else {
		return SAT_UNKNOWN;
	}
}

string parseUnixSocketAddress(const char *address) {
	if (getSocketAddressType(address) != SAT_UNIX) {
		throw ArgumentException("Not a valid Unix socket address");
	}
	return string(address + sizeof("unix:") - 1);
}

void parseTcpSocketAddress(const char *address, string &host, unsigned short &port) {
	if (getSocketAddressType(address) != SAT_TCP) {
		throw ArgumentException("Not a valid TCP socket address");
	}
	
	vector<string> args;
	split(address + sizeof("tcp://") - 1, ':', args);
	if (args.size() != 2) {
		throw ArgumentException("Not a valid TCP socket address");
	} else {
		host = args[0];
		port = atoi(args[1].c_str());
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
createServer(const char *address, unsigned int backlogSize, bool autoDelete) {
	TRACE_POINT();
	switch (getSocketAddressType(address)) {
	case SAT_UNIX:
		return createUnixServer(parseUnixSocketAddress(address).c_str(),
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
createUnixServer(const char *filename, unsigned int backlogSize, bool autoDelete) {
	struct sockaddr_un addr;
	int fd, ret;
	
	if (strlen(filename) > sizeof(addr.sun_path) - 1) {
		string message = "Cannot create Unix socket '";
		message.append(filename);
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	fd = syscalls::socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
		int e = errno;
		throw SystemException("Cannot create a Unix socket file descriptor", e);
	}
	
	addr.sun_family = AF_LOCAL;
	strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
	
	if (autoDelete) {
		do {
			ret = unlink(filename);
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
		message.append(filename);
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
		message.append(filename);
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
connectToUnixServer(const char *filename) {
	int fd, ret;
	struct sockaddr_un addr;
	
	if (strlen(filename) > sizeof(addr.sun_path) - 1) {
		string message = "Cannot connect to Unix socket '";
		message.append(filename);
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	fd = syscalls::socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		int e = errno;
		throw SystemException("Cannot create a Unix socket file descriptor", e);
	}
	
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
	
	try {
		ret = syscalls::connect(fd, (const sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message("Cannot connect to Unix socket '");
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

int
connectToTcpServer(const char *hostname, unsigned int port) {
	struct addrinfo hints, *res;
	int ret, e, fd;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(hostname, toString(port).c_str(), &hints, &res);
	if (ret != 0) {
		string message = "Cannot resolve IP address '";
		message.append(hostname);
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
		message.append(hostname);
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


} // namespace Passenger
