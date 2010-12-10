#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int
main(int argc, char *argv[]) {
	long size = atol(argv[1]) * 1024 * 1024;
	char *memory = (char *) malloc(size);
	memset(memory, 0, size);
	sleep(999999999);
	return 0;
}

