/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion Holding B.V.
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
 * Touch all files in the server instance dir every 6 hours in order to prevent /tmp
 * cleaners from weaking havoc:
 * http://code.google.com/p/phusion-passenger/issues/detail?id=365
 */
class InstanceDirToucher {
private:
	WorkingObjectsPtr wo;
	oxt::thread *thr;

	void
	threadMain() {
		while (!this_thread::interruption_requested()) {
			syscalls::sleep(60 * 60);

			begin_touch:

			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			// Fork a process which touches everything in the server instance dir.
			pid_t pid = syscalls::fork();
			if (pid == 0) {
				// Child
				int prio, ret, e;

				closeAllFileDescriptors(2);

				// Make process nicer.
				do {
					prio = getpriority(PRIO_PROCESS, getpid());
				} while (prio == -1 && errno == EINTR);
				if (prio != -1) {
					prio++;
					if (prio > 20) {
						prio = 20;
					}
					do {
						ret = setpriority(PRIO_PROCESS, getpid(), prio);
					} while (ret == -1 && errno == EINTR);
				} else {
					perror("getpriority");
				}

				do {
					ret = chdir(wo->instanceDir->getPath().c_str());
				} while (ret == -1 && errno == EINTR);
				if (ret == -1) {
					e = errno;
					fprintf(stderr, "chdir(\"%s\") failed: %s (%d)\n",
						wo->instanceDir->getPath().c_str(),
						strerror(e), e);
					fflush(stderr);
					_exit(1);
				}
				restoreOomScore(agentsOptions);

				execlp("/bin/sh", "/bin/sh", "-c", "find . | xargs touch", (char *) 0);
				e = errno;
				fprintf(stderr, "Cannot execute 'find . | xargs touch': %s (%d)\n",
					strerror(e), e);
				fflush(stderr);
				_exit(1);
			} else if (pid == -1) {
				// Error
				P_WARN("Could not touch the server instance directory because "
					"fork() failed. Retrying in 2 minutes...");
				this_thread::restore_interruption si(di);
				this_thread::restore_syscall_interruption rsi(dsi);
				syscalls::sleep(60 * 2);
				goto begin_touch;
			} else {
				syscalls::waitpid(pid, NULL, 0);
			}
		}
	}

public:
	InstanceDirToucher(const WorkingObjectsPtr &wo) {
		this->wo = wo;
		thr = new oxt::thread(boost::bind(&InstanceDirToucher::threadMain, this),
			"Server instance dir toucher", 256 * 1024);
	}

	~InstanceDirToucher() {
		thr->interrupt_and_join();
		delete thr;
	}
};

typedef boost::shared_ptr<InstanceDirToucher> InstanceDirToucherPtr;
