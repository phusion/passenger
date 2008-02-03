#ifndef _PASSENGER_DUMMY_SPAWN_MANAGER_H_
#define _PASSENGER_DUMMY_SPAWN_MANAGER_H_

#define DUMMY_REQUEST_HANDLER_EXECUTABLE "/home/hongli/Projects/mod_rails/benchmark/DummyRequestHandler"

#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Application.h"

namespace Passenger {

using namespace std;

/**
 * This class implements a dummy spawn manager. This spawn manager will spawn
 * benchmark/DummyRequestHandler, which is probably the fastest possible
 * implementation of a request handler. The purpose of this class to benchmark
 * the performance of the Apache module (i.e. not benchmarking the Ruby request
 * handler or Rails itself).
 *
 * This header file is not used by default. Modify ApplicationPool to make use
 * of this file/class.
 *
 * Of course, don't forget to compile benchmark/DummyRequestHandler before you
 * use this class!
 */
class DummySpawnManager {
public:
	ApplicationPtr spawn(const string &appRoot, const string &user = "", const string &group = "") {
		int fd1[2], fd2[2];
		pid_t pid;
		
		pipe(fd1);
		pipe(fd2);
		pid = fork();
		if (pid == 0) {
			pid = fork();
			if (pid == 0) {
				dup2(fd1[0], 0);
				dup2(fd2[1], 1);
				close(fd1[0]);
				close(fd1[1]);
				close(fd2[0]);
				close(fd2[1]);
				execlp(DUMMY_REQUEST_HANDLER_EXECUTABLE, DUMMY_REQUEST_HANDLER_EXECUTABLE, NULL);
				_exit(1);
			} else {
				_exit(0);
			}
		} else {
			int reader, writer;
			
			close(fd1[0]);
			close(fd2[1]);
			reader = fd2[0];
			writer = fd1[1];
			waitpid(pid, NULL, 0);
			return ApplicationPtr(new Application(appRoot, pid, reader, writer));
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_DUMMY_SPAWN_MANAGER_H_ */
