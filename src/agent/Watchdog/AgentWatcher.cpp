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

/**
 * Abstract base class for watching agent processes.
 */
class AgentWatcher: public boost::enable_shared_from_this<AgentWatcher> {
private:
	/** The watcher thread. */
	oxt::thread *thr;

	void threadMain(boost::shared_ptr<AgentWatcher> self) {
		try {
			pid_t pid, ret;
			int status, e;

			while (!boost::this_thread::interruption_requested()) {
				{
					boost::lock_guard<boost::mutex> l(lock);
					pid = this->pid;
				}

				// Process can be started before the watcher thread is launched.
				if (pid == 0) {
					pid = start();
				}
				ret = syscalls::waitpid(pid, &status, 0);
				if (ret == -1 && errno == ECHILD) {
					/* If the agent is attached to gdb then waitpid()
					 * here can return -1 with errno == ECHILD.
					 * Fallback to kill() polling for checking
					 * whether the agent is alive.
					 */
					ret = pid;
					status = 0;
					P_WARN("waitpid() on " << name() << " (pid=" << pid <<
						") returned -1 with " <<
						"errno = ECHILD, falling back to kill polling");
					waitpidUsingKillPolling(pid);
					e = 0;
				} else {
					e = errno;
				}

				{
					boost::lock_guard<boost::mutex> l(lock);
					this->pid = 0;
				}

				boost::this_thread::disable_interruption di;
				boost::this_thread::disable_syscall_interruption dsi;
				if (ret == -1) {
					P_WARN(name() << " (pid=" << pid << ") crashed or killed for "
						"an unknown reason (errno = " <<
						strerror(e) << "), restarting it...");
				} else if (WIFEXITED(status)) {
					if (WEXITSTATUS(status) == 0) {
						/* When the web server is gracefully exiting, it will
						 * tell one or more agents to gracefully exit with exit
						 * status 0. If we see this then it means the watchdog
						 * is gracefully shutting down too and we should stop
						 * watching.
						 */
						return;
					} else {
						P_WARN(name() << " (pid=" << pid <<
							") crashed with exit status " <<
							WEXITSTATUS(status) << ", restarting it...");
					}
				} else {
					P_WARN(name() << " (pid=" << pid <<
						") crashed with signal " <<
						getSignalName(WTERMSIG(status)) <<
						", restarting it...");
				}

				const char *sleepTime;
				if ((sleepTime = getenv("PASSENGER_AGENT_RESTART_SLEEP")) != NULL) {
					sleep(atoi(sleepTime));
				}
			}
		} catch (const boost::thread_interrupted &) {
		} catch (const tracable_exception &e) {
			boost::lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = e.what();
			threadExceptionBacktrace = e.backtrace();
			wo->errorEvent.notify();
		} catch (const std::exception &e) {
			boost::lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = e.what();
			wo->errorEvent.notify();
		} catch (...) {
			boost::lock_guard<boost::mutex> l(lock);
			threadExceptionMessage = "Unknown error";
			wo->errorEvent.notify();
		}
	}

protected:
	/** PID of the process we're watching. 0 if no process is started at this time. */
	pid_t pid;

	/** If the watcher thread threw an uncaught exception then its information will
	 * be stored here so that the main thread can check whether a watcher encountered
	 * an error. These are empty strings if everything is OK.
	 */
	string threadExceptionMessage;
	string threadExceptionBacktrace;

	/** The agent process's feedback fd. */
	FileDescriptor feedbackFd;

	/**
	 * Lock for protecting the exchange of data between the main thread and
	 * the watcher thread.
	 */
	mutable boost::mutex lock;

	WorkingObjectsPtr wo;

	/**
	 * Returns the filename of the agent process's executable. This method may be
	 * called in a forked child process and may therefore not allocate memory.
	 */
	virtual string getExeFilename() const = 0;

