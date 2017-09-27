#ifndef _FILE_OFFSET_BITS
	#define _FILE_OFFSET_BITS 64
#endif
#ifndef _LARGE_FILES
	#define _LARGE_FILES 1
#endif
#ifndef _LARGEFILE_SOURCE
	#define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
	#define _LARGEFILE64_SOURCE
#endif

#include <FileTools/LargeFiles.h>
#include <stdio.h>
#include <stdlib.h>

namespace Passenger {


::FILE *
lfs_fopen(const char *filename, const char *mode) {
	return fopen(filename, mode);
}

::FILE *
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
