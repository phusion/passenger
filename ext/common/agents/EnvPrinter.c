#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

extern char **environ;

int
main(int argc, char *argv[]) {
	int i = 0;
	while (environ[i] != NULL) {
		write(STDOUT_FILENO, environ[i], strlen(environ[i]) + 1);
		i++;
	}
	return 0;
}