	/**
	 * This method is to exec() the agent with the right arguments.
	 * It is called from within a forked child process, so don't do any dynamic
	 * memory allocations in here. It must also not throw any exceptions.
	 * It must also preserve the value of errno after exec() is called.
	 */
	virtual void execProgram() const {
		execl(getExeFilename().c_str(),
			getExeFilename().c_str(),
			"3",  // feedback fd
			(char *) 0);
	}

	/**
	 * This method is to send startup arguments to the agent process through
	 * the given file descriptor, which is the agent process's feedback fd.
	 * May throw arbitrary exceptions.
	 */
	virtual void sendStartupArguments(pid_t pid, FileDescriptor &fd) = 0;

	/**
	 * This method is to process the startup info that the agent process has
	 * sent back. May throw arbitrary exceptions.
	 */
	virtual bool processStartupInfo(pid_t pid, FileDescriptor &fd, const vector<string> &args) = 0;

	/**
	 * Kill a process (but not its children) with SIGTERM.
	 * Does not wait until it has quit.
	 */
	static void killAndDontWait(pid_t pid) {
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		syscalls::kill(pid, SIGTERM);
	}

	/**
	 * Kill a process with SIGKILL, and attempt to kill its children too.
	 * Then wait until it has quit.
	 */
	static void killProcessGroupAndWait(pid_t pid) {
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		// If the process is a process group leader then killing the
		// group will likely kill all its child processes too.
		if (syscalls::killpg(pid, SIGKILL) == -1) {
			syscalls::kill(pid, SIGKILL);
		}
		syscalls::waitpid(pid, NULL, 0);
	}

	/**
	 * Behaves like <tt>waitpid(pid, status, WNOHANG)</tt>, but waits at most
	 * <em>timeout</em> miliseconds for the process to exit.
	 */
	static int timedWaitPid(pid_t pid, int *status, unsigned long long timeout) {
		Timer<SystemTime::GRAN_10MSEC> timer;
		int ret;

		do {
			ret = syscalls::waitpid(pid, status, WNOHANG);
			if (ret > 0 || ret == -1) {
				return ret;
			} else {
				syscalls::usleep(10000);
			}
		} while (timer.elapsed() < timeout);
		return 0; // timed out
	}

	static void waitpidUsingKillPolling(pid_t pid) {
		bool done = false;

		while (!done) {
			int ret = syscalls::kill(pid, 0);
			done = ret == -1;
			if (!done) {
				syscalls::usleep(20000);
			}
		}
	}

public:
	AgentWatcher(const WorkingObjectsPtr &wo) {
		thr = NULL;
		pid = 0;
		this->wo = wo;
	}

	virtual ~AgentWatcher() {
		delete thr;
	}

	/**
	 * Store information about the started agent process in the given report object.
	 * May throw arbitrary exceptions.
	 *
	 * @pre start() has been called and succeeded.
	 */
	virtual void reportAgentStartupResult(Json::Value &report) = 0;

	/** Returns the name of the agent that this class is watching. */
	virtual const char *name() const = 0;

