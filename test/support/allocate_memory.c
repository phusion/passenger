#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int
main(int argc, char *argv[]) {
	long size = atol(argv[1]) * 1024 * 1024;
	char *memoryMb = (char *) malloc(size);
	memset(memoryMb, 0, size);
	sleep(999999999);
	return 0;
}
