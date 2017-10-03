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
#ifndef _PASSENGER_PROCESS_MANAGEMENT_SPAWN_H_
#define _PASSENGER_PROCESS_MANAGEMENT_SPAWN_H_

#include <string>
#include <StaticString.h>

namespace Passenger {

using namespace std;


/**
 * Like system(), but properly resets the signal handler mask,
 * disables malloc debugging and closes file descriptors > 2.
 * _command_ must be null-terminated.
 */
int runShellCommand(const StaticString &command);

/**
 * Run a command and capture its stdout output. This function
 * does not care whether the command fails.
 *
 * If something goes wrong or when interrupted while capturing the
 * output, then the child process is killed with SIGKILL before this
 * function returns.
 *
 * @param command The argument to pass to execvp(). Must be null-terminated.
 * @param status The status of the child process will be stored here, if non-NULL.
 *               When unable to waitpid() the child process because of an ECHILD
 *               or ESRCH, this will be set to -1.
 * @throws SystemException
 * @throws boost::thread_interrupted
 */
string runCommandAndCaptureOutput(const char **command, int *status = NULL);


} // namespace Passenger

#endif /* _PASSENGER_PROCESS_MANAGEMENT_SPAWN_H_ */
