#include "TestSupport.h"
#include "Utils/CachedFileStat.hpp"
#include "Utils/SystemTime.h"
#include <sys/types.h>
#include <utime.h>

using namespace std;
using namespace Passenger;

namespace tut {
	struct CachedFileStatTest {
		struct stat buf;
		
		~CachedFileStatTest() {
			SystemTime::release();
			unlink("test.txt");
			unlink("test2.txt");
			unlink("test3.txt");
			unlink("test4.txt");
		}
	};
	
	DEFINE_TEST_GROUP(CachedFileStatTest);
	
	static void touch(const char *filename, time_t timestamp = 0) {
		FILE *f = fopen(filename, "w");
		fprintf(f, "hi");
		fclose(f);
		if (timestamp != 0) {
			struct utimbuf buf;
			buf.actime = timestamp;
			buf.modtime = timestamp;
			utime(filename, &buf);
		}
	}
	
	/************ Tests involving a single file ************/
	
	TEST_METHOD(1) {
		// Statting a new file works.
		touch("test.txt");
		CachedFileStat stat(1);
		ensure_equals(stat.stat("test.txt", &buf, 1), 0);
		ensure_equals((long) buf.st_size, (long) 2);
	}
	
	TEST_METHOD(2) {
		// It does not re-stat an existing file until the cache has expired.
		CachedFileStat stat(1);
		
		SystemTime::force(5);
		touch("test.txt", 1);
		ensure_equals("1st stat succceeded",
			stat.stat("test.txt", &buf, 1),
			0);
		
		touch("test.txt", 1000);
		ensure_equals("2nd stat succceeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("Cached value was used",
			buf.st_mtime,
			(time_t) 1);
		
		SystemTime::force(6);
		ensure_equals("3rd stat succceeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("Cache has been invalidated",
			buf.st_mtime,
			(time_t) 1000);
	}
	
	TEST_METHOD(3) {
		// Statting a nonexistant file returns an error.
		CachedFileStat stat(1);
		ensure_equals(stat.stat("test.txt", &buf, 1), -1);
		ensure_equals("It sets errno appropriately", errno, ENOENT);
	}
	
