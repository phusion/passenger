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
 * This class is responsible for spawning new instances of Ruby on Rails applications.
 * Use the spawn() method to do so.
 *
 * <h2>Implementation details</h2>
 * Internally, it makes use of a spawn server, which is written in Ruby. This server
 * is automatically started when a SpawnManager instance is created, and automatically
 * shutdown when that instance is destroyed. Spawning requests are sent to the server,
 * and details about the spawned process is returned.
 *
 * The communication channel with the server is anonymous, i.e. no other processes
 * can access the communication channel, so communication is guaranteed to be safe
 * (unless, of course, if the spawn server itself is a trojan).
 *
 * The server will try to keep the spawning time as small as possible, by keeping
 * corresponding Ruby on Rails frameworks and application code in memory. So the second
 * time an instance of the same application is spawned, the spawn time is significantly
 * lower than the first time. Nevertheless, spawning is a relatively expensive operation
 * (compared to the processing of a typical HTTP request/response), and so should be
 * avoided whenever possible.
 *
 * See the documentation of the spawn server for full implementation details.
 */
class SpawnManager {
private:
	MessageChannel channel;
	pid_t pid;

public:
	/**
	 * Construct a new SpawnManager.
	 *
	 * @param spawnServerCommand The filename of the spawn server to use.
	 * @param logFile Specify a log file that the spawn manager should use.
	 *            Messages on its standard output and standard error channels
	 *            will be written to this log file. If an empty string is
	 *            specified, no log file will be used, and the spawn server
	 *            will use the same standard output/error channels as the
	 *            current process.
	 * @param environment The RAILS_ENV environment that all RoR applications
	 *            should use. If an empty string is specified, the current value
	 *            of the RAILS_ENV environment variable will be used.
	 * @param rubyCommand The Ruby interpreter's command.
	 * @param SystemException An error occured while trying to setup the spawn server.
	 * @throws IOException The specified log file could not be opened.
	 */
	SpawnManager(const string &spawnServerCommand,
	             const string &logFile = "",
	             const string &environment = "production",
	             const string &rubyCommand = "ruby") {
		int fds[2];
		FILE *logFileHandle = NULL;
		
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
			throw SystemException("Cannot create a Unix socket", errno);
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
			pid = fork();
			if (pid == 0) {
				if (!logFile.empty()) {
					dup2(fileno(logFileHandle), STDERR_FILENO);
					fclose(logFileHandle);
				}
				dup2(STDERR_FILENO, STDOUT_FILENO);
				if (!environment.empty()) {
					setenv("RAILS_ENV", environment.c_str(), true);
				}
				dup2(fds[1], STDIN_FILENO);
				close(fds[0]);
				close(fds[1]);
				
				// Close all other file descriptors
				for (int i = sysconf(_SC_OPEN_MAX); i >= 0; i--) {
					if (i != STDIN_FILENO && i != STDOUT_FILENO && i != STDERR_FILENO) {
						close(i);
					}
				}
				
				execlp(rubyCommand.c_str(), rubyCommand.c_str(), spawnServerCommand.c_str(), NULL);
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
			if (logFileHandle != NULL) {
				fclose(logFileHandle);
			}
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
	
	~SpawnManager() throw() {
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
