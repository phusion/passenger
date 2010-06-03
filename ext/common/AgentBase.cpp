/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>

#include "Constants.h"
#include "AgentBase.h"
#include "Logging.h"

namespace Passenger {

static bool _feedbackFdAvailable = false;

static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

bool
feedbackFdAvailable() {
	return _feedbackFdAvailable;
}

VariantMap
initializeAgent(int argc, char *argv[], const char *processName) {
	TRACE_POINT();
	VariantMap options;
	
	ignoreSigpipe();
	setup_syscall_interruption_support();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	try {
		if (argc == 1) {
			int ret = fcntl(FEEDBACK_FD, F_GETFL);
			if (ret == -1) {
				if (errno == EBADF) {
					fprintf(stderr,
						"You're not supposed to start this program from the command line. "
						"It's used internally by Phusion Passenger.\n");
					exit(1);
				} else {
					int e = errno;
					fprintf(stderr,
						"Encountered an error in feedback file descriptor 3: %s (%d)\n",
							strerror(e), e);
					exit(1);
				}
			} else {
				_feedbackFdAvailable = true;
				options.readFrom(FEEDBACK_FD);
			}
		} else {
			options.readFrom((const char **) argv + 1, argc - 1);
		}
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		exit(1);
	}
	
	setLogLevel(options.getInt("log_level", false, 1));
	if (!options.get("debug_log_file", false).empty()) {
		setDebugFile(options.get("debug_log_file").c_str());
	}
	
	// Change process title.
	strncpy(argv[0], processName, strlen(argv[0]));
	for (int i = 1; i < argc; i++) {
		memset(argv[i], '\0', strlen(argv[i]));
	}
	
	return options;
}

} // namespace Passenger