	/**
	 * Starts the agent process. May throw arbitrary exceptions.
	 */
	virtual pid_t start() {
		boost::this_thread::disable_interruption di;
		boost::this_thread::disable_syscall_interruption dsi;
		string exeFilename = getExeFilename();
		SocketPair fds;
		int e, ret;
		pid_t pid;

		/* Create feedback fd for this agent process. We'll send some startup
		 * arguments to this agent process through this fd, and we'll receive
		 * startup information through it as well.
		 */
		fds = createUnixSocketPair(__FILE__, __LINE__);

		pid = syscalls::fork();
		if (pid == 0) {
			// Child

			/* Make sure file descriptor FEEDBACK_FD refers to the newly created
			 * feedback fd (fds[1]) and close all other file descriptors.
			 * In this child process we don't care about the original FEEDBACK_FD
			 * (which is the Watchdog's communication channel to the agents starter.)
			 *
			 * fds[1] is guaranteed to be != FEEDBACK_FD because the watchdog
			 * is started with FEEDBACK_FD already assigned.
			 */
			syscalls::close(fds[0]);

			if (syscalls::dup2(fds[1], FEEDBACK_FD) == -1) {
				/* Something went wrong, report error through feedback fd. */
				e = errno;
				try {
					writeArrayMessage(fds[1],
						"system error before exec",
						"dup2() failed",
						toString(e).c_str(),
						NULL);
					_exit(1);
				} catch (...) {
					fprintf(stderr, "PassengerWatchdog: dup2() failed: %s (%d)\n",
						strerror(e), e);
					fflush(stderr);
					_exit(1);
				}
			}

			resetSignalHandlersAndMask();
			closeAllFileDescriptors(FEEDBACK_FD);

			/* Become the process group leader so that the watchdog can kill the
			 * agent as well as all its descendant processes, and so that a Ctrl-C
			 * only affects the watchdog but not agents. */
			setpgid(getpid(), getpid());

			try {
				execProgram();
			} catch (...) {
				fprintf(stderr, "PassengerWatchdog: execProgram() threw an exception\n");
				fflush(stderr);
				_exit(1);
			}
			e = errno;
			try {
				writeArrayMessage(FEEDBACK_FD,
					"exec error",
					toString(e).c_str(),
					NULL);
			} catch (...) {
				fprintf(stderr, "PassengerWatchdog: could not execute %s: %s (%d)\n",
					exeFilename.c_str(), strerror(e), e);
				fflush(stderr);
			}
			_exit(1);
		} else if (pid == -1) {
			// Error
			e = errno;
			throw SystemException("Cannot fork a new process", e);
		} else {
			// Parent
			FileDescriptor feedbackFd(fds[0]);
			vector<string> args;

			fds[1].close();
			boost::this_thread::restore_interruption ri(di);
			boost::this_thread::restore_syscall_interruption rsi(dsi);
			ScopeGuard failGuard(boost::bind(killProcessGroupAndWait, pid));

			/* Send startup arguments. Ignore EPIPE and ECONNRESET here
			 * because the child process might have sent an feedback message
			 * without reading startup arguments.
			 */
			try {
				sendStartupArguments(pid, feedbackFd);
			} catch (const SystemException &ex) {
				if (ex.code() != EPIPE && ex.code() != ECONNRESET) {
					throw SystemException(string("Unable to start the ") + name() +
						": an error occurred while sending startup arguments",
						ex.code());
				}
			}

			// Now read its feedback.
			try {
				ret = readArrayMessage(feedbackFd, args);
			} catch (const SystemException &e) {
				if (e.code() == ECONNRESET) {
					ret = false;
				} else {
					throw SystemException(string("Unable to start the ") + name() +
						": unable to read its startup information",
						e.code());
				}
			}
			if (!ret) {
				boost::this_thread::disable_interruption di2;
				boost::this_thread::disable_syscall_interruption dsi2;
				int status;

				/* The feedback fd was prematurely closed for an unknown reason.
				 * Did the agent process crash?
				 *
				 * We use timedWaitPid() here because if the process crashed
				 * because of an uncaught exception, the file descriptor
				 * might be closed before the process has printed an error
				 * message, so we give it some time to print the error
				 * before we kill it.
				 */
				ret = timedWaitPid(pid, &status, 5000);
				if (ret == 0) {
					/* Doesn't look like it; it seems it's still running.
					 * We can't do anything without proper feedback so kill
					 * the agent process and throw an exception.
					 */
					failGuard.runNow();
					throw RuntimeException(string("Unable to start the ") + name() +
						": it froze and reported an unknown error during its startup");
				} else if (ret != -1 && WIFSIGNALED(status)) {
					/* Looks like a crash which caused a signal. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have been killed with signal " +
						getSignalName(WTERMSIG(status)) + " during startup");
				} else if (ret == -1) {
					/* Looks like it exited after detecting an error. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have crashed during startup for an unknown reason");
				} else {
					/* Looks like it exited after detecting an error, but has an exit code. */
					throw RuntimeException(string("Unable to start the ") + name() +
						": it seems to have crashed during startup for an unknown reason, "
						"with exit code " + toString(WEXITSTATUS(status)));
				}
			}

			if (args[0] == "system error before exec") {
				throw SystemException(string("Unable to start the ") + name() +
					": " + args[1], atoi(args[2]));
			} else if (args[0] == "exec error") {
				e = atoi(args[1]);
				if (e == ENOENT) {
					throw RuntimeException(string("Unable to start the ") + name() +
						" because its executable (" + getExeFilename() + ") "
						"doesn't exist. This probably means that your "
						PROGRAM_NAME " installation is broken or "
						"incomplete. Please reinstall " PROGRAM_NAME);
				} else {
					throw SystemException(string("Unable to start the ") + name() +
						" because exec(\"" + getExeFilename() + "\") failed",
						atoi(args[1]));
				}
			} else if (!processStartupInfo(pid, feedbackFd, args)) {
				throw RuntimeException(string("The ") + name() +
					" sent an unknown startup info message '" +
					args[0] + "'");
			}

			boost::lock_guard<boost::mutex> l(lock);
			this->feedbackFd = feedbackFd;
			this->pid = pid;
			failGuard.clear();
			return pid;
		}
	}

