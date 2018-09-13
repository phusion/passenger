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
#ifndef _PASSENGER_PROCESS_MANAGEMENT_SPAWN_H_
#define _PASSENGER_PROCESS_MANAGEMENT_SPAWN_H_

#include <sys/types.h>
#include <boost/function.hpp>
#include <string>
#include <StaticString.h>

namespace Passenger {

using namespace std;

struct SubprocessInfo {
	/**
	 * The PID of the subprocess. This is set to -1 on
	 * object creation. If fork fails or is interrupted,
	 * then this field is unmodified.
	 *
	 * Attention: if you called `runCommand()` with `wait = true`,
	 * or if you called `runCommandAndCaptureOutput()`,
	 * then when that function returns, this PID no longer
	 * exists.
	 */
	pid_t pid;

	/**
	 * The status of the subprocess, as returned by waitpid().
	 * This is set to -1 on object creation.
	 *
	 * Only if `runCommand()` is done waiting for the subprocess
	 * will this field be set. So if you call `runCommand()` with
	 * `wait = false` then this field will never be modified.
	 *
	 * When unable to waitpid() the subprocess because of
	 * an ECHILD or ESRCH, then this field is set to -2.
	 */
	int status;

	SubprocessInfo()
		: pid(-1),
		  status(-1)
		{ }
};

struct SubprocessOutput {
	/**
	 * The read subprocess output data.
	 */
	string data;

	/**
	 * Whether the entire file has been read. If false, then it
	 * means there is more data than specified through the `maxSize`
	 * parameter.
	 */
	bool eof;

	SubprocessOutput()
		: eof(false)
		{ }
};


// See ProcessManagement/Utils.h for definition
void printExecError(const char **command, int errcode);

/**
 * Like system(), but properly resets the signal handler mask,
 * disables malloc debugging and closes file descriptors > 2.
 *
 * This is like `runCommand()` but runs something through the shell.
 *
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
int runShellCommand(const StaticString &command);

/**
 * Run a command and (if so configured) wait for it. You can see this function
 * as a more flexible version of system(): it accepts a command array
 * instead of a shell command string, and you can choose whether to wait
 * for the subprocess or not.
 *
 * In addition, this function also properly resets the signal handler mask,
 * disables malloc debugging and closes file descriptors > 2.
 *
 * Information about the subprocess is stored inside `info`. See the comments
 * for the `SubprocessInfo` structure to learn more about it.
 *
 * If this function encounters an error or is interrupted, then it ensures
 * that as much information as possible about the current state of things
 * is stored in `info` so that the caller can clean things up appropriately.
 *
 * @param command The argument array to pass to execvp(). Must be null-terminated.
 * @param info
 * @param wait Whether to wait for the subprocess before returning.
 * @param killSubprocessOnInterruption Whether to automatically kill the subprocess
 *   when this function is interrupted.
 * @param afterFork A function object to be called right after forking.
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
void runCommand(const char **command, SubprocessInfo &info,
	bool wait = true, bool killSubprocessOnInterruption = true,
	const boost::function<void ()> &afterFork = boost::function<void ()>(),
	const boost::function<void (const char **command, int errcode)> &onExecFail = printExecError);

/**
 * Run a command, wait for it, and capture its stdout output.
 * This function does not care whether the command fails.
 *
 * In addition (like `runCommand()`), this function also properly
 * resets the signal handler mask, disables malloc debugging and
 * closes file descriptors > 2.
 *
 * If something goes wrong or when interrupted while capturing the
 * output, then `output` contains the output captured so far.
 *
 * Information about the subprocess is stored inside `info`. See the comments
 * for the `SubprocessInfo` structure to learn more about it.
 *
 * If this function encounters an error or is interrupted, then it ensures
 * that as much information as possible about the current state of things
 * is stored in `info` so that the caller can clean things up appropriately.
 *
 * @param command The argument array to pass to execvp(). Must be null-terminated.
 * @param info
 * @param maxSize The maximum number of output bytes to read.
 * @param killSubprocessOnInterruption Whether to automatically kill the subprocess
 *   when this function is interrupted.
 * @param afterFork A function object to be called right after forking.
 * @param onExecFail A function object to be called if exec fails.
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
void runCommandAndCaptureOutput(const char **command, SubprocessInfo &info,
	SubprocessOutput &output, size_t maxSize, bool killSubprocessOnInterruption = true,
	const boost::function<void ()> &afterFork = boost::function<void ()>(),
	const boost::function<void (const char **command, int errcode)> &onExecFail = printExecError);


} // namespace Passenger

#endif /* _PASSENGER_PROCESS_MANAGEMENT_SPAWN_H_ */
