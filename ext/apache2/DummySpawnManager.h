/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_DUMMY_SPAWN_MANAGER_H_
#define _PASSENGER_DUMMY_SPAWN_MANAGER_H_

// TODO: make this path not hardcoded
#define DUMMY_REQUEST_HANDLER_EXECUTABLE "/home/hongli/Projects/passenger/benchmark/DummyRequestHandler"

#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <cstdio>
#include <unistd.h>
#include <errno.h>

#include "Application.h"
#include "Exceptions.h"

namespace Passenger {

using namespace std;

/**
 * A dummy SpawnManager replacement for testing/debugging purposes.
 *
 * This class implements a dummy spawn manager, and is 100% interface-compatible with
 * SpawnManager. This spawn manager will spawn <tt>benchmark/DummyRequestHandler</tt>,
 * which is probably the fastest possible implementation of a request handler. The purpose
 * of this class to benchmark the performance of the Apache module (i.e. not benchmarking
 * the Ruby request handler or Rails itself).
 *
 * This header file is not used by default. Define the macro <tt>PASSENGER_USE_DUMMY_SPAWN_MANAGER</tt>
 * to make ApplicationPool use DummySpawnManager instead of SpawnManager.
 *
 * Of course, don't forget to compile benchmark/DummyRequestHandler!
 *
 * @ingroup Support
 */
class DummySpawnManager {
public:
	ApplicationPtr spawn(const string &appRoot, const string &user = "", const string &group = "") {
		int fds[2];
		pid_t pid;
		
		if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket", errno);
		}
		pid = fork();
		if (pid == 0) {
			pid = fork();
			if (pid == 0) {
				dup2(fds[0], STDIN_FILENO);
				close(fds[0]);
				close(fds[1]);
				execlp(DUMMY_REQUEST_HANDLER_EXECUTABLE, DUMMY_REQUEST_HANDLER_EXECUTABLE, NULL);
				int e = errno;
				fprintf(stderr, "Unable to run %s: %s\n", DUMMY_REQUEST_HANDLER_EXECUTABLE, strerror(e));
				fflush(stderr);
				_exit(1);
			} else if (pid == -1) {
				perror("Cannot fork a new process");
				fflush(stderr);
				_exit(1);
			} else {
				_exit(0);
			}
		} else if (pid == -1) {
			close(fds[0]);
			close(fds[1]);
			throw SystemException("Cannot fork a new process", errno);
		} else {
			close(fds[0]);
			waitpid(pid, NULL, 0);
			return ApplicationPtr(new Application(appRoot, pid, fds[1]));
		}
	}
	
	pid_t getServerPid() const {
		return 0;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_DUMMY_SPAWN_MANAGER_H_ */
