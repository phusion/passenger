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
#ifdef HAVE_RUBY_IO_H
	/* Ruby 1.9 */
	#include "ruby/intern.h"
	#include "ruby/io.h"
#else
	#include "rubysig.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <grp.h>
#include <signal.h>
#ifdef HAVE_ALLOCA_H
	#include <alloca.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
	#define HAVE_KQUEUE
	#include <pthread.h>
	#include <sys/event.h>
	#include <sys/time.h>
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
#if !defined(RUBY_UBF_IO) && defined(RB_UBF_DFL)
	/* MacRuby compatibility */
	#define RUBY_UBF_IO RB_UBF_DFL
#endif
#ifndef IOV_MAX
	/* Linux doesn't define IOV_MAX in limits.h for some reason. */
	#define IOV_MAX sysconf(_SC_IOV_MAX)
#endif

static VALUE mPassenger;
static VALUE mNativeSupport;
static VALUE S_ProcessTimes;
#ifdef HAVE_KQUEUE
	static VALUE cFileSystemWatcher;
#endif

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
	const char *filename_str;
	long filename_length;
	
	filename_str = RSTRING_PTR(filename);
	filename_length = RSTRING_LEN(filename);
	
	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		rb_sys_fail("Cannot create a Unix socket");
		return Qnil;
	}
	
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, filename_str,
		MIN((long) filename_length, (long) sizeof(addr.sun_path)));
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
			close((int) i);
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
		if (counter == (size_t) bytes_written) {
			/* Found. In fact, all vectors up to this one contain exactly
			 * bytes_written bytes. So discard all these vectors.
			 */
			group->io_vectors += i + 1;
			group->count -= i + 1;
			group->total_size -= bytes_written;
			return;
		} else if (counter > (size_t) bytes_written) {
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

#ifndef TRAP_BEG
	typedef struct {
		int filedes;
		const struct iovec *iov;
		int iovcnt;
	} WritevWrapperData;
	
	static VALUE
	writev_wrapper(void *ptr) {
		WritevWrapperData *data = (WritevWrapperData *) ptr;
		return (VALUE) writev(data->filedes, data->iov, data->iovcnt);
	}
#endif

static VALUE
f_generic_writev(VALUE fd, VALUE *array_of_components, unsigned int count) {
	VALUE components, str;
	unsigned int total_size, total_components, ngroups;
	IOVectorGroup *groups;
	unsigned int i, j, group_offset, vector_offset;
	unsigned long long ssize_max;
	ssize_t ret;
	int done, fd_num, e;
	#ifndef TRAP_BEG
		WritevWrapperData writev_wrapper_data;
	#endif
	
	/* First determine the number of components that we have. */
	total_components   = 0;
	for (i = 0; i < count; i++) {
	        Check_Type(array_of_components[i], T_ARRAY);
		total_components += (unsigned int) RARRAY_LEN(array_of_components[i]);
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
		for (j = 0; j < (unsigned int) RARRAY_LEN(components); j++) {
			str = rb_ary_entry(components, j);
			str = rb_obj_as_string(str);
			total_size += (unsigned int) RSTRING_LEN(str);
			/* I know writev() doesn't write to iov_base, but on some
			 * platforms it's still defined as non-const char *
			 * :-(
			 */
			groups[group_offset].io_vectors[vector_offset].iov_base = (char *) RSTRING_PTR(str);
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
			#ifdef TRAP_BEG
				TRAP_BEG;
				ret = writev(fd_num, groups[i].io_vectors, groups[i].count);
				TRAP_END;
			#else
				writev_wrapper_data.filedes = fd_num;
				writev_wrapper_data.iov     = groups[i].io_vectors;
				writev_wrapper_data.iovcnt  = groups[i].count;
				ret = (int) rb_thread_blocking_region(writev_wrapper,
					&writev_wrapper_data, RUBY_UBF_IO, 0);
			#endif
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
	uid_t the_uid = (uid_t) NUM2LL(uid);
	gid_t the_gid = (gid_t) NUM2LL(gid);
	
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

static VALUE
process_times(VALUE self) {
	struct rusage usage;
	unsigned long long utime, stime;
	
	if (getrusage(RUSAGE_SELF, &usage) == -1) {
		rb_sys_fail("getrusage()");
	}
	
	utime = (unsigned long long) usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
	stime = (unsigned long long) usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
	return rb_struct_new(S_ProcessTimes, rb_ull2inum(utime), rb_ull2inum(stime));
}

#if defined(HAVE_KQUEUE) || defined(IN_DOXYGEN)
typedef struct {
	VALUE klass;
	VALUE filenames;
	VALUE termination_pipe;
	
	/* File descriptor of termination_pipe. */
	int termination_fd;
	
	/* Whether something went wrong during initialization. */
	int preparation_error;
	
	/* Information for kqueue. */
	unsigned int events_len;
	int *fds;
	unsigned int fds_len;
	int kq;
	
	/* When the watcher thread is done it'll write to this pipe
	 * to signal the main (Ruby) thread.
	 */
	int notification_fd[2];
	
	/* When the main (Ruby) thread is interrupted it'll write to
	 * this pipe to tell the watcher thread to exit.
	 */
	int interruption_fd[2];
} FSWatcher;

typedef struct {
	int fd;
	ssize_t ret;
	char byte;
	int error;
} FSWatcherReadByteData;

static void
fs_watcher_real_close(FSWatcher *watcher) {
	unsigned int i;
	
	if (watcher->kq != -1) {
		close(watcher->kq);
		watcher->kq = -1;
	}
	if (watcher->notification_fd[0] != -1) {
		close(watcher->notification_fd[0]);
		watcher->notification_fd[0] = -1;
	}
	if (watcher->notification_fd[1] != -1) {
		close(watcher->notification_fd[1]);
		watcher->notification_fd[1] = -1;
	}
	if (watcher->interruption_fd[0] != -1) {
		close(watcher->interruption_fd[0]);
		watcher->interruption_fd[0] = -1;
	}
	if (watcher->interruption_fd[1] != -1) {
		close(watcher->interruption_fd[1]);
		watcher->interruption_fd[1] = -1;
	}
	if (watcher->fds != NULL) {
		for (i = 0; i < watcher->fds_len; i++) {
			close(watcher->fds[i]);
		}
		free(watcher->fds);
		watcher->fds = NULL;
		watcher->fds_len = 0;
	}
}

static void
fs_watcher_free(void *obj) {
	FSWatcher *watcher = (FSWatcher *) obj;
	fs_watcher_real_close(watcher);
	free(watcher);
}

static VALUE
fs_watcher_init(VALUE arg) {
	FSWatcher *watcher = (FSWatcher *) arg;
	struct kevent *events;
	VALUE filename;
	unsigned int i;
	uint32_t fflags;
	VALUE filenum;
	struct stat buf;
	int fd;
	
	/* Open each file in the filenames list and add each one to the events array. */
	
	/* +2 for the termination pipe and the interruption pipe. */
	events = alloca((RARRAY_LEN(watcher->filenames) + 2) * sizeof(struct kevent));
	watcher->fds = malloc(RARRAY_LEN(watcher->filenames) * sizeof(int));
	if (watcher->fds == NULL) {
		rb_raise(rb_eNoMemError, "Cannot allocate memory.");
		return Qnil;
	}
	for (i = 0; i < RARRAY_LEN(watcher->filenames); i++) {
		filename = rb_ary_entry(watcher->filenames, i);
		if (TYPE(filename) != T_STRING) {
			filename = rb_obj_as_string(filename);
		}
		
		if (stat(RSTRING_PTR(filename), &buf) == -1) {
			watcher->preparation_error = 1;
			goto end;
		}
		
		#ifdef O_EVTONLY
			fd = open(RSTRING_PTR(filename), O_EVTONLY);
		#else
			fd = open(RSTRING_PTR(filename), O_RDONLY);
		#endif
		if (fd == -1) {
			watcher->preparation_error = 1;
			goto end;
		}
		
		watcher->fds[i] = fd;
		watcher->fds_len++;
		fflags = NOTE_WRITE | NOTE_EXTEND | NOTE_RENAME | NOTE_DELETE | NOTE_REVOKE;
		EV_SET(&events[i], fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
			fflags, 0, 0);
	}
	
	watcher->events_len = watcher->fds_len;
	
	/* Create pipes for inter-thread communication. */
	
	if (pipe(watcher->notification_fd) == -1) {
		rb_sys_fail("pipe()");
		return Qnil;
	}
	if (pipe(watcher->interruption_fd) == -1) {
		rb_sys_fail("pipe()");
		return Qnil;
	}
	
	/* Create a kqueue and register all events. */
	
	watcher->kq = kqueue();
	if (watcher->kq == -1) {
		rb_sys_fail("kqueue()");
		return Qnil;
	}
	
	if (watcher->termination_pipe != Qnil) {
		filenum = rb_funcall(watcher->termination_pipe,
			rb_intern("fileno"), 0);
		EV_SET(&events[watcher->events_len], NUM2INT(filenum),
			EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, 0);
		watcher->termination_fd = NUM2INT(filenum);
		watcher->events_len++;
	}
	EV_SET(&events[watcher->events_len], watcher->interruption_fd[0],
		EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, 0);
	watcher->events_len++;
	
	if (kevent(watcher->kq, events, watcher->events_len, NULL, 0, NULL) == -1) {
		rb_sys_fail("kevent()");
		return Qnil;
	}
	
end:
	if (watcher->preparation_error) {
		for (i = 0; i < watcher->fds_len; i++) {
			close(watcher->fds[i]);
		}
		free(watcher->fds);
		watcher->fds = NULL;
		watcher->fds_len = 0;
	}
	return Data_Wrap_Struct(watcher->klass, NULL, fs_watcher_free, watcher);
}

static VALUE
fs_watcher_new(VALUE klass, VALUE filenames, VALUE termination_pipe) {
	FSWatcher *watcher;
	VALUE result;
	int status;
	
	Check_Type(filenames, T_ARRAY);
	watcher = (FSWatcher *) calloc(1, sizeof(FSWatcher));
	if (watcher == NULL) {
		rb_raise(rb_eNoMemError, "Cannot allocate memory.");
		return Qnil;
	}
	watcher->klass = klass;
	watcher->filenames = filenames;
	watcher->termination_pipe = termination_pipe;
	watcher->termination_fd = -1;
	watcher->kq = -1;
	watcher->notification_fd[0] = -1;
	watcher->notification_fd[1] = -1;
	watcher->interruption_fd[0] = -1;
	watcher->interruption_fd[1] = -1;
	
	result = rb_protect(fs_watcher_init, (VALUE) watcher, &status);
	if (status) {
		fs_watcher_free(watcher);
		rb_jump_tag(status);
	} else {
		return result;
	}
}

static void *
fs_watcher_wait_on_kqueue(void *arg) {
	FSWatcher *watcher = (FSWatcher *) arg;
	struct kevent *events;
	int nevents;
	ssize_t ret;
	
	events = alloca(sizeof(struct kevent) * watcher->events_len);
	nevents = kevent(watcher->kq, NULL, 0, events, watcher->events_len, NULL);
	if (nevents == -1) {
		ret = write(watcher->notification_fd[1], "e", 1);
	} else if (nevents >= 1 && (
		   events[0].ident == (uintptr_t) watcher->termination_fd
		|| events[0].ident == (uintptr_t) watcher->interruption_fd[0]
	)) {
		ret = write(watcher->notification_fd[1], "t", 1);
	} else {
		ret = write(watcher->notification_fd[1], "f", 1);
	}
	if (ret == -1) {
		close(watcher->notification_fd[1]);
		watcher->notification_fd[1] = -1;
	}
	return NULL;
}

static VALUE
fs_watcher_wait_fd(VALUE _fd) {
	int fd = (int) _fd;
	rb_thread_wait_fd(fd);
	return Qnil;
}

#ifndef TRAP_BEG
	static VALUE
	fs_watcher_read_byte_from_fd_wrapper(void *_arg) {
		FSWatcherReadByteData *data = (FSWatcherReadByteData *) _arg;
		data->ret = read(data->fd, &data->byte, 1);
		data->error = errno;
		return Qnil;
	}
#endif

static VALUE
fs_watcher_read_byte_from_fd(VALUE _arg) {
	FSWatcherReadByteData *data = (FSWatcherReadByteData *) _arg;
	#ifdef TRAP_BEG
		TRAP_BEG;
		data->ret = read(data->fd, &data->byte, 1);
		TRAP_END;
		data->error = errno;
	#else
		rb_thread_blocking_region(fs_watcher_read_byte_from_fd_wrapper,
			data, RUBY_UBF_IO, 0);
	#endif
	return Qnil;
}

static VALUE
fs_watcher_wait_for_change(VALUE self) {
	FSWatcher *watcher;
	pthread_t thr;
	ssize_t ret;
	int e, interrupted = 0;
	FSWatcherReadByteData read_data;
	
	Data_Get_Struct(self, FSWatcher, watcher);
	
	if (watcher->preparation_error) {
		return Qfalse;
	}
	
	/* Spawn a thread, and let the thread perform the blocking kqueue
	 * wait. When kevent() returns the thread will write its status to the
	 * notification pipe. In the mean time we let the Ruby interpreter wait
	 * on the other side of the pipe for us so that we don't block Ruby
	 * threads.
	 */
	
	e = pthread_create(&thr, NULL, fs_watcher_wait_on_kqueue, watcher);
	if (e != 0) {
		errno = e;
		rb_sys_fail("pthread_create()");
		return Qnil;
	}
	
	/* Note that rb_thread_wait() does not wait for the fd when the app
	 * is single threaded, so we must join the thread after we've read
	 * from the notification fd.
	 */
	rb_protect(fs_watcher_wait_fd, (VALUE) watcher->notification_fd[0], &interrupted);
	if (interrupted) {
		/* We got interrupted so tell the watcher thread to exit. */
		ret = write(watcher->interruption_fd[1], "x", 1);
		if (ret == -1) {
			e = errno;
			fs_watcher_real_close(watcher);
			errno = e;
			rb_sys_fail("write() to interruption pipe");
			return Qnil;
		}
		pthread_join(thr, NULL);
		
		/* Now clean up stuff. */
		fs_watcher_real_close(watcher);
		rb_jump_tag(interrupted);
		return Qnil;
	}
	
	read_data.fd = watcher->notification_fd[0];
	rb_protect(fs_watcher_read_byte_from_fd, (VALUE) &read_data, &interrupted);
	if (interrupted) {
		/* We got interrupted so tell the watcher thread to exit. */
		ret = write(watcher->interruption_fd[1], "x", 1);
		if (ret == -1) {
			e = errno;
			fs_watcher_real_close(watcher);
			errno = e;
			rb_sys_fail("write() to interruption pipe");
			return Qnil;
		}
		pthread_join(thr, NULL);
		
		/* Now clean up stuff. */
		fs_watcher_real_close(watcher);
		rb_jump_tag(interrupted);
		return Qnil;
	}
	
	pthread_join(thr, NULL);
	
	if (read_data.ret == -1) {
		fs_watcher_real_close(watcher);
		errno = read_data.error;
		rb_sys_fail("read()");
		return Qnil;
	} else if (read_data.ret == 0) {
		fs_watcher_real_close(watcher);
		errno = read_data.error;
		rb_raise(rb_eRuntimeError, "Unknown error: unexpected EOF");
		return Qnil;
	} else if (read_data.byte == 't') {
		/* termination_fd or interruption_fd became readable */
		return Qnil;
	} else if (read_data.byte == 'f') {
		/* a file or directory changed */
		return Qtrue;
	} else {
		fs_watcher_real_close(watcher);
		errno = read_data.error;
		rb_raise(rb_eRuntimeError, "Unknown error: unexpected notification data");
		return Qnil;
	}
}

static VALUE
fs_watcher_close(VALUE self) {
	FSWatcher *watcher;
	Data_Get_Struct(self, FSWatcher, watcher);
	fs_watcher_real_close(watcher);
	return Qnil;
}
#endif


/***************************/

void
Init_passenger_native_support() {
	struct sockaddr_un addr;
	
	/* */
	mPassenger = rb_define_module("PhusionPassenger"); // Do not remove the above comment. We want the Passenger module's rdoc to be empty.
	
	/*
	 * Utility functions for accessing system functionality.
	 */
	mNativeSupport = rb_define_module_under(mPassenger, "NativeSupport");
	
	S_ProcessTimes = rb_struct_define("ProcessTimes", "utime", "stime", NULL);
	
	rb_define_singleton_method(mNativeSupport, "send_fd", send_fd, 2);
	rb_define_singleton_method(mNativeSupport, "recv_fd", recv_fd, 1);
	rb_define_singleton_method(mNativeSupport, "create_unix_socket", create_unix_socket, 2);
	rb_define_singleton_method(mNativeSupport, "close_all_file_descriptors", close_all_file_descriptors, 1);
	rb_define_singleton_method(mNativeSupport, "disable_stdio_buffering", disable_stdio_buffering, 0);
	rb_define_singleton_method(mNativeSupport, "split_by_null_into_hash", split_by_null_into_hash, 1);
	rb_define_singleton_method(mNativeSupport, "writev", f_writev, 2);
	rb_define_singleton_method(mNativeSupport, "writev2", f_writev2, 3);
	rb_define_singleton_method(mNativeSupport, "writev3", f_writev3, 4);
	rb_define_singleton_method(mNativeSupport, "switch_user", switch_user, 3);
	rb_define_singleton_method(mNativeSupport, "process_times", process_times, 0);
	
	#ifdef HAVE_KQUEUE
		cFileSystemWatcher = rb_define_class_under(mNativeSupport,
			"FileSystemWatcher", rb_cObject);
		rb_define_singleton_method(cFileSystemWatcher, "_new",
			fs_watcher_new, 2);
		rb_define_method(cFileSystemWatcher, "wait_for_change",
			fs_watcher_wait_for_change, 0);
		rb_define_method(cFileSystemWatcher, "close",
			fs_watcher_close, 0);
	#endif
	
	/* The maximum length of a Unix socket path, including terminating null. */
	rb_define_const(mNativeSupport, "UNIX_PATH_MAX", INT2NUM(sizeof(addr.sun_path)));
	/* The maximum size of the data that may be passed to #writev. */
	rb_define_const(mNativeSupport, "SSIZE_MAX", LL2NUM(SSIZE_MAX));
}
