/*
 * Sets given environment variables, then execs the given command.
 *
 * This is a separate executable because it does quite
 * some non-async-signal-safe stuff that we can't do after
 * fork()ing from the Spawner and before exec()ing.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <string>
#include <Utils/Base64.h>

using namespace std;
using namespace Passenger;

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
	
	execvp(executable, (char * const *) execArgs);
	int e = errno;
	fprintf(stderr, "*** ERROR ***: Cannot execute %s: %s (%d)\n",
		executable, strerror(e), e);
	return 1;
}
