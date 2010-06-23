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
#include "ruby.h"
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <grp.h>
#ifdef __OpenBSD__
	// OpenBSD needs this for 'struct iovec'. Apparently it isn't
	// always included by unistd.h and sys/types.h.
	#include <sys/uio.h>
#endif

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#ifndef RARRAY_LEN
	#define RARRAY_LEN(ary) RARRAY(ary)->len
#endif
#ifndef RSTRING_PTR
	#define RSTRING_PTR(str) RSTRING(str)->ptr
#endif
#ifndef RSTRING_LEN
	#define RSTRING_LEN(str) RSTRING(str)->len
#endif

static VALUE mPassenger;
static VALUE mNativeSupport;

/*
 * call-seq: send_fd(socket_fd, fd_to_send)
 *
 * Send a file descriptor over the given Unix socket. You do not have to call
 * this function directly. A convenience wrapper is provided by IO#send_io.
 *
 * - +socket_fd+ (integer): The file descriptor of the socket.
 * - +fd_to_send+ (integer): The file descriptor to send.
 * - Raises +SystemCallError+ if something went wrong.
 */
static VALUE
send_fd(VALUE self, VALUE socket_fd, VALUE fd_to_send) {
	struct msghdr msg;
	struct iovec vec;
	char dummy[1];
	#if defined(__APPLE__) || defined(__SOLARIS__) || defined(__arm__)
		struct {
			struct cmsghdr header;
			int fd;
		} control_data;
	#else
		char control_data[CMSG_SPACE(sizeof(int))];
	#endif
	struct cmsghdr *control_header;
	int control_payload;
	
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
	control_payload = NUM2INT(fd_to_send);
	#if defined(__APPLE__) || defined(__SOLARIS__) || defined(__arm__)
		control_header->cmsg_len = sizeof(control_data);
		control_data.fd = control_payload;
	#else
		control_header->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(control_header), &control_payload, sizeof(int));
	#endif
	
	if (sendmsg(NUM2INT(socket_fd), &msg, 0) == -1) {
		rb_sys_fail("sendmsg(2)");
		return Qnil;
	}
	
	return Qnil;
}

/*
 * call-seq: recv_fd(socket_fd)
 *
 * Receive a file descriptor from the given Unix socket. Returns the received
 * file descriptor as an integer. Raises +SystemCallError+ if something went
 * wrong.
 *
 * You do not have call this method directly. A convenience wrapper is
 * provided by IO#recv_io.
 */
static VALUE
recv_fd(VALUE self, VALUE socket_fd) {
	struct msghdr msg;
	struct iovec vec;
	char dummy[1];
	#if defined(__APPLE__) || defined(__SOLARIS__) || defined(__arm__)
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
	
	if (recvmsg(NUM2INT(socket_fd), &msg, 0) == -1) {
		rb_sys_fail("Cannot read file descriptor with recvmsg()");
		return Qnil;
	}
	
	control_header = CMSG_FIRSTHDR(&msg);
	if (control_header == NULL) {
		rb_raise(rb_eIOError, "No valid file descriptor received.");
		return Qnil;
	}
	if (control_header->cmsg_len   != EXPECTED_CMSG_LEN
	 || control_header->cmsg_level != SOL_SOCKET
	 || control_header->cmsg_type  != SCM_RIGHTS) {
		rb_raise(rb_eIOError, "No valid file descriptor received.");
		return Qnil;
	}
	#if defined(__APPLE__) || defined(__SOLARIS__) || defined(__arm__)
		return INT2NUM(control_data.fd);
	#else
		return INT2NUM(*((int *) CMSG_DATA(control_header)));
	#endif
}

/*
 * call-seq: create_unix_socket(filename, backlog)
 *
 * Create a SOCK_STREAM server Unix socket. Unlike Ruby's UNIXServer class,
 * this function is also able to create Unix sockets on the abstract namespace
 * by prepending the filename with a null byte.
 *
 * - +filename+ (string): The filename of the Unix socket to create.
 * - +backlog+ (integer): The backlog to use for listening on the socket.
 * - Returns: The file descriptor of the created Unix socket, as an integer.
 * - Raises +SystemCallError+ if something went wrong.
 */
