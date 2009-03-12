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
