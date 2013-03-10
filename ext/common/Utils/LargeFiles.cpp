#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES 1
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <Utils/LargeFiles.h>
#include <stdlib.h>

namespace Passenger {

FILE *
lfs_fopen(const char *filename, const char *mode) {
	return fopen(filename, mode);
}

FILE *
lfs_fdopen(int filedes, const char *mode) {
	return fdopen(filedes, mode);
}

int
lfs_mkstemp(char *templ) {
	#ifdef __linux__
		return mkstemp64(templ);
	#else
		return mkstemp(templ);
	#endif
}

} // namespace Passenger