	/**
	 * Begin watching the agent process.
	 *
	 * @pre start() has been called and succeeded.
	 * @pre This watcher isn't already watching.
	 * @throws RuntimeException If a precondition failed.
	 * @throws thread_interrupted
	 * @throws thread_resource_error
	 */
	virtual void beginWatching() {
		boost::lock_guard<boost::mutex> l(lock);
		if (pid == 0) {
			throw RuntimeException("start() hasn't been called yet");
		}
		if (thr != NULL) {
			throw RuntimeException("Already started watching.");
		}

		thr = new oxt::thread(boost::bind(&AgentWatcher::threadMain, this, shared_from_this()),
			name(), 256 * 1024);
	}

	static void stopWatching(vector< boost::shared_ptr<AgentWatcher> > &watchers) {
		vector< boost::shared_ptr<AgentWatcher> >::const_iterator it;
		vector<oxt::thread *> threads;
		unsigned int i = 0;

		for (it = watchers.begin(); it != watchers.end(); it++, i++) {
			threads.push_back((*it)->thr);
			threads[i] = (*it)->thr;
		}

		oxt::thread::interrupt_and_join_multiple(&threads[0], threads.size());
		for (it = watchers.begin(); it != watchers.end(); it++, i++) {
			delete (*it)->thr;
			(*it)->thr = NULL;
		}
	}

	/**
	 * Tell the agent process to gracefully shut down. Returns true if it
	 * was signaled, or false if it wasn't started.
	 */
	virtual bool signalShutdown() {
		boost::lock_guard<boost::mutex> l(lock);
		if (pid == 0) {
			return false;
		} else {
			killAndDontWait(pid);
			return true;
		}
	}

	/**
	 * Force the agent process to shut down. Returns true if it was shut down,
	 * or false if it wasn't started.
	 */
	virtual bool forceShutdown() {
		boost::lock_guard<boost::mutex> l(lock);
		if (pid == 0) {
			return false;
		} else {
			killProcessGroupAndWait(pid);
			this->pid = 0;
			return true;
		}
	}

	/**
	 * If the watcher thread has encountered an error, then the error message
	 * will be stored here. If the error message is empty then it means
	 * everything is still OK.
	 */
	string getErrorMessage() const {
		boost::lock_guard<boost::mutex> l(lock);
		return threadExceptionMessage;
	}

	/**
	 * The error backtrace, if applicable.
	 */
	string getErrorBacktrace() const {
		boost::lock_guard<boost::mutex> l(lock);
		return threadExceptionBacktrace;
	}

	/**
	 * Returns the agent process feedback fd, or -1 if the agent process
	 * hasn't been started yet. Can be used to check whether this agent process
	 * has exited without using waitpid().
	 */
	const FileDescriptor getFeedbackFd() const {
		boost::lock_guard<boost::mutex> l(lock);
		return feedbackFd;
	}
};

typedef boost::shared_ptr<AgentWatcher> AgentWatcherPtr;
