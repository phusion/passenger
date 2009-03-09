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

#include <map>
#include <list>

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

using namespace std;
using namespace boost;
using namespace Passenger;

// CachedMultiFileStat is written in C++, with a C wrapper API around it.
// I'm not going to reinvent my own linked list and hash table in C when I
// can just use the STL.
struct CachedMultiFileStat {
	struct Item {
		string filename;
		CachedFileStat cstat;
		
		Item(const string &filename)
			: cstat(filename)
		{
			this->filename = filename;
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
		ret = item->cstat.refresh(throttleRate);
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
