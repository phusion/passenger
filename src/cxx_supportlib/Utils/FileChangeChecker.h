/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#ifndef _PASSENGER_CHANGE_FILE_CHECKER_H_
#define _PASSENGER_CHANGE_FILE_CHECKER_H_

#include <string>
#include <cerrno>

#include <Utils/CachedFileStat.hpp>
#include <SystemTools/SystemTime.h>

namespace Passenger {

using namespace std;
using namespace oxt;

/**
 * A utility class for checking for file changes. Example:
 *
 * @code
 * FileChangeChecker checker;
 * checker.changed("foo.txt");   // false
 * writeToFile("foo.txt");
 * checker.changed("foo.txt");   // true
 * checker.changed("foo.txt");   // false
 * @endcode
 *
 * FileChangeChecker uses stat() to retrieve file information. It also
 * supports throttling in order to limit the number of actual stat() calls.
 * This can improve performance on systems where disk I/O is a problem.
 */
class FileChangeChecker {
private:
	struct Entry {
		string filename;
		time_t lastMtime;
		time_t lastCtime;

		Entry(const string &filename) {
			this->filename  = filename;
			this->lastMtime = 0;
			this->lastCtime = 0;
		}
	};

	typedef boost::shared_ptr<Entry> EntryPtr;
	typedef list<EntryPtr> EntryList;
	typedef map<string, EntryList::iterator> EntryMap;

	CachedFileStat cstat;
	unsigned int maxSize;
	EntryList entries;
	EntryMap fileToEntry;

public:
	/**
	 * Create a FileChangeChecker object.
	 *
	 * @param maxSize The maximum size of the internal file list. A size of 0 means unlimited.
	 */
	FileChangeChecker(unsigned int maxSize = 0)
		: cstat(maxSize)
	{
		this->maxSize = maxSize;
	}

	/**
	 * Checks whether, since the last call to changed() with this filename,
	 * the file's timestamp has changed or whether the file has been created
	 * or removed. If the stat() call fails for any other reason (e.g. the
	 * directory is not readable) then this method will return false.
	 *
	 * If this method was called with this filename for the first time, or if
	 * information about this file has since been removed from the internal
	 * file list, then this method will return whether the file is stat()-able.
	 * That is, if the file doesn't exist then it will return false, but if
	 * the directory is not readable then it will also return false.
	 *
	 * @param filename The file to check. Note that two different filename
	 *                 strings are treated as two different files, so you should
	 *                 use absolute filenames if you change working directory
	 *                 often.
	 * @param throttleRate When set to a non-zero value, throttling will be
	 *                     enabled. stat() will be called at most once per
	 *                     throttleRate seconds.
 	 * @throws TimeRetrievalException Something went wrong while retrieving the
	 *         system time.
	 * @throws boost::thread_interrupted
	 */
	bool changed(const string &filename, unsigned int throttleRate = 0) {
		EntryMap::iterator it(fileToEntry.find(filename));
		EntryPtr entry;
		struct stat buf;
		bool result, newEntry = false;
		int ret;

		if (it == fileToEntry.end()) {
			// Filename not in file list.
			// If file list is full, remove the least recently used
			// file list entry.
			if (maxSize != 0 && fileToEntry.size() == maxSize) {
				EntryList::iterator listEnd(entries.end());
				listEnd--;
				string filename((*listEnd)->filename);
				entries.pop_back();
				fileToEntry.erase(filename);
			}

			// Add to file list as most recently used.
			entry = EntryPtr(new Entry(filename));
			entries.push_front(entry);
			fileToEntry[filename] = entries.begin();
			newEntry = true;
		} else {
			// Filename is in file list.
			entry = *it->second;

			// Mark this entry as most recently used.
			entries.erase(it->second);
			entries.push_front(entry);
			fileToEntry[filename] = entries.begin();
		}

		ret = cstat.stat(filename, &buf, throttleRate);
		if (newEntry) {
			// The file's information isn't in the file list.
			if (ret == -1) {
				entry->lastMtime = 0;
				entry->lastCtime = 0;
				return false;
			} else {
				entry->lastMtime = buf.st_mtime;
				entry->lastCtime = buf.st_ctime;
				return true;
			}
		} else {
			// The file's information was already in the file list.
			if (ret == -1 && errno == ENOENT) {
				result = false;
				entry->lastMtime = 0;
				entry->lastCtime = 0;
			} else if (ret == -1) {
				result = false;
			} else {
				result = entry->lastMtime != buf.st_mtime || entry->lastCtime != buf.st_ctime;
				entry->lastMtime = buf.st_mtime;
				entry->lastCtime = buf.st_ctime;
			}
			return result;
		}
	}

	/**
	 * Change the maximum size of the internal file list.
	 *
	 * A size of 0 means unlimited.
	 */
	void setMaxSize(unsigned int maxSize) {
		if (maxSize != 0) {
			int toRemove = fileToEntry.size() - maxSize;
			for (int i = 0; i < toRemove; i++) {
				string filename(entries.back()->filename);
				entries.pop_back();
				fileToEntry.erase(filename);
			}
		}
		this->maxSize = maxSize;
		cstat.setMaxSize(maxSize);
	}

	/**
	 * Returns whether <tt>filename</tt> is in the internal file list.
	 */
	bool knows(const string &filename) const {
		return fileToEntry.find(filename) != fileToEntry.end();
	}
};

} // namespace Passenger

#endif /* _PASSENGER_CHANGE_FILE_CHECKER_H_ */
