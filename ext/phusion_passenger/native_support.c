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
#include "ruby.h"
#include "rubysig.h"
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#ifdef HAVE_ALLOCA_H
	#include <alloca.h>
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
#ifndef IOV_MAX
	/* Linux doesn't define IOV_MAX in limits.h for some reason. */
	#define IOV_MAX sysconf(_SC_IOV_MAX)
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
disable_stdio_buffering(VALUE self) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	return Qnil;
}

/**
 * Split the given string into an hash. Keys and values are obtained by splitting the
 * string using the null character as the delimitor.
 */
static VALUE
split_by_null_into_hash(VALUE self, VALUE data) {
	const char *cdata   = RSTRING_PTR(data);
	unsigned long len   = RSTRING_LEN(data);
	const char *begin   = cdata;
	const char *current = cdata;
	const char *end     = cdata + len;
	VALUE result, key, value;
	
	result = rb_hash_new();
	while (current < end) {
		if (*current == '\0') {
			key   = rb_str_substr(data, begin - cdata, current - begin);
			begin = current = current + 1;
			while (current < end) {
				if (*current == '\0') {
					value = rb_str_substr(data, begin - cdata, current - begin);;
					begin = current = current + 1;
					rb_hash_aset(result, key, value);
					break;
				} else {
					current++;
				}
			}
		} else {
			current++;
		}
	}
	return result;
}

typedef struct {
	/* The IO vectors in this group. */
	struct iovec *io_vectors;
	
	/* The number of IO vectors in io_vectors. */
	unsigned int count;
	
	/* The combined size of all IO vectors in this group. */
	ssize_t      total_size;
} IOVectorGroup;

/* Given that _bytes_written_ bytes in _group_ had been successfully written,
 * update the information in _group_ so that the next writev() call doesn't
 * write the already written bytes.
 */
static void
update_group_written_info(IOVectorGroup *group, ssize_t bytes_written) {
	unsigned int i;
	size_t counter;
	struct iovec *current_vec;
	
	/* Find the last vector that contains data that had already been written. */
	counter = 0;
	for (i = 0; i < group->count; i++) {
		counter += group->io_vectors[i].iov_len;
		if (counter == bytes_written) {
			/* Found. In fact, all vectors up to this one contain exactly
			 * bytes_written bytes. So discard all these vectors.
			 */
			group->io_vectors += i + 1;
			group->count -= i + 1;
			group->total_size -= bytes_written;
			return;
		} else if (counter > bytes_written) {
			/* Found. Discard all vectors before this one, and
			 * truncate this vector.
			 */
			group->io_vectors += i;
			group->count -= i;
			group->total_size -= bytes_written;
			current_vec = &group->io_vectors[0];
			current_vec->iov_base = ((char *) current_vec->iov_base) +
				current_vec->iov_len - (counter - bytes_written);
			current_vec->iov_len = counter - bytes_written;
			return;
		}
	}
	rb_raise(rb_eRuntimeError, "writev() returned an unexpected result");
}

