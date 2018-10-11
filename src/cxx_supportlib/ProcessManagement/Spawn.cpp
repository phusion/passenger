/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <string>
#include <algorithm>
#include <cerrno>

#include <ProcessManagement/Spawn.h>
#include <ProcessManagement/Utils.h>
#include <StaticString.h>
#include <Exceptions.h>
#include <IOTools/IOUtils.h>

namespace Passenger {

using namespace std;


int
runShellCommand(const StaticString &command) {
	string commandNt = command;
	const char *argv[] = {
		"/bin/sh",
		"-c",
		commandNt.c_str(),
		NULL
	};
	SubprocessInfo info;
	runCommand(argv, info);
	return info.status;
}

void
runCommand(const char **command, SubprocessInfo &info, bool wait, bool killSubprocessOnInterruption,
	const boost::function<void ()> &afterFork,
	const boost::function<void (const char **, int errcode)> &onExecFail)
{
	int e, waitStatus;
	pid_t waitRet;

	info.pid = syscalls::fork();
	if (info.pid == 0) {
		resetSignalHandlersAndMask();
		disableMallocDebugging();
		if (afterFork) {
			afterFork();
		}
		closeAllFileDescriptors(2);
		execvp(command[0], (char * const *) command);
		if (onExecFail) {
			onExecFail(command, errno);
		}
		_exit(1);
	} else if (info.pid == -1) {
		e = errno;
		throw SystemException("Cannot fork() a new process", e);
	} else if (wait) {
		try {
			waitRet = syscalls::waitpid(info.pid, &waitStatus, 0);
		} catch (const boost::thread_interrupted &) {
			if (killSubprocessOnInterruption) {
				boost::this_thread::disable_syscall_interruption dsi;
				syscalls::kill(SIGKILL, info.pid);
				syscalls::waitpid(info.pid, NULL, 0);
			}
			throw;
		}

		if (waitRet != -1) {
			info.status = waitStatus;
		} else if (errno == ECHILD || errno == ESRCH) {
			info.status = -2;
		} else {
			int e = errno;
			throw SystemException(string("Error waiting for the '") +
				command[0] + "' command", e);
		}
	}
}

void
runCommandAndCaptureOutput(const char **command, SubprocessInfo &info,
	SubprocessOutput &output, size_t maxSize, bool killSubprocessOnInterruption,
	const boost::function<void ()> &afterFork,
	const boost::function<void (const char **command, int errcode)> &onExecFail)
{
	pid_t waitRet;
	int e, waitStatus;
	Pipe p;

	p = createPipe(__FILE__, __LINE__);

	info.pid = syscalls::fork();
	if (info.pid == 0) {
		dup2(p[1], 1);
		close(p[0]);
		close(p[1]);
		resetSignalHandlersAndMask();
		disableMallocDebugging();
		if (afterFork) {
			afterFork();
		}
		closeAllFileDescriptors(2);
		execvp(command[0], (char * const *) command);
		if (onExecFail) {
			onExecFail(command, errno);
		}
		_exit(1);
	} else if (info.pid == -1) {
		e = errno;
		throw SystemException("Cannot fork() a new process", e);
	} else {
		size_t totalRead = 0;

		output.eof = false;
		p[1].close();
		while (totalRead < maxSize) {
			char buf[1024 * 4];
			ssize_t ret;

			try {
				ret = syscalls::read(p[0], buf,
					std::min<size_t>(sizeof(buf), maxSize - totalRead));
			} catch (const boost::thread_interrupted &) {
				if (killSubprocessOnInterruption) {
					boost::this_thread::disable_syscall_interruption dsi;
					syscalls::kill(SIGKILL, info.pid);
					syscalls::waitpid(info.pid, NULL, 0);
				}
				throw;
			}
			if (ret == -1) {
				e = errno;
				if (killSubprocessOnInterruption) {
					boost::this_thread::disable_syscall_interruption dsi;
					syscalls::kill(SIGKILL, info.pid);
					syscalls::waitpid(info.pid, NULL, 0);
				}
				throw SystemException(string("Cannot read output from the '") +
					command[0] + "' command", e);
			} else if (ret == 0) {
				output.eof = true;
				break;
			} else {
				totalRead += ret;
				output.data.append(buf, ret);
			}
		}
		p[0].close();

		try {
			waitRet = syscalls::waitpid(info.pid, &waitStatus, 0);
		} catch (const boost::thread_interrupted &) {
			if (killSubprocessOnInterruption) {
				boost::this_thread::disable_syscall_interruption dsi;
				syscalls::kill(SIGKILL, info.pid);
				syscalls::waitpid(info.pid, NULL, 0);
			}
			throw;
		}

		if (waitRet != -1) {
			info.status = waitStatus;
		} else if (errno == ECHILD || errno == ESRCH) {
			info.status = -2;
		} else {
			int e = errno;
			throw SystemException(string("Error waiting for the '") +
				command[0] + "' command", e);
		}
	}
}


} // namespace Passenger
