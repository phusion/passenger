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
