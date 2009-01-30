#include "tut.h"
#include "CachedFileStat.h"
#include "SystemTime.h"
#include <sys/types.h>
#include <utime.h>

using namespace std;

namespace tut {
	struct CachedFileStatTest {
		CachedFileStat *stat;
		
		CachedFileStatTest() {
			stat = (CachedFileStat *) NULL;
		}
		
		~CachedFileStatTest() {
			if (stat != NULL) {
				cached_file_stat_free(stat);
			}
			passenger_system_time_release_forced_value();
			unlink("test.txt");
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
	
	TEST_METHOD(1) {
		// cached_file_stat_new() does not stat the file immediately.
		touch("test.txt");
		stat = cached_file_stat_new("test.txt");
		ensure_equals((long) stat->info.st_size, (long) 0);
		ensure_equals(stat->info.st_mtime, (time_t) 0);
	}
	
	TEST_METHOD(2) {
		// cached_file_stat_refresh() on a newly created
		// CachedFileStat works.
		touch("test.txt");
		stat = cached_file_stat_new("test.txt");
		ensure_equals(cached_file_stat_refresh(stat, 1), 0);
		ensure_equals((long) stat->info.st_size, (long) 2);
	}
	
	TEST_METHOD(3) {
		// cached_file_stat_refresh() does not re-stat the file
		// until the cache has expired.
		passenger_system_time_force_value(5);
		stat = cached_file_stat_new("test.txt");
		touch("test.txt", 1);
		ensure_equals("1st refresh succceeded",
			cached_file_stat_refresh(stat, 1),
			0);
		
		touch("test.txt", 1000);
		ensure_equals("2nd refresh succceeded",
			cached_file_stat_refresh(stat, 1),
			0);
		ensure_equals("Cached value was used",
			stat->info.st_mtime,
			(time_t) 1);
		
		passenger_system_time_force_value(6);
		ensure_equals("3rd refresh succceeded",
			cached_file_stat_refresh(stat, 1),
			0);
		ensure_equals("Cache has been invalidated",
			stat->info.st_mtime,
			(time_t) 1000);
	}
	
	TEST_METHOD(5) {
		// cached_file_stat_refresh() on a nonexistant file returns
		// an error.
		stat = cached_file_stat_new("test.txt");
		ensure_equals(cached_file_stat_refresh(stat, 1), -1);
	}
	
	TEST_METHOD(6) {
		// cached_file_stat_refresh() on a nonexistant file does not
		// re-stat the file until the cache has expired.
		passenger_system_time_force_value(5);
		stat = cached_file_stat_new("test.txt");
		ensure_equals("1st refresh failed",
			cached_file_stat_refresh(stat, 1),
			-1);
		ensure_equals("It sets errno appropriately", errno, ENOENT);
		
		ensure_equals("2nd refresh failed",
			cached_file_stat_refresh(stat, 1),
			-1);
		ensure_equals("Cached value was used",
			stat->info.st_mtime,
			(time_t) 0);
		
		touch("test.txt", 1000);
		passenger_system_time_force_value(6);
		ensure_equals("3rd refresh succeeded",
			cached_file_stat_refresh(stat, 1),
			0);
		ensure_equals("Cache has been invalidated",
			stat->info.st_mtime,
			(time_t) 1000);
		
		unlink("test.txt");
		ensure_equals("4th refresh succeeded even though file was unlinked",
			cached_file_stat_refresh(stat, 1),
			0);
		ensure_equals("Cached value was used",
			stat->info.st_mtime,
			(time_t) 1000);
	}
}
