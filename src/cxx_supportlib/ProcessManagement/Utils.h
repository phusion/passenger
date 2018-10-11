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
#ifndef _PASSENGER_PROCESS_MANAGEMENT_UTILS_H_
#define _PASSENGER_PROCESS_MANAGEMENT_UTILS_H_

#include <sys/types.h>

namespace Passenger {

using namespace std;


/**
 * Thread-safe and async-signal safe way to fork().
 *
 * On Linux, the fork() glibc wrapper grabs a ptmalloc lock, so
 * if malloc causes a segfault then we can't fork.
 * http://sourceware.org/bugzilla/show_bug.cgi?id=4737
 *
 * OS X apparently does something similar, except they use a
 * spinlock so it results in 100% CPU. See _cthread_fork_prepare()
 * at http://www.opensource.apple.com/source/Libc/Libc-166/threads.subproj/cthreads.c
 * However, since POSIX in OS X is implemented on top of a Mach layer,
 * calling asyncFork() can mess up the state of the Mach layer, causing
 * some POSIX functions to mysteriously fail. See
 * https://code.google.com/p/phusion-passenger/issues/detail?id=1094
 * You should therefore not use asyncFork() unless you're in a signal
 * handler or if you only perform async-signal-safe stuff in the child.
 *
 * On 2017 October 9 with macOS 10.11 El Capitan, we also confirmed
 * a case in which the child process can get stuck indefinitely with 0% CPU.
 * If we we create a thread which performs memory allocation, and shortly
 * after thread creation we fork, then the child process gets stuck because
 * one of its pthread_atfork() handlers tries to allocate memory, which tries
 * to grab a lock which was already locked. This means that on macOS
 * we pretty much can never use regular fork() at all in a multithreaded
 * environment.
 *
 * As of 2018 May 16 with macOS 10.13 High Sierra, it was confirmed that the
 * use of asyncFork() can lead to the following messages to be printed
 * if the child process allocates memory:
 *
 *   malloc: *** mach_vm_map(size=1048576) failed (error code=268435459)
 *   malloc: *** error: can't allocate region securely
 *   malloc: *** set a breakpoint in malloc_error_break to debug
 *
 * See https://github.com/phusion/passenger/issues/1193#issuecomment-389503928
 */
pid_t asyncFork();

/**
 * Resets the current process's signal handler disposition and signal mask
 * to default values. One should call this every time one forks a child process;
 * non-default signal masks/handler dispositions can cause all kinds of weird quirks,
 * like waitpid() malfunctioning on macOS.
 *
 * This function is async-signal safe.
 */
void resetSignalHandlersAndMask();

/**
 * Disables malloc() debugging facilities on macOS.
 */
void disableMallocDebugging();

/**
 * Close all file descriptors that are higher than `lastToKeepOpen`.
 *
 * If you set `asyncSignalSafe` to true, then this function becomes fully async-signal,
 * through the use of asyncFork() instead of fork(). However, read the documentation
 * for asyncFork() to learn about its caveats.
 *
 * Also, regardless of whether `asyncSignalSafe` is true or not, this function is not
 * *thread* safe. Make sure there are no other threads running that might open file
 * descriptors, otherwise some file descriptors might not be closed even though they
 * should be.
 */
void closeAllFileDescriptors(int lastToKeepOpen, bool asyncSignalSafe = false);

/**
 * Given a failed exec() syscall and its resulting errno
 * value, print an appropriate error message to STDERR.
 *
 * This function is async signal-safe. Its main intended use is to be the
 * default value for the `onExecFail` parameter for the
 * `runCommand()` and `runCommandAndCaptureOutput()` functions.
 */
void printExecError(const char **command, int errcode);
void printExecError2(const char **command, int errcode, char *buf, size_t size);


} // namespace Passenger

#endif /* _PASSENGER_PROCESS_MANAGEMENT_UTILS_H_ */
