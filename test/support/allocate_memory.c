#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

// Lets the allocated memory escape, so that the compiler can't optimize away
// writes to it.
uint64_t * volatile allocatedMemory;

/*
 * Allocates and touches the given number of MB, prints "ready" on stdout,
 * then sleeps until killed. Used by ProcessMetricsCollectorTest.
 *
 * The memory is filled with pseudo-random data instead of zeros, so that
 * macOS's memory compressor cannot shrink it. Compressed pages don't count
 * as resident, which would make the measured memory usage too low.
 */
int
main(int argc, char *argv[]) {
	size_t size = (size_t) atol(argv[1]) * 1024 * 1024;
	uint64_t *memory = (uint64_t *) malloc(size);
	size_t count = size / sizeof(uint64_t);
	uint64_t state = 88172645463325252ULL;
	size_t i;

	allocatedMemory = memory;
	for (i = 0; i < count; i++) {
		// xorshift64
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		memory[i] = state;
	}

	printf("ready\n");
	fflush(stdout);
	sleep(999999999);
	return 0;
}
