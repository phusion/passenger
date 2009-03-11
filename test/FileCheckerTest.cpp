#include "tut.h"
#include "FileChecker.h"
#include "SystemTime.h"
#include <stdio.h>
#include <utime.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct FileCheckerTest {
		FileCheckerTest() {
		}
		
		~FileCheckerTest() {
			unlink("test.txt");
			SystemTime::release();
		}
	};
	
	static void touch(const char *filename, time_t timestamp = 0) {
		FILE *f = fopen(filename, "w");
		fclose(f);
		if (timestamp != 0) {
			struct utimbuf buf;
			buf.actime = timestamp;
			buf.modtime = timestamp;
			utime(filename, &buf);
		}
	}
	
	DEFINE_TEST_GROUP(FileCheckerTest);
	
	TEST_METHOD(1) {
		// File is not changed if it didn't exist.
		FileChecker checker("test.txt");
		ensure(!checker.changed());
	}
	
	TEST_METHOD(2) {
		// File is not changed if its ctime and mtime didn't change.
		touch("test.txt");
		FileChecker checker("test.txt");
		ensure(!checker.changed());
	}
	
	TEST_METHOD(3) {
		// File is changed if it didn't exist but has now been created.
		FileChecker checker("test.txt");
		touch("test.txt");
		ensure(checker.changed());
	}
	
	TEST_METHOD(4) {
		// File is changed if its mtime changed.
		touch("test.txt", time(NULL) - 5);
		FileChecker checker("test.txt");
		touch("test.txt");
		ensure("First check: changed", checker.changed());
		ensure("Second check: unchanged", !checker.changed());
	}
	
	TEST_METHOD(5) {
		// Throttling works.
		SystemTime::force(5);
		
		FileChecker checker("test.txt");
		checker.changed(3);
		touch("test.txt");
		ensure(!checker.changed(3));
		
		SystemTime::force(6);
		ensure(!checker.changed(3));
		
		SystemTime::force(8);
		ensure(checker.changed(3));
		ensure(!checker.changed(3));
	}
}
