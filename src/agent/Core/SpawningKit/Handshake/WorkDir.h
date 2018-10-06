/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

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
	HandshakeWorkDir() {
		char buf[PATH_MAX + 1];
		char *pos = buf;
		const char *end = buf + PATH_MAX;

		pos = appendData(pos, end, getSystemTempDir());
		pos = appendData(pos, end, "/passenger.spawn.XXXXXXXXXX");
		*pos = '\0';

		const char *result = mkdtemp(buf);
		if (result == NULL) {
			int e = errno;
			throw SystemException("Cannot create a temporary directory "
				"in the format of '" + StaticString(buf) + "'", e);
		} else {
			path = result;
		}
	}

	~HandshakeWorkDir() {
		if (!path.empty()) {
			removeDirTree(path);
		}
	}

	const string &getPath() const {
		return path;
	}

	void finalize(uid_t uid, gid_t gid) {
		finalize(path, uid, gid);
	}

	string dontRemoveOnDestruction() {
		string result = path;
		path.clear();
		return result;
	}

	static void finalize(const string &path, uid_t uid, gid_t gid) {
		// We do not chown() the work dir until:
		//
		//  - HandshakePrepare is done populating the work dir,
		//  - SpawnEnvSetupperMain is done reading from and modifying the work dir
		//
		// This way, the application user cannot perform symlink attacks
		// inside the work dir until we are done (at which point the
		// follow-up code will only perform read/write operations after
		// dropping root privileges).
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		syscalls::chown(path.c_str(), uid, gid);
	}
};


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_HANDSHAKE_WORKDIR_H_ */
