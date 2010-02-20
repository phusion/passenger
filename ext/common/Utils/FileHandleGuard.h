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
#ifndef _PASSENGER_FILE_HANDLE_GUARD_H_
#define _PASSENGER_FILE_HANDLE_GUARD_H_

#include <boost/noncopyable.hpp>
#include <oxt/system_calls.hpp>
#include <cstdio>

namespace Passenger {

using namespace oxt;


/**
 * Simple scope-level guard object for ensuring that a given FILE handle
 * or file descriptor is closed when the object goes out of scope.
 */
class FileHandleGuard: public boost::noncopyable {
private:
	enum { T_FILE, T_FD } type;
	union {
		FILE *file;
		int fd;
	} u;
public:
	FileHandleGuard(FILE *file) {
		type = T_FILE;
		u.file = file;
	}
	
	FileHandleGuard(int fd) {
		type = T_FD;
		u.fd = fd;
	}
	
	void close() {
		if (type == T_FILE && u.file != NULL) {
			syscalls::fclose(u.file);
			u.file = NULL;
		} else if (type == T_FD && u.fd != -1) {
			syscalls::close(u.fd);
			u.fd = -1;
		}
	}
	
	~FileHandleGuard() {
		this_thread::disable_syscall_interruption dsi;
		close();
	}
};


} // namespace Passenger

#endif /* _PASSENGER_FILE_HANDLE_GUARD_H_ */
