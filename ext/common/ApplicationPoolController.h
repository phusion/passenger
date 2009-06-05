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
#ifndef _PASSENGER_APPLICATION_POOL_CONTROLLER_H_
#define _PASSENGER_APPLICATION_POOL_CONTROLLER_H_

#include <string>

#include <oxt/thread.hpp>

#include <unistd.h>


namespace Passenger {

using namespace boost;
using namespace oxt;
using namespace std;

class ApplicationPoolServerController {
private:
	/**
	 * The path to the server executable.
	 *
	 * @invariant executable != ""
	 */
	string executable;
	
	/**
	 * The PID of the server executable process, or 0 if the server
	 * executable is not running.
	 */
	pid_t pid;
	
	/**
	 * The filename on which the server executable is listening, or
	 * the empty string if the server executable is not running.
	 */
	string socketFilename;
	
	/**
	 * The secret token.
	 */
	string secretToken;
	
	void stop() {
		TRACE_POINT();
		this_thread::disable_syscall_interruption dsi;
		int ret, status;
		time_t begin;
		bool done = false;
		
		// Send a shutdown command to the ApplicationPool server by
		// sending a random byte through the admin pipe.
		syscalls::write(adminPipe, "x", 1);
		syscalls::close(adminPipe);
		
		P_TRACE(2, "Waiting for existing ApplicationPoolServerExecutable (PID " <<
			serverPid << ") to exit...");
		begin = syscalls::time(NULL);
		while (!done && syscalls::time(NULL) < begin + 5) {
			ret = syscalls::waitpid(serverPid, &status, WNOHANG);
			done = ret > 0 || ret == -1;
			if (!done) {
				syscalls::usleep(100000);
			}
		}
		if (done) {
			if (ret > 0) {
				if (WIFEXITED(status)) {
					P_TRACE(2, "ApplicationPoolServerExecutable exited with exit status " <<
						WEXITSTATUS(status) << ".");
				} else if (WIFSIGNALED(status)) {
					P_TRACE(2, "ApplicationPoolServerExecutable exited because of signal " <<
						WTERMSIG(status) << ".");
				} else {
					P_TRACE(2, "ApplicationPoolServerExecutable exited for an unknown reason.");
				}
			} else {
				P_TRACE(2, "ApplicationPoolServerExecutable exited.");
			}
		} else {
			P_DEBUG("ApplicationPoolServerExecutable not exited in time. Killing it...");
			syscalls::kill(serverPid, SIGKILL);
			syscalls::waitpid(serverPid, NULL, 0);
		}
		
		do {
			ret = unlink(serverSocket.c_str());
		} while (ret == -1 && errno == EINTR);
		
		password = "";
		adminPipe = -1;
		serverSocket = "";
		serverPid = 0;
	}
	
public:
	ApplicationPoolServerController(const string &executable) {
		this->executable = executable;
	}
	
	~ApplicationPoolServerController() {
		this_thread::disable_interruption dsi;
		stop();
	}
	
	void restart() {
		if (started()) {
			stop();
		}
		
	}
	
	bool started() const {
		return getPid() != 0;
	}
	
	pid_t getPid() const {
		return pid;
	}
	
	string getSocketFilename() const {
		return socketFilename;
	}
	
	ApplicationPoolClientPtr connect() {
		
	}
};

} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_CONTROLLER_H_ */
