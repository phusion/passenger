/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2013 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL2_DIRECT_SPAWNER_H_
#define _PASSENGER_APPLICATION_POOL2_DIRECT_SPAWNER_H_

#include <ApplicationPool2/Spawner.h>
#include <limits.h>  // for PTHREAD_STACK_MIN
#include <pthread.h>


namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class DirectSpawner: public Spawner {
private:
	static int startBackgroundThread(void *(*mainFunction)(void *), void *arg) {
		// Using raw pthread API because we don't want to register such
		// trivial threads on the oxt::thread list.
		pthread_t thr;
		pthread_attr_t attr;
		size_t stack_size = 96 * 1024;

		unsigned long min_stack_size;
		bool stack_min_size_defined;
		bool round_stack_size;
		int ret;

		#ifdef PTHREAD_STACK_MIN
			// PTHREAD_STACK_MIN may not be a constant macro so we need
			// to evaluate it dynamically.
			min_stack_size = PTHREAD_STACK_MIN;
			stack_min_size_defined = true;
		#else
			// Assume minimum stack size is 128 KB.
			min_stack_size = 128 * 1024;
			stack_min_size_defined = false;
		#endif
		if (stack_size != 0 && stack_size < min_stack_size) {
			stack_size = min_stack_size;
			round_stack_size = !stack_min_size_defined;
		} else {
			round_stack_size = true;
		}

		if (round_stack_size) {
			// Round stack size up to page boundary.
			long page_size;
			#if defined(_SC_PAGESIZE)
				page_size = sysconf(_SC_PAGESIZE);
			#elif defined(_SC_PAGE_SIZE)
				page_size = sysconf(_SC_PAGE_SIZE);
			#elif defined(PAGESIZE)
				page_size = sysconf(PAGESIZE);
			#elif defined(PAGE_SIZE)
				page_size = sysconf(PAGE_SIZE);
			#else
				page_size = getpagesize();
			#endif
			if (stack_size % page_size != 0) {
				stack_size = stack_size - (stack_size % page_size) + page_size;
			}
		}

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, 1);
		pthread_attr_setstacksize(&attr, stack_size);
		ret = pthread_create(&thr, &attr, mainFunction, arg);
		pthread_attr_destroy(&attr);
		return ret;
	}

	static void *detachProcessMain(void *arg) {
		this_thread::disable_syscall_interruption dsi;
		pid_t pid = (pid_t) (long) arg;
		syscalls::waitpid(pid, NULL, 0);
		return NULL;
	}

	void detachProcess(pid_t pid) {
		startBackgroundThread(detachProcessMain, (void *) (long) pid);
	}

	vector<string> createCommand(const Options &options, const SpawnPreparationInfo &preparation,
		shared_array<const char *> &args) const
	{
		vector<string> startCommandArgs;
		string agentsDir = config->resourceLocator.getAgentsDir();
		vector<string> command;

		split(options.getStartCommand(config->resourceLocator), '\t', startCommandArgs);
		if (startCommandArgs.empty()) {
			throw RuntimeException("No startCommand given");
		}

		if (shouldLoadShellEnvvars(options, preparation)) {
			command.push_back(preparation.shell);
			command.push_back(preparation.shell);
			command.push_back("-lc");
			command.push_back("exec \"$@\"");
			command.push_back("SpawnPreparerShell");
		} else {
			command.push_back(agentsDir + "/SpawnPreparer");
		}
		command.push_back(agentsDir + "/SpawnPreparer");
		command.push_back(preparation.appRoot);
		command.push_back(serializeEnvvarsFromPoolOptions(options));
		command.push_back(startCommandArgs[0]);
		// Note: do not try to set a process title here.
		// https://code.google.com/p/phusion-passenger/issues/detail?id=855
		command.push_back(startCommandArgs[0]);
		for (unsigned int i = 1; i < startCommandArgs.size(); i++) {
			command.push_back(startCommandArgs[i]);
		}

		createCommandArgs(command, args);
		return command;
	}

public:
	DirectSpawner(const ServerInstanceDir::GenerationPtr &_generation,
		const SpawnerConfigPtr &_config)
		: Spawner(_config)
	{
		generation = _generation;
	}

	virtual ProcessPtr spawn(const Options &options) {
		TRACE_POINT();
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		P_DEBUG("Spawning new process: appRoot=" << options.appRoot);
		possiblyRaiseInternalError(options);

		shared_array<const char *> args;
		SpawnPreparationInfo preparation = prepareSpawn(options);
		vector<string> command = createCommand(options, preparation, args);
		SocketPair adminSocket = createUnixSocketPair();
		Pipe errorPipe = createPipe();
		DebugDirPtr debugDir = boost::make_shared<DebugDir>(preparation.uid, preparation.gid);
		pid_t pid;

		pid = syscalls::fork();
		if (pid == 0) {
			setenv("PASSENGER_DEBUG_DIR", debugDir->getPath().c_str(), 1);
			purgeStdio(stdout);
			purgeStdio(stderr);
			resetSignalHandlersAndMask();
			disableMallocDebugging();
			int adminSocketCopy = dup2(adminSocket.first, 3);
			int errorPipeCopy = dup2(errorPipe.second, 4);
			dup2(adminSocketCopy, 0);
			dup2(adminSocketCopy, 1);
			dup2(errorPipeCopy, 2);
			closeAllFileDescriptors(2);
			setChroot(preparation);
			switchUser(preparation);
			setWorkingDirectory(preparation);
			execvp(args[0], (char * const *) args.get());

			int e = errno;
			printf("!> Error\n");
			printf("!> \n");
			printf("Cannot execute \"%s\": %s (errno=%d)\n", command[0].c_str(),
				strerror(e), e);
			fprintf(stderr, "Cannot execute \"%s\": %s (errno=%d)\n",
				command[0].c_str(), strerror(e), e);
			fflush(stdout);
			fflush(stderr);
			_exit(1);

		} else if (pid == -1) {
			int e = errno;
			throw SystemException("Cannot fork a new process", e);

		} else {
			UPDATE_TRACE_POINT();
			ScopeGuard guard(boost::bind(nonInterruptableKillAndWaitpid, pid));
			P_DEBUG("Process forked for appRoot=" << options.appRoot << ": PID " << pid);
			adminSocket.first.close();
			errorPipe.second.close();

			NegotiationDetails details;
			details.preparation = &preparation;
			details.stderrCapturer =
				make_shared<BackgroundIOCapturer>(
					errorPipe.first,
					pid,
					// The cast works around a compilation problem in Clang.
					(const char *) "stderr");
			details.stderrCapturer->start();
			details.pid = pid;
			details.adminSocket = adminSocket.second;
			details.io = BufferedIO(adminSocket.second);
			details.errorPipe = errorPipe.first;
			details.options = &options;
			details.debugDir = debugDir;

			ProcessPtr process;
			{
				this_thread::restore_interruption ri(di);
				this_thread::restore_syscall_interruption rsi(dsi);
				process = negotiateSpawn(details);
			}
			detachProcess(process->pid);
			guard.clear();
			P_DEBUG("Process spawning done: appRoot=" << options.appRoot <<
				", pid=" << process->pid);
			return process;
		}
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_DIRECT_SPAWNER_H_ */
