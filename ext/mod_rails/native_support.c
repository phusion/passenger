#include "ruby.h"
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static VALUE mModRails;
static VALUE mNativeSupport;

static VALUE
send_fd(VALUE self, VALUE socket_fd, VALUE fd_to_send) {
	int fd;
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
	cmsg.fd = NUM2INT(fd_to_send);
	
	if (sendmsg(NUM2INT(socket_fd), &msg, 0) == -1) {
		rb_sys_fail("sendmsg(2)");
		return Qnil;
	}
	
	return Qnil;
}

static VALUE
recv_fd(VALUE self, VALUE socket_fd) {
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

	if (recvmsg(NUM2INT(socket_fd), &msg, 0) == -1) {
		rb_sys_fail("Cannot read file descriptor with recvmsg()");
		return Qnil;
	}

	if (msg.msg_controllen != CMSG_SPACE(sizeof(int))
	 || cmsg.hdr.cmsg_len != CMSG_SPACE(0) + sizeof(int)
	 || cmsg.hdr.cmsg_level != SOL_SOCKET
	 || cmsg.hdr.cmsg_type != SCM_RIGHTS) {
		rb_sys_fail("No valid file descriptor received.");
		return Qnil;
	}
	return INT2NUM(cmsg.fd);
}

static VALUE
create_unix_socket(VALUE self, VALUE filename, VALUE backlog) {
	int fd, ret;
	struct sockaddr_un addr;
	char *filename_str;
	long filename_length;
	
	filename_str = rb_str2cstr(filename, &filename_length);
	
	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		rb_sys_fail("Cannot create a Unix socket");
		return Qnil;
	}
	
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, filename_str, MIN(filename_length, sizeof(addr.sun_path)));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
	
	ret = bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
	if (ret == -1) {
		int e = errno;
		close(fd);
		errno = e;
		rb_sys_fail("Cannot bind Unix socket");
		return Qnil;
	}
	
	ret = listen(fd, NUM2INT(backlog));
	if (ret == -1) {
		int e = errno;
		close(fd);
		errno = e;
		rb_sys_fail("Cannot listen on Unix socket");
		return Qnil;
	}
	return INT2NUM(fd);
}

static VALUE
f_accept(VALUE self, VALUE fileno) {
	int fd = accept(NUM2INT(fileno), NULL, NULL);
	if (fd == -1) {
		rb_sys_fail("accept() failed");
		return Qnil;
	} else {
		return INT2NUM(fd);
	}
}

void
Init_native_support() {
	struct sockaddr_un addr;
	mModRails = rb_define_module("ModRails");
	mNativeSupport = rb_define_module_under(mModRails, "NativeSupport");
	rb_define_singleton_method(mNativeSupport, "send_fd", send_fd, 2);
	rb_define_singleton_method(mNativeSupport, "recv_fd", recv_fd, 1);
	rb_define_singleton_method(mNativeSupport, "create_unix_socket", create_unix_socket, 2);
	rb_define_singleton_method(mNativeSupport, "accept", f_accept, 1);
	
	/* The maximum length of a Unix socket path, including terminating null. */
	rb_define_const(mNativeSupport, "UNIX_PATH_MAX", INT2NUM(sizeof(addr.sun_path)));
}
