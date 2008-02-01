#ifndef _PASSENGER_SPAWN_MANAGER_H_
#define _PASSENGER_SPAWN_MANAGER_H_

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>

#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <cstdio>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

#include "Application.h"
#include "MessageChannel.h"
#include "Exceptions.h"

namespace Passenger {

using namespace std;
using namespace boost;

/**
 * This class is responsible for spawning Ruby on Rails applications.
 * TODO: write better documentation
 */
class SpawnManager {
private:
	MessageChannel channel;
	pid_t pid;

public:
	SpawnManager(const string &spawnManagerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby") {
		int fds[2];
		char fd_string[20];
		FILE *logFileHandle = NULL;
		
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket", errno);
		}
		if (apr_snprintf(fd_string, sizeof(fd_string), "%d", fds[1]) <= 0) {
			throw MemoryException();
		}
		if (!logFile.empty()) {
			logFileHandle = fopen(logFile.c_str(), "a");
			if (logFileHandle == NULL) {
				string message("Cannot open log file '");
				message.append(logFile);
				message.append("' for writing.");
				throw IOException(message);
			}
		}

		pid = fork();
		if (pid == 0) {
			// TODO: redirect stderr to a log file
			pid = fork();
			if (pid == 0) {
				if (!logFile.empty()) {
					dup2(fileno(logFileHandle), STDOUT_FILENO);
					dup2(fileno(logFileHandle), STDERR_FILENO);
					fclose(logFileHandle);
				}
				if (!environment.empty()) {
					setenv("RAILS_ENV", environment.c_str(), true);
				}
				close(fds[0]);
				execlp(rubyCommand.c_str(), rubyCommand.c_str(), spawnManagerCommand.c_str(), fd_string, NULL);
				fprintf(stderr, "Unable to run %s: %s\n", rubyCommand.c_str(), strerror(errno));
				_exit(1);
			} else if (pid == -1) {
				fprintf(stderr, "Unable to fork a process: %s\n", strerror(errno));
				_exit(0);
			} else {
				_exit(0);
			}
		} else if (pid == -1) {
			close(fds[0]);
			close(fds[1]);
			throw SystemException("Unable to fork a process", errno);
		} else {
			close(fds[1]);
			if (!logFile.empty()) {
				fclose(logFileHandle);
			}
			waitpid(pid, NULL, 0);
			channel = MessageChannel(fds[0]);
		}
	}
	
	~SpawnManager() {
		channel.close();
	}
	
	ApplicationPtr spawn(const string &appRoot, const string &username = "") {
		vector<string> args;
	
		channel.write("spawn_application", appRoot.c_str(), username.c_str(), NULL);
		if (!channel.read(args)) {
			throw IOException("The spawn manager server has exited unexpectedly.");
		}
		pid_t pid = atoi(args.front().c_str());
		int reader = channel.readFileDescriptor();
		int writer = channel.readFileDescriptor();
		return ApplicationPtr(new Application(appRoot, pid, reader, writer));
	}
};

typedef shared_ptr<SpawnManager> SpawnManagerPtr;

} // namespace Passenger

#endif /* _PASSENGER_SPAWN_MANAGER_H_ */