static VALUE
f_generic_writev(VALUE fd, VALUE *array_of_components, unsigned int count) {
	VALUE components, str;
	unsigned int total_size, total_components, ngroups;
	IOVectorGroup *groups;
	unsigned int i, j, group_offset, vector_offset;
	unsigned long long ssize_max;
	ssize_t ret;
	int done, fd_num, e;
	
	/* First determine the number of components that we have. */
	total_components   = 0;
	for (i = 0; i < count; i++) {
		total_components += RARRAY_LEN(array_of_components[i]);
	}
	if (total_components == 0) {
		return NUM2INT(0);
	}
	
	/* A single writev() call can only accept IOV_MAX vectors, so we
	 * may have to split the components into groups and perform
	 * multiple writev() calls, one per group. Determine the number
	 * of groups needed, how big each group should be and allocate
	 * memory for them.
	 */
	if (total_components % IOV_MAX == 0) {
		ngroups = total_components / IOV_MAX;
		groups  = alloca(ngroups * sizeof(IOVectorGroup));
		if (groups == NULL) {
			rb_raise(rb_eNoMemError, "Insufficient stack space.");
		}
		memset(groups, 0, ngroups * sizeof(IOVectorGroup));
		for (i = 0; i < ngroups; i++) {
			groups[i].io_vectors = alloca(IOV_MAX * sizeof(struct iovec));
			if (groups[i].io_vectors == NULL) {
				rb_raise(rb_eNoMemError, "Insufficient stack space.");
			}
			groups[i].count = IOV_MAX;
		}
	} else {
		ngroups = total_components / IOV_MAX + 1;
		groups  = alloca(ngroups * sizeof(IOVectorGroup));
		if (groups == NULL) {
			rb_raise(rb_eNoMemError, "Insufficient stack space.");
		}
		memset(groups, 0, ngroups * sizeof(IOVectorGroup));
		for (i = 0; i < ngroups - 1; i++) {
			groups[i].io_vectors = alloca(IOV_MAX * sizeof(struct iovec));
			if (groups[i].io_vectors == NULL) {
				rb_raise(rb_eNoMemError, "Insufficient stack space.");
			}
			groups[i].count = IOV_MAX;
		}
		groups[ngroups - 1].io_vectors = alloca((total_components % IOV_MAX) * sizeof(struct iovec));
		if (groups[ngroups - 1].io_vectors == NULL) {
			rb_raise(rb_eNoMemError, "Insufficient stack space.");
		}
		groups[ngroups - 1].count = total_components % IOV_MAX;
	}
	
	/* Now distribute the components among the groups, filling the iovec
	 * array in each group. Also calculate the total data size while we're
	 * at it.
	 */
	total_size    = 0;
	group_offset  = 0;
	vector_offset = 0;
	for (i = 0; i < count; i++) {
		components = array_of_components[i];
		for (j = 0; j < RARRAY_LEN(components); j++) {
			str = rb_ary_entry(components, j);
			str = rb_obj_as_string(str);
			total_size += RSTRING_LEN(str);
			groups[group_offset].io_vectors[vector_offset].iov_base = RSTRING_PTR(str);
			groups[group_offset].io_vectors[vector_offset].iov_len  = RSTRING_LEN(str);
			groups[group_offset].total_size += RSTRING_LEN(str);
			vector_offset++;
			if (vector_offset == groups[group_offset].count) {
				group_offset++;
				vector_offset = 0;
			}
		}
	}
	
	/* We don't compare to SSIZE_MAX directly in order to shut up a compiler warning on OS X Snow Leopard. */
	ssize_max = SSIZE_MAX;
	if (total_size > ssize_max) {
		rb_raise(rb_eArgError, "The total size of the components may not be larger than SSIZE_MAX.");
	}
	
	/* Write the data. */
	fd_num = NUM2INT(fd);
	for (i = 0; i < ngroups; i++) {
		/* Wait until the file descriptor becomes writable before writing things. */
		rb_thread_fd_writable(fd_num);
		
		done = 0;
		while (!done) {
			TRAP_BEG;
			ret = writev(fd_num, groups[i].io_vectors, groups[i].count);
			TRAP_END;
			if (ret == -1) {
				/* If the error is something like EAGAIN, yield to another
				 * thread until the file descriptor becomes writable again.
				 * In case of other errors, raise an exception.
				 */
				if (!rb_io_wait_writable(fd_num)) {
					rb_sys_fail("writev()");
				}
			} else if (ret < groups[i].total_size) {
				/* Not everything in this group has been written. Retry without
				 * writing the bytes that been successfully written.
				 */
				e = errno;
				update_group_written_info(&groups[i], ret);
				errno = e;
				rb_io_wait_writable(fd_num);
			} else {
				done = 1;
			}
		}
	}
	return INT2NUM(total_size);
}

/**
 * Writes all of the strings in the +components+ array into the given file
 * descriptor using the +writev()+ system call. Unlike IO#write, this method
 * does not require one to concatenate all those strings into a single buffer
 * in order to send the data in a single system call. Thus, #writev is a great
 * way to perform zero-copy I/O.
 *
 * Unlike the raw writev() system call, this method ensures that all given
 * data is written before returning, by performing multiple writev() calls
 * and whatever else is necessary.
 *
 *   writev(@socket.fileno, ["hello ", "world", "\n"])
 */
static VALUE
f_writev(VALUE self, VALUE fd, VALUE components) {
	return f_generic_writev(fd, &components, 1);
}

/**
 * Like #writev, but accepts two arrays. The data is written in the given order.
 *
 *   writev2(@socket.fileno, ["hello ", "world", "\n"], ["another ", "message\n"])
 */
static VALUE
f_writev2(VALUE self, VALUE fd, VALUE components1, VALUE components2) {
	VALUE array_of_components[2] = { components1, components2 };
	return f_generic_writev(fd, array_of_components, 2);
}

/**
 * Like #writev, but accepts three arrays. The data is written in the given order.
 *
 *   writev3(@socket.fileno,
 *     ["hello ", "world", "\n"],
 *     ["another ", "message\n"],
 *     ["yet ", "another ", "one", "\n"])
 */
static VALUE
f_writev3(VALUE self, VALUE fd, VALUE components1, VALUE components2, VALUE components3) {
	VALUE array_of_components[3] = { components1, components2, components3 };
	return f_generic_writev(fd, array_of_components, 3);
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
	rb_define_singleton_method(mNativeSupport, "split_by_null_into_hash", split_by_null_into_hash, 1);
	rb_define_singleton_method(mNativeSupport, "writev", f_writev, 2);
	rb_define_singleton_method(mNativeSupport, "writev2", f_writev2, 3);
	rb_define_singleton_method(mNativeSupport, "writev3", f_writev3, 4);
	rb_define_singleton_method(mNativeSupport, "switch_user", switch_user, 3);
	
	/* The maximum length of a Unix socket path, including terminating null. */
	rb_define_const(mNativeSupport, "UNIX_PATH_MAX", INT2NUM(sizeof(addr.sun_path)));
	/* The maximum size of the data that may be passed to #writev. */
	rb_define_const(mNativeSupport, "SSIZE_MAX", LL2NUM(SSIZE_MAX));
}
