/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_HANDSHAKE_WORKDIR_H_
#define _PASSENGER_SPAWNING_KIT_HANDSHAKE_WORKDIR_H_

#include <oxt/system_calls.hpp>
#include <string>
#include <cerrno>

#include <sys/types.h>
#include <limits.h>
#include <unistd.h>

#include <Exceptions.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;


/**
 * A temporary directory for handshaking with a child process
 * during spawning. It is removed after spawning is finished
 * or has failed.
 */
class HandshakeWorkDir {
private:
	string path;

public:
	HandshakeWorkDir(uid_t uid, gid_t gid) {
		char buf[PATH_MAX] = "/tmp/passenger.spawn.XXXXXXXXXX";
		const char *result = mkdtemp(buf);
		if (result == NULL) {
			int e = errno;
			throw SystemException("Cannot create a temporary directory "
				"in the format of '/tmp/passenger.spawn.XXX'", e);
		} else {
			path = result;
			boost::this_thread::disable_interruption di;
			boost::this_thread::disable_syscall_interruption dsi;
			syscalls::chown(result, uid, gid);
		}
	}

	~HandshakeWorkDir() {
		removeDirTree(path);
	}

	const string &getPath() const {
		return path;
	}
};


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_HANDSHAKE_WORKDIR_H_ */
