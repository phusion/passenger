#ifndef _PASSENGER_MESSAGE_CHANNEL_H_
#define _PASSENGER_MESSAGE_CHANNEL_H_

#include <algorithm>
#include <string>
#include <list>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#include "Exceptions.h"

namespace Passenger {

using namespace std;

class MessageChannel {
private:
	const static char DELIMITER = '\0';
	int fd;

public:
	MessageChannel() {
		this->fd = -1;
	}

	MessageChannel(int fd) {
		this->fd = fd;
	}
	
	void close() {
		if (fd != -1) {
			::close(fd);
			fd = -1;
		}
	}

	void write(const list<string> &args) {
		list<string>::const_iterator it;
		string data;
		uint16_t dataSize = 0;
		unsigned int i = 0;
		string::size_type written;
		int ret;

		for (it = args.begin(); it != args.end(); it++) {
			dataSize += it->size() + 1;
		}
		if (!args.empty()) {
			dataSize--;
			data.reserve(dataSize + sizeof(dataSize));
			dataSize = htons(dataSize);
			data.append((const char *) &dataSize, sizeof(dataSize));
		}
		for (it = args.begin(); it != args.end(); it++) {
			data.append(*it);
			if (i != args.size() - 1) {
				data.append(1, DELIMITER);
			}
			i++;
		}
		
		written = 0;
		do {
			do {
				ret = ::write(fd, data.data() + written, data.size() - written);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				throw SystemException("write() failed", errno);
			} else {
				written += ret;
			}
		} while (written < data.size());
	}
	
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
	
	void writeFileDescriptor(int fileDescriptor) {
		struct msghdr msg;
		struct iovec vec[1];
		char buf[1];
		struct {
			struct cmsghdr hdr;
			int fd;
		} cmsg;
	
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
	
		/* Linux and Solaris doesn't work if msg_iov is NULL. */
		buf[0] = '\0';
		vec[0].iov_base = buf;
		vec[0].iov_len = 1;
		msg.msg_iov = vec;
		msg.msg_iovlen = 1;
	
		msg.msg_control = (caddr_t)&cmsg;
		msg.msg_controllen = CMSG_SPACE(sizeof(int));
		msg.msg_flags = 0;
		cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(int));
		cmsg.hdr.cmsg_level = SOL_SOCKET;
		cmsg.hdr.cmsg_type = SCM_RIGHTS;
		cmsg.fd = fileDescriptor;
		
		if (sendmsg(fd, &msg, 0) == -1) {
			throw SystemException("Cannot send file descriptor with sendmsg()", errno);
		}
	}
	
	bool read(vector<string> &args) {
		uint16_t size;
		int ret;
		unsigned int alreadyRead = 0;
		
		do {
			do {
				ret = ::read(fd, (char *) &size + alreadyRead, sizeof(size) - alreadyRead);
			} while (ret == -1 && errno == EINTR);
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
			do {
				ret = ::read(fd, tmp, min(size - buffer.size(), sizeof(tmp)));
			} while (ret == -1 && errno == EINTR);
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
			args.push_back(const_buffer.substr(start));
		}
		return true;
	}
	
	int readFileDescriptor() {
		struct msghdr msg;
		struct iovec vec[2];
		char buf[1];
		struct {
			struct cmsghdr hdr;
			int fd;
		} cmsg;

		msg.msg_name = NULL;
		msg.msg_namelen = 0;
	
		vec[0].iov_base = buf;
		vec[0].iov_len = sizeof(buf);
		msg.msg_iov = vec;
		msg.msg_iovlen = 1;

		msg.msg_control = (caddr_t)&cmsg;
		msg.msg_controllen = CMSG_SPACE(sizeof(int));
		msg.msg_flags = 0;
		cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(int));
		cmsg.hdr.cmsg_level = SOL_SOCKET;
		cmsg.hdr.cmsg_type = SCM_RIGHTS;
		cmsg.fd = -1;

		if (recvmsg(fd, &msg, 0) == -1) {
			throw SystemException("Cannot read file descriptor with recvmsg()", errno);
		}

		if (msg.msg_controllen != CMSG_SPACE(sizeof(int))
		 || cmsg.hdr.cmsg_len != CMSG_SPACE(0) + sizeof(int)
		 || cmsg.hdr.cmsg_level != SOL_SOCKET
		 || cmsg.hdr.cmsg_type != SCM_RIGHTS) {
			throw IOException("No valid file descriptor received.");
		}
		return cmsg.fd;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_CHANNEL_H_ */
