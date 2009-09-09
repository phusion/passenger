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
#ifndef _PASSENGER_SERVER_INSTANCE_DIR_H_
#define _PASSENGER_SERVER_INSTANCE_DIR_H_

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Exceptions.h"
#include "Utils.h"

namespace Passenger {

using namespace std;
using namespace boost;

class ServerInstanceDir: public noncopyable {
public:
	class Generation: public noncopyable {
	private:
		friend class ServerInstanceDir;
		
		string path;
		unsigned int number;
		bool owner;
		
		Generation(const string &serverInstanceDir, unsigned int number) {
			path = serverInstanceDir + "/generation-" + toString(number);
			this->number = number;
			owner = false;
		}
		
		void create(bool userSwitching, const string &defaultUser, uid_t workerUid, gid_t workerGid) {
			uid_t defaultUid;
			gid_t defaultGid;
			
			determineLowestUserAndGroup(defaultUser, defaultUid, defaultGid);
			
			/* We set a very tight permission here: no read or write access for
			 * anybody except the owner. The individual files and subdirectories
			 * decide for themselves whether they're readable by anybody.
			 */
			makeDirTree(path, "u=wxs,g=x,o=x");
			
			
			/* We want the upload buffer directory to be only accessible by the web
			 * server's worker processs.
			 *
			 * It only makes sense to chown this directory to workerUid and workerGid
			 * if the web server is actually able to change the user of the worker
			 * processes. That is, if the web server is running as root.
			 */
			if (geteuid() == 0) {
				makeDirTree(path + "/webserver_private", "u=wxs,g=,o=", workerUid, workerGid);
			} else {
				makeDirTree(path + "/webserver_private", "u=wxs,g=,o=");
			}
			
			/* The master directory contains a socket which all Nginx worker processes
			 * connect to, so it should only be executable by the Nginx worker processes.
			 * We have this directory because filesystem permissions on a socket might
			 * not be portable.
			 */
			if (geteuid() == 0) {
				/* If we're running as root then we don't need write access
				 * for this directory because we'll create the contents before
				 * we lower our privileges.
				 */
				if (userSwitching) {
					makeDirTree(path + "/master", "u=xs,g=,o=", workerUid, workerGid);
				} else {
					makeDirTree(path + "/master", "u=xs,g=,o=", defaultUid, defaultGid);
				}
			} else {
				makeDirTree(path + "/master", "u=wxs,g=,o=");
			}
			
			if (geteuid() == 0) {
				if (userSwitching) {
					/* If user switching is possible and turned on, then each backend
					 * process may be running as a different user, so the backends
					 * subdirectory must be world-writable. However we don't want
					 * everybody to be able to know the sockets' filenames, so
					 * the directory is not readable, not even by its owner.
					 */
					makeDirTree(path + "/backends", "u=wxs,g=wx,o=wx");
				} else {
					/* If user switching is off then all backend processes will be
					 * running as lowestUser, so make lowestUser the owner of the
					 * directory. Nobody else (except root) may access this directory.
					 *
					 * The directory is not readable as a security precaution:
					 * nobody should be able to know the sockets' filenames without
					 * having access to the application pool.
					 */
					makeDirTree(path + "/backends", "u=wxs,g=,o=", defaultUid, defaultGid);
				}
			} else {
				/* If user switching is not possible then all backend processes will
				 * be running as the same user as the web server. So we'll make the
				 * backends subdirectory only writable by this user. Nobody else
				 * (except root) may access this subdirectory.
				 *
				 * The directory is not readable as a security precaution:
				 * nobody should be able to know the sockets' filenames without having
				 * access to the application pool.
				 */
				makeDirTree(path + "/backends", "u=wxs,g=,o=");
			}
			
			owner = true;
		}
	
	public:
		~Generation() {
			if (owner) {
				removeDirTree(path);
			}
		}
		
		unsigned int getNumber() const {
			return number;
		}
		
		string getPath() const {
			return path;
		}
	};
	
	typedef shared_ptr<Generation> GenerationPtr;
	
private:
	string path;
	bool owner;
	
	void initialize(const string &path, bool create) {
		this->path = path;
		owner = create;
		if (!create) {
			return;
		}
		
		/* Create the server instance directory. We only need to write to this
		 * directory (= creating/removing files or subdirectories) for these
		 * reasons:
		 * - Initial population of structure files (structure_version.txt, instance.pid).
		 * - To create/remove a generation directory.
		 * - To remove the entire server instance directory (after all
		 *   generations are removed).
		 *
		 * All these things are done by the helper server, so the directory is owned
		 * by the process the helper server is running as, and only writable by this
		 * owner. Everybody else have read and execution rights:
		 * - The former is necessary for allowing admin tools to query the list of
		 *   generation directories. The permissions on the generation directory
		 *   are used to further lock down security.
		 * - The latter is necessary for allowing access to the generation directories
		 *   as well as access to the structure files. The structure files must be
		 *   world-readable because admin tools must at least be able to see which
		 *   server instances are running, regardless of the user it was started as.
		 */
		makeDirTree(path, "u=rwxs,g=rx,o=rx");
		
		/* Write structure version file. If you change the version here don't forget
		 * to do it in lib/phusion_passenger/admin_tools/server_instance.rb too.
		 *
		 * Once written, nobody may write to it; only reading is possible.
		 */
		string structureVersionFile = path + "/structure_version.txt";
		createFile(structureVersionFile, "1.0", /* major.minor */
			S_IRUSR | S_IRGRP | S_IROTH);
	}
	
public:
	ServerInstanceDir(pid_t webServerPid, const string &parentDir = "") {
		string theParentDir;
		
		if (parentDir.empty()) {
			theParentDir = getSystemTempDir();
		} else {
			theParentDir = parentDir;
		}
		initialize(theParentDir + "/passenger." + toString<unsigned long long>(webServerPid),
			true);
	}
	
	ServerInstanceDir(const string &path) {
		initialize(path, false);
	}
	
	~ServerInstanceDir() {
		if (owner && getNewestGeneration() == NULL) {
			removeDirTree(path);
		}
	}
	
	string getPath() const {
		return path;
	}
	
	GenerationPtr newGeneration(bool userSwitching, const string &defaultUser, uid_t workerUid, gid_t workerGid) {
		GenerationPtr newestGeneration = getNewestGeneration();
		unsigned int newNumber;
		if (newestGeneration != NULL) {
			newNumber = newestGeneration->getNumber() + 1;
		} else {
			newNumber = 0;
		}
		
		GenerationPtr generation(new Generation(path, newNumber));
		generation->create(userSwitching, defaultUser, workerUid, workerGid);
		return generation;
	}
	
	GenerationPtr getGeneration(unsigned int number) const {
		return ptr(new Generation(path, number));
	}
	
	GenerationPtr getNewestGeneration() const {
		DIR *dir = opendir(path.c_str());
		struct dirent *entry;
		int result = -1;
		
		if (dir == NULL) {
			int e = errno;
			throw FileSystemException("Cannot open directory " + path,
				e, path);
		}
		while ((entry = readdir(dir)) != NULL) {
			if (entry->d_type == DT_DIR
			 && strncmp(entry->d_name, "generation-", sizeof("generation-") - 1) == 0) {
				const char *numberString = entry->d_name + sizeof("generation-") - 1;
				int number = atoi(numberString);
				if (number >= 0 && number > result) {
					result = number;
				}
			}
		}
		closedir(dir);
		
		if (result == -1) {
			return GenerationPtr();
		} else {
			return getGeneration(result);
		}
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SERVER_INSTANCE_DIR_H_ */
