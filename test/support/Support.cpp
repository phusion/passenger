#include "Support.h"

namespace Test {

void
touchFile(const char *filename, time_t timestamp) {
	FILE *f = fopen(filename, "a");
	if (f != NULL) {
		fclose(f);
	} else {
		int e = errno;
		cerr << "Cannot touch file '" << filename << "': " <<
			strerror(e) <<" (" << e << ")" << endl;
		throw exception();
	}
	
	if (timestamp != (time_t) -1) {
		struct utimbuf times;
		times.actime = timestamp;
		times.modtime = timestamp;
		utime(filename, &times);
	}
}

} // namespace Test