static VALUE
create_unix_socket(VALUE self, VALUE filename, VALUE backlog) {
	int fd, ret;
	struct sockaddr_un addr;
	char *filename_str;
	long filename_length;
	
	filename_str = RSTRING_PTR(filename);
	filename_length = RSTRING_LEN(filename);
	
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

/*
 * call-seq: accept(fileno)
 *
 * Accept a new client from the given socket.
 *
 * - +fileno+ (integer): The file descriptor of the server socket.
 * - Returns: The accepted client's file descriptor.
 * - Raises +SystemCallError+ if something went wrong.
 */
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

/*
 * call-seq: close_all_file_descriptors(exceptions)
 *
 * Close all file descriptors, except those given in the +exceptions+ array.
 * For example, the following would close all file descriptors except standard
 * input (0) and standard output (1).
 *
 *  close_all_file_descriptors([0, 1])
 */
static VALUE
close_all_file_descriptors(VALUE self, VALUE exceptions) {
	long i, j;
	
	for (i = sysconf(_SC_OPEN_MAX) - 1; i >= 0; i--) {
		int is_exception = 0;
		for (j = 0; j < RARRAY_LEN(exceptions) && !is_exception; j++) {
			long fd = NUM2INT(rb_ary_entry(exceptions, j));
			is_exception = i == fd;
		}
		if (!is_exception) {
			close(i);
		}
	}
	return Qnil;
}

/*
 * call-seq: disable_stdio_buffering
 *
 * Disables any kind of buffering on the C +stdout+ and +stderr+ variables,
 * so that +fprintf()+ on +stdout+ and +stderr+ have immediate effect.
 */
static VALUE
disable_stdio_buffering() {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	return Qnil;
}

/**
 * Ruby's implementations of initgroups, setgid and setuid are broken various ways,
 * sigh...
 * Ruby's setgid and setuid can't handle negative UIDs and initgroups is just broken.
 * Work around it by using our own implementation.
 */
static VALUE
switch_user(VALUE self, VALUE username, VALUE uid, VALUE gid) {
	uid_t the_uid = NUM2LL(uid);
	gid_t the_gid = NUM2LL(gid);

	if (initgroups(RSTRING_PTR(username), the_gid) == -1) {
		rb_sys_fail("initgroups");
	}
	if (setgid(the_gid) == -1) {
		rb_sys_fail("setgid");
	}
	if (setuid(the_uid) == -1) {
		rb_sys_fail("setuid");
	}
	return Qnil;
}

/***************************/

void
Init_native_support() {
	struct sockaddr_un addr;
	
	/* */
	mPassenger = rb_define_module("PhusionPassenger"); // Do not remove the above comment. We want the Passenger module's rdoc to be empty.
	
	/*
	 * Utility functions for accessing system functionality.
	 */
	mNativeSupport = rb_define_module_under(mPassenger, "NativeSupport");
	
	rb_define_singleton_method(mNativeSupport, "send_fd", send_fd, 2);
	rb_define_singleton_method(mNativeSupport, "recv_fd", recv_fd, 1);
	rb_define_singleton_method(mNativeSupport, "create_unix_socket", create_unix_socket, 2);
	rb_define_singleton_method(mNativeSupport, "accept", f_accept, 1);
	rb_define_singleton_method(mNativeSupport, "close_all_file_descriptors", close_all_file_descriptors, 1);
	rb_define_singleton_method(mNativeSupport, "disable_stdio_buffering", disable_stdio_buffering, 0);
	rb_define_singleton_method(mNativeSupport, "switch_user", switch_user, 3);
	
	/* The maximum length of a Unix socket path, including terminating null. */
	rb_define_const(mNativeSupport, "UNIX_PATH_MAX", INT2NUM(sizeof(addr.sun_path)));
}
