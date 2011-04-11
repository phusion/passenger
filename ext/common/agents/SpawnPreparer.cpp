// This is a separate executable because it does quite
// some non-async-signal-safe stuff that we can't do after
// fork()ing from the Spawner and before exec()ing.

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <string>

using namespace std;

static void
createPipe(int p[2]) {
	int ret = pipe(p);
	if (ret == -1) {
		perror("Cannot create a pipe");
		exit(1);
	}
}

static string
createCommandString(const char *argv[]) {
	int i = 0;
	string result;
	while (argv[i] != NULL) {
		if (i != 0) {
			result.append(" ");
		}
		result.append(argv[i]);
		i++;
	}
	return result;
}

static string
runCommand(const char *argv[], bool captureStderr = false, const char *inputData = "") {
	int input[2], output[2];
	string commandString = createCommandString(argv);
	createPipe(input);
	createPipe(output);
	pid_t pid = fork();
	if (pid == 0) {
		dup2(input[0], 0);
		dup2(output[1], 1);
		if (captureStderr) {
			dup2(output[1], 2);
		}
		close(input[0]);
		close(input[1]);
		close(output[0]);
		close(output[1]);
		execvp(argv[0], (char * const *) argv);
		
		int e = errno;
		fprintf(stderr, "*** ERROR ***: Cannot execute %s: %s (%d)\n",
			commandString.c_str(), strerror(e), e);
		fflush(stderr);
		_exit(1);
		
	} else if (pid == -1) {
		perror("Cannot fork a process");
		exit(1);
		
	} else {
		close(input[0]);
		close(output[1]);
		
		ssize_t ret, written = 0;
		ssize_t inputSize = strlen(inputData);
		do {
			ret = write(inputData[1], inputData + written, inputSize - written);
			if (ret == -1) {
				perror("Cannot write to subprocess stdin");
				kill(pid, SIGKILL);
				exit(1);
			}
			written += ret;
		} while (written < inputSize);
		close(input[1]);
		
		bool done = false;
		string result;
		while (!done) {
			char buf[1024 * 8];
			ret = read(output[0], buf, sizeof(buf));
			if (ret == -1) {
				perror("Cannot read from subprocess stdin");
				kill(pid, SIGKILL);
				exit(1);
			} else if (ret == 0) {
				done = true;
			} else {
				result.append(buf, ret);
			}
		}
		
		waitpid(pid, NULL, 0);
		return result;
	}
}

static bool
isBash(const char *shell) {
	char command[1024];
	snprintf(command, sizeof(command), "'%s' --version", shell);
	const char *argv[] = { "zsh", "-c", command, NULL };
	string result = runCommand(argv, true);
	return result.find("GNU bash") != string::npos;
}

static char *
concatStr(char *str, const char *str2) {
	size_t len = strlen(str2);
	memcpy(str, str2, len);
	return str + len;
}

static void
appendenv(const char *name, const char *value, const char *separator) {
	const char *currentValue = getenv(name);
	char *newValue = (char *) malloc(strlen(value) + strlen(separator) + strlen(value) + 1);
	char *end = newValue;
	if (*currentValue != '\0') {
		end = concatStr(end, currentValue);
		end = concatStr(end, separator);
	}
	end = concatStr(end, value);
	*end = '\0';
	setenv(name, newValue, 1);
}

static void
mergeEnvironmentVariable(const char *name, const char *value) {
	if (strcmp(name, "PATH") == 0
	 || strcmp(name, "LD_LIBRARY_PATH") == 0
	 || strcmp(name, "DYLD_LIBRARY_PATH") == 0
	 || strcmp(name, "RUBYLIB") == 0
	 || strcmp(name, "PYTHONPATH") == 0) {
		appendenv(name, value, ":");
	} else {
		setenv(name, value, 0);
	}
}

static void
loadShellEnvironmentVariables(const char *shell, const char *envPrinterAgent) {
	static const char * const script =
		"function quietly_load {\n"
		"	[[ -f $1 ]] && source $1 > /dev/null\n"
		"}\n"
		"quietly_load /etc/profile\n"
		"quietly_load ~/.bash_profile || quietly_load ~/.profile\n"
		"quietly_load ~/.bashrc\n"
		"umask\n"
		"exec \"$1\"";
	const char *argv[] = { shell, "-c", script, shell, envPrinterAgent, NULL };
	string result = runCommand(argv);
	const char *current = result.c_str();
	const char *end = current + result.size();
	char *sep;
	
	// Process umask.
	sep = (char *) strchr(current, '\n');
	if (sep == NULL) {
		return;
	}
	*sep = '\0';
	umask((mode_t) strtol(current, (char **) NULL, 8));
	current = sep + 1;
	
	// Process environment variables.
	while (current < end) {
		size_t len = strlen(current);
		sep = strchr(current, '=');
		if (sep != NULL) {
			char *name = (char *) malloc(sep - current + 1);
			const char *value = sep + 1;
			memcpy(name, current, sep - current);
			name[sep - current] = '\0';
			mergeEnvironmentVariable(name, value);
			free(name);
		}
		current = current + len + 1;
	}
}

// Usage: SpawnPreparationAgent <env printer filename> <executable> <exec args...>
int
main(int argc, char *argv[]) {
	if (argc < 4) {
		fprintf(stderr, "Too few arguments.\n");
		exit(1);
	}
	
	const char *envPrinterAgent = argv[1];
	const char *executable = argv[2];
	char **execArgs = &argv[3];
	
	struct passwd *passwd = getpwuid(geteuid());
	if (passwd != NULL && passwd->pw_shell != NULL && isBash(passwd->pw_shell)) {
		loadShellEnvironmentVariables(passwd->pw_shell, envPrinterAgent);
	}
	
	execvp(executable, (char * const *) execArgs);
	int e = errno;
	fprintf(stderr, "*** ERROR ***: Cannot execute %s: %s (%d)\n",
		executable, strerror(e), e);
	return 1;
}
