/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2016 Phusion Holding B.V.
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
#include <stdlib.h>
#include <string>
#include <sstream>
#include <modp_b64.h>
#include <Utils/SystemMetricsCollector.h>

using namespace std;
using namespace Passenger;

extern "C" {
	extern char **environ;
}

static void
changeWorkingDir(const char *dir) {
	int ret = chdir(dir);
	if (ret == 0) {
		setenv("PWD", dir, 1);
	} else {
		int e = errno;
		printf("!> Error\n");
		printf("!> \n");
		printf("Unable to change working directory to '%s': %s (errno=%d)\n",
			dir, strerror(e), e);
		fflush(stdout);
		exit(1);
	}
}

static void
setGivenEnvVars(const char *envvarsData) {
	string envvars = modp::b64_decode(envvarsData);
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

	SystemMetrics metrics;
	bool collected = false;
	try {
		SystemMetricsCollector collector;
		collector.collect(metrics);
		usleep(50000); // Correct collect CPU metrics.
		collector.collect(metrics);
		collected = true;
	} catch (const RuntimeException &e) {
		fprintf(stderr, "Warning: %s\n", e.what());
	}
	if (collected) {
		f = fopen((dir + "/system_metrics").c_str(), "w");
		if (f != NULL) {
			stringstream stream;
			metrics.toDescription(stream);
			string info = stream.str();

			fwrite(info.data(), 1, info.size(), f);
			fclose(f);
		}
	}
}

// Usage: PassengerAgent spawn-preparer <working directory> <envvars> <executable> <exec args...>
int
spawnPreparerMain(int argc, char *argv[]) {
	#define ARG_OFFSET 1
	if (argc < ARG_OFFSET + 5) {
		fprintf(stderr, "Too few arguments.\n");
		exit(1);
	}

	const char *workingDir = argv[ARG_OFFSET + 1];
	const char *envvars = argv[ARG_OFFSET + 2];
	const char *executable = argv[ARG_OFFSET + 3];
	char **execArgs = &argv[ARG_OFFSET + 4];

	changeWorkingDir(workingDir);
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
