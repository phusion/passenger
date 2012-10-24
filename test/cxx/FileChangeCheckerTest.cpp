#include "TestSupport.h"
#include "Utils/FileChangeChecker.h"
#include "Utils/SystemTime.h"
#include <unistd.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct FileChangeCheckerTest {
		FileChangeCheckerTest() {
		}
		
		~FileChangeCheckerTest() {
			unlink("test.txt");
			unlink("test2.txt");
			SystemTime::release();
		}
	};
	
	DEFINE_TEST_GROUP(FileChangeCheckerTest);
	
	TEST_METHOD(1) {
		// If a file is checked for the first time, then it
		// returns whether the file exists.
		FileChangeChecker checker(10);
		touchFile("test.txt");
		ensure(checker.changed("test.txt"));
		ensure(!checker.changed("test2.txt"));
	}
	
	TEST_METHOD(2) {
		// If a file is checked for the first time, and its
		// directory is not accessible, then false is returned.
		if (geteuid() != 0) {
			// Root can read everything so no point in
			// testing if we're running as root.
			
			FileChangeChecker checker(10);
			TempDir d("test.tmp");
			touchFile("test.tmp/test.txt");
			
			runShellCommand("chmod a= test.tmp");
			ensure(!checker.changed("test.tmp/test.txt"));
			
			// Should still be false.
			ensure(!checker.changed("test.tmp/test.txt"));
			
			// Now make it accessible again...
			runShellCommand("chmod u=rwx test.tmp");
			ensure(checker.changed("test.tmp/test.txt"));
		}
	}
	
	TEST_METHOD(3) {
		// File is not changed if it didn't exist and
		// still doesn't exist.
		FileChangeChecker checker(10);
		
		checker.changed("test.txt");
		ensure("test.txt not changed", !checker.changed("test.txt"));
		
		checker.changed("test2.txt");
		ensure("test2.txt not changed", !checker.changed("test2.txt"));
	}
	
	TEST_METHOD(4) {
		// File is not changed if its ctime and mtime didn't change.
		FileChangeChecker checker(10);
		
		touchFile("test.txt");
		checker.changed("test.txt");
		ensure(!checker.changed("test.txt"));
		
		touchFile("test2.txt");
		checker.changed("test2.txt");
		ensure(!checker.changed("test2.txt"));
	}
	
	TEST_METHOD(5) {
		// File is changed if it didn't exist but has now been created.
		FileChangeChecker checker(10);
		
		checker.changed("test.txt");
		checker.changed("test2.txt");
		touchFile("test.txt");
		touchFile("test2.txt");
		ensure(checker.changed("test.txt"));
		ensure(checker.changed("test2.txt"));
	}
	
	TEST_METHOD(6) {
		// File is not changed if existed and has now been deleted.
		FileChangeChecker checker(10);
		
		touchFile("test.txt");
		checker.changed("test.txt");
		unlink("test.txt");
		ensure("test.txt is not considered changed if it has been deleted",
			!checker.changed("test.txt"));
		
		touchFile("test2.txt");
		checker.changed("test2.txt");
		unlink("test2.txt");
		ensure("test2.txt is not considered changed if it has been deleted",
			!checker.changed("test2.txt"));
	}
	
	TEST_METHOD(7) {
		// File is changed if its mtime changed.
		FileChangeChecker checker(1);
		
		touchFile("test.txt", 5);
		checker.changed("test.txt");
		touchFile("test.txt", 10);
		ensure("First check: changed", checker.changed("test.txt"));
		ensure("Second check: unchanged", !checker.changed("test.txt"));
		
		touchFile("test2.txt", 5);
		checker.changed("test2.txt");
		touchFile("test2.txt", 10);
		ensure("First check test2.txt: changed", checker.changed("test2.txt"));
		ensure("Second check test2.txt: unchanged", !checker.changed("test2.txt"));
	}
	
	TEST_METHOD(8) {
		// If a file is not checked for the first time and its
		// information is still in the cache, but the directory
		// in which the file lives is now suddenly inaccessible,
		// then false is returned.
		if (geteuid() != 0) {
			// Root can read everything so no point in
			// testing if we're running as root.
			
			FileChangeChecker checker(10);
			TempDir d("test.tmp");
			touchFile("test.tmp/test.txt", 1);
			checker.changed("test.tmp/test.txt");
			
			touchFile("test.tmp/test.txt", 2);
			runShellCommand("chmod a= test.tmp");
			ensure("First check returns false", !checker.changed("test.tmp/test.txt"));
			
			// Now make it accessible again...
			runShellCommand("chmod u=rwx test.tmp");
			ensure("Second check returns true", checker.changed("test.tmp/test.txt"));
		}
	}
	
	TEST_METHOD(9) {
		// Throttling works.
		SystemTime::force(5);
		
		FileChangeChecker checker(1);
		checker.changed("test.txt", 3);
		touchFile("test.txt");
		ensure(!checker.changed("test.txt", 3));
		
		SystemTime::force(6);
		ensure(!checker.changed("test.txt", 3));
		
		SystemTime::force(8);
		ensure(checker.changed("test.txt", 3));
		ensure(!checker.changed("test.txt", 3));
	}
	
	TEST_METHOD(10) {
		// Test scenario involving multiple files.
		FileChangeChecker checker(10);
		
		checker.changed("test.txt");
		checker.changed("test2.txt");
		checker.changed("test3.txt");
		
		touchFile("test2.txt", 1);
		ensure(!checker.changed("test.txt"));
		ensure(checker.changed("test2.txt"));
		ensure(!checker.changed("test3.txt"));
		
		touchFile("test.txt", 2);
		touchFile("test3.txt", 3);
		ensure(checker.changed("test.txt"));
		ensure(!checker.changed("test2.txt"));
		ensure(checker.changed("test3.txt"));
	}
	
	TEST_METHOD(11) {
		// Different filenames are treated as different files.
		FileChangeChecker checker(10);
		checker.changed("test.txt");
		checker.changed("./test.txt");
		touchFile("test.txt", 1);
		ensure(checker.changed("test.txt"));
		ensure(checker.changed("./test.txt"));
	}
	
	TEST_METHOD(12) {
		if (geteuid() != 0) {
			// Root can read everything so no point in
			// testing if we're running as root.
			
			FileChangeChecker checker(10);
			TempDir d("test.tmp");
			touchFile("test.tmp/test.txt", 1);
			
			checker.changed("test.tmp/test.txt");
			touchFile("test.tmp/test.txt", 2);
			runShellCommand("chmod a= test.tmp");
			ensure("(1)", !checker.changed("test.tmp/test.txt"));
			runShellCommand("chmod u=rwx test.tmp");
			ensure("(2)", checker.changed("test.tmp/test.txt"));
		}
	}
	
	TEST_METHOD(13) {
		// Size limitation works.
		FileChangeChecker checker(2);
		touchFile("test.txt", 1);
		touchFile("test2.txt", 2);
		touchFile("test3.txt", 3);
		
		checker.changed("test.txt");
		checker.changed("test2.txt");
		checker.changed("test3.txt");
		
		// test.txt is now removed from the file list.
		
		unlink("test.txt");
		unlink("test2.txt");
		unlink("test3.txt");
		ensure("test2.txt is still in the file list", checker.knows("test2.txt"));
		ensure("test2.txt is not considered changed", !checker.changed("test2.txt"));
		ensure("test3.txt is still in the file list", checker.knows("test3.txt"));
		ensure("test3.txt is not considered changed", !checker.changed("test3.txt"));
		ensure("test.txt is removed from the file list", !checker.knows("test.txt"));
	}
	
	TEST_METHOD(14) {
		// Increasing the file list size dynamically works.
		FileChangeChecker checker(2);
		touchFile("test.txt", 1);
		touchFile("test2.txt", 2);
		touchFile("test3.txt", 3);
		
		checker.changed("test.txt");
		checker.changed("test2.txt");
		checker.changed("test3.txt");
		
		// test.txt is now removed from the file list.
		
		checker.setMaxSize(3);
		unlink("test.txt");
		unlink("test2.txt");
		unlink("test3.txt");
		
		ensure("test.txt is removed from the file list", !checker.knows("test.txt"));
		// The above changed() call should not remove test2.txt from the file list.
		ensure("test2.txt is still in the file list", checker.knows("test2.txt"));
		ensure("test3.txt is still in the file list", checker.knows("test3.txt"));
		
		checker.changed("test.txt");
		checker.changed("test4.txt");
		ensure("test2.txt is removed from the file list, again", !checker.knows("test2.txt"));
	}
	
	TEST_METHOD(16) {
		// Decreasing the file list size dynamically works, and will
		// remove the oldest entries.
		FileChangeChecker checker(4);
		checker.changed("test.txt");
		checker.changed("test2.txt");
		checker.changed("test3.txt");
		checker.changed("test4.txt");
		
		checker.setMaxSize(2);
		ensure(!checker.knows("test.txt"));
		ensure(!checker.knows("test2.txt"));
		ensure(checker.knows("test3.txt"));
		ensure(checker.knows("test4.txt"));
		
		checker.changed("test.txt");
		ensure(!checker.knows("test3.txt"));
	}
	
	TEST_METHOD(17) {
		// An initial maxSize of 0 makes the file list's size unlimited.
		FileChangeChecker checker(0);
		checker.changed("test.txt");
		checker.changed("test2.txt");
		checker.changed("test3.txt");
		checker.changed("test4.txt");
		
		ensure(checker.knows("test.txt"));
		ensure(checker.knows("test2.txt"));
		ensure(checker.knows("test3.txt"));
		ensure(checker.knows("test4.txt"));
	}
	
	TEST_METHOD(18) {
		// Dynamically setting the file list size to 0 makes
		// the file list's size unlimited.
		FileChangeChecker checker(2);
		checker.changed("test.txt");
		checker.changed("test2.txt");
		checker.setMaxSize(0);
		checker.changed("test3.txt");
		checker.changed("test4.txt");
		
		ensure(checker.knows("test.txt"));
		ensure(checker.knows("test2.txt"));
		ensure(checker.knows("test3.txt"));
		ensure(checker.knows("test4.txt"));
	}
	
	TEST_METHOD(19) {
		// Changing the file list size dynamically from 0 to non-0 works;
		// it removes the oldest entries, if necessary.
		FileChangeChecker checker(0);
		checker.changed("test.txt");
		checker.changed("test2.txt");
		checker.changed("test3.txt");
		checker.changed("test4.txt");
		checker.changed("test5.txt");
		checker.setMaxSize(2);
		ensure(!checker.knows("test.txt"));
		ensure(!checker.knows("test2.txt"));
		ensure(!checker.knows("test3.txt"));
		ensure(checker.knows("test4.txt"));
		ensure(checker.knows("test5.txt"));
	}
}
