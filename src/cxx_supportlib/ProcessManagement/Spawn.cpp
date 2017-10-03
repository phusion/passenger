/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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

#include <sys/resource.h> // get/setpriority
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <string>
#include <cerrno>

#include <ProcessManagement/Spawn.h>
#include <ProcessManagement/Utils.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <Utils/IOUtils.h>

namespace Passenger {

using namespace std;


int
runShellCommand(const StaticString &command) {
	pid_t pid = fork();
	if (pid == 0) {
		resetSignalHandlersAndMask();
		disableMallocDebugging();
		closeAllFileDescriptors(2);
		execlp("/bin/sh", "/bin/sh", "-c", command.data(), (char * const) 0);
		_exit(1);
	} else if (pid == -1) {
		return -1;
	} else {
		int status;
		if (waitpid(pid, &status, 0) == -1) {
			return -1;
		} else {
			return status;
		}
	}
}

string
runCommandAndCaptureOutput(const char **command, int *status) {
	pid_t pid, waitRet;
	int e, waitStatus;
	Pipe p;

	p = createPipe(__FILE__, __LINE__);

	boost::this_thread::disable_syscall_interruption dsi;
	pid = syscalls::fork();
	if (pid == 0) {
		// Make ps nicer, we want to have as little impact on the rest
		// of the system as possible while collecting the metrics.
		int prio = getpriority(PRIO_PROCESS, getpid());
		prio++;
		if (prio > 20) {
			prio = 20;
		}
		setpriority(PRIO_PROCESS, getpid(), prio);

		dup2(p[1], 1);
		close(p[0]);
		close(p[1]);
		closeAllFileDescriptors(2);
		execvp(command[0], (char * const *) command);
		_exit(1);
	} else if (pid == -1) {
		e = errno;
		throw SystemException("Cannot fork() a new process", e);
	} else {
		bool done = false;
		string result;

		p[1].close();
		while (!done) {
			char buf[1024 * 4];
			ssize_t ret;

			try {
				boost::this_thread::restore_syscall_interruption rsi(dsi);
				ret = syscalls::read(p[0], buf, sizeof(buf));
			} catch (const thread_interrupted &) {
				syscalls::kill(SIGKILL, pid);
				syscalls::waitpid(pid, NULL, 0);
				throw;
			}
			if (ret == -1) {
				e = errno;
				syscalls::kill(SIGKILL, pid);
				syscalls::waitpid(pid, NULL, 0);
				throw SystemException(string("Cannot read output from the '") +
					command[0] + "' command", e);
			}
			done = ret == 0;
			result.append(buf, ret);
		}
		p[0].close();

		waitRet = syscalls::waitpid(pid, &waitStatus, 0);
		if (waitRet != -1) {
			if (status != NULL) {
				*status = waitStatus;
			}
		} else if (errno == ECHILD || errno == ESRCH) {
			if (status != NULL) {
				*status = -1;
			}
		} else {
			int e = errno;
			throw SystemException(string("Error waiting for the '") +
				command[0] + "' command", e);
		}
		return result;
	}
}


} // namespace Passenger