	TEST_METHOD(4) {
		// It does not re-stat a previously nonexistant file until
		// the cache has expired.
		SystemTime::force(5);
		CachedFileStat stat(1);
		ensure_equals("1st stat failed",
			stat.stat("test.txt", &buf, 1),
			-1);
		ensure_equals("It sets errno appropriately", errno, ENOENT);
		
		errno = EEXIST;
		touch("test.txt", 1000);
		ensure_equals("2nd stat failed",
			stat.stat("test.txt", &buf, 1),
			-1);
		ensure_equals("It sets errno appropriately", errno, ENOENT);
		ensure_equals("Cached value was used",
			buf.st_mtime,
			(time_t) 0);
		
		SystemTime::force(6);
		ensure_equals("3rd stat succeeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("Cache has been invalidated",
			buf.st_mtime,
			(time_t) 1000);
		
		unlink("test.txt");
		ensure_equals("4th stat succeeded even though file was unlinked",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("Cached value was used",
			buf.st_mtime,
			(time_t) 1000);
	}
	
	TEST_METHOD(5) {
		// If the throttling rate is 0 then the cache will be bypassed.
		SystemTime::force(5);
		CachedFileStat stat(2);
		ensure_equals("1st stat returns -1",
			stat.stat("test.txt", &buf, 0),
			-1);
		touch("test.txt");
		ensure_equals("2nd stat did not go through the cache",
			stat.stat("test.txt", &buf, 0),
			0);
	}
	
	
	/************ Tests involving multiple files ************/
	
	TEST_METHOD(10) {
		// Throttling in combination with multiple files works.
		CachedFileStat stat(2);
		SystemTime::force(5);
		
		// Touch and stat test.txt. The next stat should return
		// the old info.
		
		touch("test.txt", 10);
		ensure_equals(
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 10);
		
		touch("test.txt", 20);
		ensure_equals(
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 10);
		
		// Touch and stat test2.txt. The next stat should return
		// the old info.
		
		touch("test2.txt", 30);
		ensure_equals(
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 30);
		
		touch("test2.txt", 40);
		ensure_equals(
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 30);
		
		// Forward timer, then stat both files again. The most recent
		// information should be returned.
		
		SystemTime::force(6);
		ensure_equals(
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 20);
		ensure_equals(
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 40);
	}
	
	TEST_METHOD(11) {
		// Cache limiting works.
		CachedFileStat stat(3);
		SystemTime::force(5);
		
		// Create and stat test.txt, test2.txt and test3.txt.
		
		touch("test.txt", 1000);
		ensure_equals(
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 1000);
		
		touch("test2.txt", 1001);
		ensure_equals(
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 1001);
		
		touch("test3.txt", 1003);
		ensure_equals(
			stat.stat("test3.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 1003);
		
		// Stat test2.txt, then create and stat test4.txt, then touch test.txt.
		// test.txt should have been removed from the cache, and thus
		// upon statting it again its new timestamp should be returned.
		
		ensure_equals(
			stat.stat("test2.txt", &buf, 1),
			0);
		
		touch("test4.txt", 1004);
		ensure_equals(
			stat.stat("test4.txt", &buf, 1),
			0);
		
		touch("test.txt", 3000);
		ensure_equals(
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 3000);
	}
	
	TEST_METHOD(12) {
		// Increasing the cache size dynamically works.
		SystemTime::force(5);
		CachedFileStat stat(2);
		touch("test.txt", 1);
		touch("test2.txt", 2);
		touch("test3.txt", 3);
		
		ensure_equals("1st stat succeeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("2nd stat succeeded",
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals("3rd stat succeeded",
			stat.stat("test3.txt", &buf, 1),
			0);
		
		// test.txt should now be removed from the cache.
		
		touch("test.txt", 10);
		ensure_equals("4th stat succeeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 10);
		
		// test2.txt should now be removed from the cache.
		// If we stat test2.txt now, test3.txt would normally
		// be removed from the cache. But if we increase the
		// cache size here then that won't happen:
		stat.setMaxSize(3);
		touch("test2.txt", 11);
		touch("test3.txt", 12);
		
		ensure_equals("5th stat succeeded",
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 11);
		
		ensure_equals("6th stat succeeded",
			stat.stat("test3.txt", &buf, 1),
			0);
		ensure_equals("test3.txt is still cached",
			buf.st_mtime,
			(time_t) 3);
		
		ensure_equals("7th stat succeeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("test.txt is still cached",
			buf.st_mtime,
			(time_t) 10);
	}
	
	TEST_METHOD(13) {
		// If we decrease the cache size dynamically, then
		// the oldest entries will be removed.
		SystemTime::force(5);
		CachedFileStat stat(3);
		touch("test.txt", 1);
		touch("test2.txt", 2);
		touch("test3.txt", 3);
		
		ensure_equals("1st stat succeeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("2nd stat succeeded",
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals("3rd stat succeeded",
			stat.stat("test3.txt", &buf, 1),
			0);
		
		// The following should remove test.txt and test2.txt from the cache.
		stat.setMaxSize(1);
		
		touch("test.txt", 10);
		touch("test2.txt", 11);
		touch("test3.txt", 12);
		
		ensure_equals("6th stat succeeded",
			stat.stat("test3.txt", &buf, 1),
			0);
		ensure_equals("test3.txt is still in the cache",
			buf.st_mtime,
			(time_t) 3);
		
		ensure_equals("4th stat succeeded",
			stat.stat("test.txt", &buf, 1),
			0);
		ensure_equals("test.txt is removed from the cache",
			buf.st_mtime,
			(time_t) 10);
		
		ensure_equals("5th stat succeeded",
			stat.stat("test2.txt", &buf, 1),
			0);
		ensure_equals("test2.txt is removed from the cache",
			buf.st_mtime,
			(time_t) 11);
	}
	
	TEST_METHOD(14) {
		// An initial cache size of 0 means that the cache size is unlimited.
		SystemTime::force(1);
		CachedFileStat stat(0);
		
		touch("test.txt", 1);
		touch("test2.txt", 2);
		touch("test3.txt", 3);
		stat.stat("test.txt", &buf, 1);
		stat.stat("test2.txt", &buf, 1);
		stat.stat("test3.txt", &buf, 1);
		
		touch("test.txt", 11);
		touch("test2.txt", 12);
		touch("test3.txt", 13);
		stat.stat("test.txt", &buf, 1);
		ensure_equals(buf.st_mtime, (time_t) 1);
		stat.stat("test2.txt", &buf, 1);
		ensure_equals(buf.st_mtime, (time_t) 2);
		stat.stat("test3.txt", &buf, 1);
		ensure_equals(buf.st_mtime, (time_t) 3);
	}
	
	TEST_METHOD(15) {
		// Setting the cache size dynamically to 0 makes the cache size unlimited.
		SystemTime::force(1);
		CachedFileStat stat(2);
		
		touch("test.txt", 1);
		touch("test2.txt", 2);
		touch("test3.txt", 3);
		stat.stat("test.txt", &buf, 1);
		stat.stat("test2.txt", &buf, 1);
		stat.stat("test3.txt", &buf, 1);
		
		// test.txt is now no longer in the cache.
		
		stat.setMaxSize(0);
		touch("test.txt", 11);
		touch("test2.txt", 12);
		touch("test3.txt", 13);
		stat.stat("test.txt", &buf, 1);
		stat.stat("test2.txt", &buf, 1);
		stat.stat("test3.txt", &buf, 1);
		
		// test.txt should now have been re-statted while test2.txt
		// and test3.txt are still cached.
		
		stat.stat("test.txt", &buf, 1);
		ensure_equals("test.txt is re-statted", buf.st_mtime, (time_t) 11);
		stat.stat("test2.txt", &buf, 1);
		ensure_equals("test2.txt is still cached", buf.st_mtime, (time_t) 2);
		stat.stat("test3.txt", &buf, 1);
		ensure_equals("test3.txt is still cached", buf.st_mtime, (time_t) 3);
	}
	
	TEST_METHOD(16) {
		// Changing the cache size dynamically from 0 to non-0 works;
		// it removes the oldest entries, if necessary.
		CachedFileStat stat(0);
		stat.stat("test.txt", &buf, 1);
		stat.stat("test2.txt", &buf, 1);
		stat.stat("test3.txt", &buf, 1);
		stat.stat("test4.txt", &buf, 1);
		stat.stat("test5.txt", &buf, 1);
		stat.setMaxSize(2);
		ensure("(1)", !stat.knows("test.txt"));
		ensure("(2)", !stat.knows("test2.txt"));
		ensure("(3)", !stat.knows("test3.txt"));
		ensure("(4)", stat.knows("test4.txt"));
		ensure("(5)", stat.knows("test5.txt"));
	}
}
