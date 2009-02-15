/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2009 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_CACHED_FILE_STAT_H_
#define _PASSENGER_CACHED_FILE_STAT_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#ifdef __cplusplus
	extern "C" {
#endif

/**
 * CachedFileStat allows one to stat() a file at a throttled rate, in order
 * to minimize stress on the filesystem. It does this by caching the old stat
 * data for a specified amount of time.
 */
typedef struct {
	/** The cached stat info. */
	struct stat info;
	/** The last return value of stat(). */
	int last_result;
	/** The errno set by the last stat() call. */
	int last_errno;
	/** The filename of the file to stat. */
	char *filename;
	/** The last time a stat() was performed. */
	time_t last_time;
} CachedFileStat;

/**
 * Create a new initialized CachedFileStat object. The file will not be
 * stat()ted until you call cached_file_stat_refresh().
 *
 * @param filename The file to stat.
 * @return A CachedFileStat object, or NULL if no memory can be allocated.
 */
CachedFileStat *cached_file_stat_new(const char *filename);

/**
 * Initialize an uninitialized CachedFileStat object.
 *
 * @return 1 if initialization succeeded, 0 if a memory allocation error occurred.
 */
int  cached_file_stat_init(CachedFileStat *stat, const char *filename);

/**
 * Deinitialize a CachedFileStat object.
 */
void cached_file_stat_deinit(CachedFileStat *stat);

/**
 * Free and deinitialize a dynamically allocated CachedFileStat object.
 */
void cached_file_stat_free(CachedFileStat *stat);

/**
 * Re-stat() the file, if necessary. If <tt>throttle_rate</tt> seconds have
 * passed since the last time stat() was called, then the file will be
 * re-stat()ted.
 *
 * The stat information, which may either be the result of a new stat() call
 * or just the old cached information, is be available as <tt>stat->info</tt>.
 *
 * @return 0 if the stat() call succeeded or if no stat() was performed,
 *         -1 if something went wrong. In the latter case, <tt>errno</tt>
 *         will be populated with an appropriate error code.
 */
int cached_file_stat_refresh(CachedFileStat *stat, unsigned int throttle_rate);


/**
 * CachedMultiFileStat allows one to stat() files at a throttled rate, in order
 * to minimize stress on the filesystem. It does this by caching the old stat
 * data for a specified amount of time.
 *
 * Unlike CachedFileStat, which can only stat() one specific file per
 * CachedFileStat object, CachedMultiFileStat can stat() any file. The
 * number of cached stat() information is limited by the given cache size.
 *
 * This class is fully thread-safe.
 */
typedef struct CachedMultiFileStat CachedMultiFileStat;

CachedMultiFileStat *cached_multi_file_stat_new(unsigned int max_size);
void cached_multi_file_stat_free(CachedMultiFileStat *mstat);
int  cached_multi_file_stat_perform(CachedMultiFileStat *mstat,
                                    const char *filename,
                                    struct stat *buf,
                                    unsigned int throttle_rate);

#ifdef __cplusplus
	}
#endif

#endif /* _PASSENGER_CACHED_FILE_STAT_H_ */

