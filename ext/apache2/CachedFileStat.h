/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2009  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
 * CachedFileStat allows one to stat() files at a throttled rate, in order
 * to minimize stress on the filesystem. It does this by caching the old stat
 * data for a specified amount of time.
 */

typedef struct {
	/** The cached stat info. */
	struct stat info;
	/** The last return value of stat(). */
	int result;
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

#ifdef __cplusplus
	}
#endif

#endif /* _PASSENGER_CACHED_FILE_STAT_H_ */

