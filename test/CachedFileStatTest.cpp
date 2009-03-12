#include "tut.h"
#include "CachedFileStat.h"
#include "SystemTime.h"
#include <sys/types.h>
#include <utime.h>

using namespace std;
using namespace Passenger;

namespace tut {
	struct CachedFileStatTest {
		CachedFileStat *stat;
		CachedMultiFileStat *mstat;
		
		CachedFileStatTest() {
			stat = (CachedFileStat *) NULL;
			mstat = (CachedMultiFileStat *) NULL;
		}
		
		~CachedFileStatTest() {
			if (stat != NULL) {
				delete stat;
			}
			if (mstat != NULL) {
				cached_multi_file_stat_free(mstat);
			}
			SystemTime::release();
			unlink("test.txt");
			unlink("test2.txt");
			unlink("test3.txt");
			unlink("test4.txt");
		}
	};
	
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
	
	DEFINE_TEST_GROUP(CachedFileStatTest);
	
	/************ Tests for CachedFileStat ************/
	
	TEST_METHOD(1) {
		// cached_file_stat_new() does not stat the file immediately.
		touch("test.txt");
		stat = new CachedFileStat("test.txt");
		ensure_equals((long) stat->info.st_size, (long) 0);
		ensure_equals(stat->info.st_mtime, (time_t) 0);
	}
	
	TEST_METHOD(2) {
		// cached_file_stat_refresh() on a newly created
		// CachedFileStat works.
		touch("test.txt");
		stat = new CachedFileStat("test.txt");
		ensure_equals(stat->refresh(1), 0);
		ensure_equals((long) stat->info.st_size, (long) 2);
	}
	
	TEST_METHOD(3) {
		// cached_file_stat_refresh() does not re-stat the file
		// until the cache has expired.
		SystemTime::force(5);
		stat = new CachedFileStat("test.txt");
		touch("test.txt", 1);
		ensure_equals("1st refresh succceeded",
			stat->refresh(1),
			0);
		
		touch("test.txt", 1000);
		ensure_equals("2nd refresh succceeded",
			stat->refresh(1),
			0);
		ensure_equals("Cached value was used",
			stat->info.st_mtime,
			(time_t) 1);
		
		SystemTime::force(6);
		ensure_equals("3rd refresh succceeded",
			stat->refresh(1),
			0);
		ensure_equals("Cache has been invalidated",
			stat->info.st_mtime,
			(time_t) 1000);
	}
	
	TEST_METHOD(5) {
		// cached_file_stat_refresh() on a nonexistant file returns
		// an error.
		stat = new CachedFileStat("test.txt");
		ensure_equals(stat->refresh(1), -1);
		ensure_equals("It sets errno appropriately", errno, ENOENT);
	}
	
	TEST_METHOD(6) {
		// cached_file_stat_refresh() on a nonexistant file does not
		// re-stat the file until the cache has expired.
		SystemTime::force(5);
		stat = new CachedFileStat("test.txt");
		ensure_equals("1st refresh failed",
			stat->refresh(1),
			-1);
		ensure_equals("It sets errno appropriately", errno, ENOENT);
		
		errno = EEXIST;
		ensure_equals("2nd refresh failed",
			stat->refresh(1),
			-1);
		ensure_equals("It sets errno appropriately", errno, ENOENT);
		ensure_equals("Cached value was used",
			stat->info.st_mtime,
			(time_t) 0);
		
		touch("test.txt", 1000);
		SystemTime::force(6);
		ensure_equals("3rd refresh succeeded",
			stat->refresh(1),
			0);
		ensure_equals("Cache has been invalidated",
			stat->info.st_mtime,
			(time_t) 1000);
		
		unlink("test.txt");
		ensure_equals("4th refresh succeeded even though file was unlinked",
			stat->refresh(1),
			0);
		ensure_equals("Cached value was used",
			stat->info.st_mtime,
			(time_t) 1000);
	}
	
	
	/************ Tests for CachedMultiFileStat ************/
	
	TEST_METHOD(10) {
		// Statting an existing file works.
		struct stat buf;
		
		touch("test.txt");
		mstat = cached_multi_file_stat_new(1);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test.txt", &buf, 0),
			0);
		ensure_equals((long) buf.st_size, (long) 2);
	}
	
	TEST_METHOD(11) {
		// Statting a nonexistant file works.
		struct stat buf;
		
		mstat = cached_multi_file_stat_new(1);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test.txt", &buf, 0),
			-1);
	}
	
	TEST_METHOD(12) {
		// Throttling works.
		struct stat buf;
		
		mstat = cached_multi_file_stat_new(2);
		SystemTime::force(5);
		
		// Touch and stat test.txt. The next stat should return
		// the old info.
		
		touch("test.txt", 10);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 10);
		
		touch("test.txt", 20);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 10);
		
		// Touch and stat test2.txt. The next stat should return
		// the old info.
		
		touch("test2.txt", 30);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 30);
		
		touch("test2.txt", 40);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 30);
		
		// Forward timer, then stat both files again. The most recent
		// information should be returned.
		
		SystemTime::force(6);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 20);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 40);
	}
	
	TEST_METHOD(13) {
		// Cache limiting works.
		struct stat buf;
		
		mstat = cached_multi_file_stat_new(3);
		SystemTime::force(5);
		
		// Create and stat test.txt, test2.txt and test3.txt.
		
		touch("test.txt", 1000);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 1000);
		
		touch("test2.txt", 1001);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test2.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 1001);
		
		touch("test3.txt", 1003);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test3.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 1003);
		
		// Stat test2.txt, then create and stat test4.txt, then touch test.txt.
		// test.txt should have been removed from the cache, and thus
		// upon statting it again its new timestamp should be returned.
		
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test2.txt", &buf, 1),
			0);
		
		touch("test4.txt", 1004);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test4.txt", &buf, 1),
			0);
		
		touch("test.txt", 3000);
		ensure_equals(
			cached_multi_file_stat_perform(mstat, "test.txt", &buf, 1),
			0);
		ensure_equals(buf.st_mtime, (time_t) 3000);
	}
}
