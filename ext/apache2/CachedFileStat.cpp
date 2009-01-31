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
#include "CachedFileStat.h"
#include "SystemTime.h"

#include <map>
#include <list>

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#include <stdlib.h>
#include <errno.h>

using namespace std;
using namespace boost;


CachedFileStat *
cached_file_stat_new(const char *filename) {
	CachedFileStat *stat;
	
	stat = (CachedFileStat *) malloc(sizeof(CachedFileStat));
	if (stat != NULL) {
		if (!cached_file_stat_init(stat, filename)) {
			free(stat);
			return NULL;
		}
	}
	return stat;
}

int
cached_file_stat_init(CachedFileStat *stat, const char *filename) {
	memset(&stat->info, 0, sizeof(struct stat));
	stat->result = -1;
	stat->filename = strdup(filename);
	if (stat->filename == NULL) {
		return 0;
	}
	stat->last_time = 0;
	return 1;
}

void
cached_file_stat_deinit(CachedFileStat *stat) {
	free(stat->filename);
}

void
cached_file_stat_free(CachedFileStat *stat) {
	cached_file_stat_deinit(stat);
	free(stat);
}

/**
 * Checks whether <em>interval</em> seconds have elapsed since <em>begin</em>
 * The current time is returned via the <tt>current_time</tt> argument,
 * so that the caller doesn't have to call time() again if it needs the current
 * time.
 *
 * @pre begin <= time(NULL)
 * @return 1 if <tt>interval</tt> seconds have elapsed since <tt>begin</tt>,
 *         0 if not, -1 if an error occurred. If an error occurred then the
 *         error code can be found in <tt>errno</tt>.
 */
static int
expired(time_t begin, unsigned int interval, time_t *current_time) {
	*current_time = passenger_system_time_get();
	if (*current_time == (time_t) - 1) {
		return -1;
	} else {
		return (unsigned int) (*current_time - begin) >= interval;
	}
}

int
cached_file_stat_refresh(CachedFileStat *cstat, unsigned int throttle_rate) {
	time_t current_time;
	int ret;
	
	ret = expired(cstat->last_time, throttle_rate, &current_time);
	if (ret == -1) {
		return -1;
	} else if (ret == 0) {
		return cstat->result;
	} else {
		ret = stat(cstat->filename, &cstat->info);
		if (ret == -1 && errno == EINTR) {
			/* If the stat() call was interrupted, then don't
			 * update any state so that the caller can call
			 * this function again without us returning a
			 * cached EINTR error.
			 */
			return -1;
		} else {
			cstat->result = ret;
			cstat->last_time = current_time;
			return ret;
		}
	}
}


// CachedMultiFileStat is written in C++, with a C wrapper API around it.
// I'm not going to reinvent my own linked list and hash table in C when I
// can just use the STL.
struct CachedMultiFileStat {
	struct Item {
		string filename;
		CachedFileStat cstat;
		
		Item(const string &filename) {
			this->filename = filename;
			cached_file_stat_init(&cstat, filename.c_str());
		}
		
		~Item() {
			cached_file_stat_deinit(&cstat);
		}
	};
	
	typedef shared_ptr<Item> ItemPtr;
	typedef list<ItemPtr> ItemList;
	typedef map<string, ItemList::iterator> ItemMap;
	
	unsigned int maxSize;
	ItemList items;
	ItemMap cache;
	boost::mutex lock;
	
	CachedMultiFileStat(unsigned int maxSize) {
		this->maxSize = maxSize;
	}
	
	int stat(const string &filename, struct stat *buf, unsigned int throttleRate = 0) {
		boost::unique_lock<boost::mutex> l(lock);
		ItemMap::iterator it(cache.find(filename));
		ItemPtr item;
		int ret;
		
		if (it == cache.end()) {
			// Filename not in cache.
			// If cache is full, remove the least recently used
			// cache entry.
			if (cache.size() == maxSize) {
				ItemList::iterator listEnd(items.end());
				listEnd--;
				string filename((*listEnd)->filename);
				items.pop_back();
				cache.erase(filename);
			}
			
			// Add to cache as most recently used.
			item = ItemPtr(new Item(filename));
			items.push_front(item);
			cache[filename] = items.begin();
		} else {
			// Cache hit.
			item = *it->second;
			
			// Mark this cache item as most recently used.
			items.erase(it->second);
			items.push_front(item);
			cache[filename] = items.begin();
		}
		ret = cached_file_stat_refresh(&item->cstat, throttleRate);
		*buf = item->cstat.info;
		return ret;
	}
};

CachedMultiFileStat *
cached_multi_file_stat_new(unsigned int max_size) {
	return new CachedMultiFileStat(max_size);
}

void
cached_multi_file_stat_free(CachedMultiFileStat *mstat) {
	delete mstat;
}

int
cached_multi_file_stat_perform(CachedMultiFileStat *mstat,
                               const char *filename,
                               struct stat *buf,
                               unsigned int throttle_rate)
{
	return mstat->stat(filename, buf, throttle_rate);
}
