/*
 * Sets given environment variables, dumps the entire environment to
 * a given file (for diagnostics purposes), then execs the given command.
 *
 * This is a separate executable because it does quite
 * some non-async-signal-safe stuff that we can't do after
 * fork()ing from the Spawner and before exec()ing.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <Utils/Base64.h>

using namespace std;
using namespace Passenger;

extern "C" {
	extern char **environ;
}

static void
setGivenEnvVars(const char *envvarsData) {
	string envvars = Base64::decode(envvarsData);
	const char *key = envvars.data();
	const char *end = envvars.data() + envvars.size();
	
	while (key < end) {
		const char *keyEnd = (const char *) memchr(key, '\0', end - key);
		if (keyEnd != NULL) {
			const char *value = keyEnd + 1;
			if (value < end) {
				const char *valueEnd = (const char *) memchr(value, '\0', end - value);
				if (valueEnd != NULL) {
					setenv(key, value, 1);
					key = valueEnd + 1;
				} else {
					break;
				}
			} else {
				break;
			}
		} else {
			break;
		}
	}
}

static void
dumpInformation() {
	const char *c_dir;
	if ((c_dir = getenv("PASSENGER_DEBUG_DIR")) == NULL) {
		return;
	}

	FILE *f;
	string dir = c_dir;

	f = fopen((dir + "/envvars").c_str(), "w");
	if (f != NULL) {
		int i = 0;
		while (environ[i] != NULL) {
			fputs(environ[i], f);
			putc('\n', f);
			i++;
		}
		fclose(f);
	}

	f = fopen((dir + "/user_info").c_str(), "w");
	if (f != NULL) {
		pid_t pid = fork();
		if (pid == 0) {
			dup2(fileno(f), 1);
			execlp("id", "id", (char *) 0);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			fprintf(stderr, "Error: cannot fork a new process: %s (errno=%d)\n",
				strerror(e), e);
		} else {
			waitpid(pid, NULL, 0);
		}
		fclose(f);
	}

	f = fopen((dir + "/ulimit").c_str(), "w");
	if (f != NULL) {
		pid_t pid = fork();
		if (pid == 0) {
			dup2(fileno(f), 1);
			execlp("ulimit", "ulimit", "-a", (char *) 0);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			fprintf(stderr, "Error: cannot fork a new process: %s (errno=%d)\n",
				strerror(e), e);
		} else {
			waitpid(pid, NULL, 0);
		}
		fclose(f);
	}

	f = fopen((dir + "/ulimit").c_str(), "w");
	if (f != NULL) {
		pid_t pid = fork();
		if (pid == 0) {
			dup2(fileno(f), 1);
			execlp("ulimit", "ulimit", "-a", (char *) 0);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			fprintf(stderr, "Error: cannot fork a new process: %s (errno=%d)\n",
				strerror(e), e);
		} else {
			waitpid(pid, NULL, 0);
		}
		fclose(f);
	}

	#ifdef __linux__
		// TODO: call helper-scripts/system-memory-stats.py
		f = fopen((dir + "/sysmemory").c_str(), "w");
		if (f != NULL) {
			pid_t pid = fork();
			if (pid == 0) {
				dup2(fileno(f), 1);
				execlp("free", "free", "-m", (char *) 0);
				_exit(1);
			} else if (pid == -1) {
				int e = errno;
				fprintf(stderr, "Error: cannot fork a new process: %s (errno=%d)\n",
					strerror(e), e);
			} else {
				waitpid(pid, NULL, 0);
			}
			fclose(f);
		}
	#endif
}

// Usage: SpawnPreparer <envvars> <executable> <exec args...>
int
main(int argc, char *argv[]) {
	if (argc < 4) {
		fprintf(stderr, "Too few arguments.\n");
		exit(1);
	}
	
	const char *envvars = argv[1];
	const char *executable = argv[2];
	char **execArgs = &argv[3];
	
	setGivenEnvVars(envvars);
	dumpInformation();
	
	// Print a newline just in case whatever executed us printed data
	// without a newline. Otherwise the next process's "!> I have control"
	// command will not be properly recognized.
	// https://code.google.com/p/phusion-passenger/issues/detail?id=842#c16
	printf("\n");
	fflush(stdout);

	execvp(executable, (char * const *) execArgs);
	int e = errno;
	fprintf(stderr, "*** ERROR ***: Cannot execute %s: %s (%d)\n",
		executable, strerror(e), e);
	return 1;
}
