#include "ruby.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

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

void
Init_native_support() {
	mModRails = rb_define_module("ModRails");
	mNativeSupport = rb_define_module_under(mModRails, "NativeSupport");
	rb_define_method(mNativeSupport, "send_fd", send_fd, 2);
	rb_define_method(mNativeSupport, "recv_fd", recv_fd, 1);
}
