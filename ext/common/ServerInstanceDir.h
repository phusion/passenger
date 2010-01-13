/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
	// Don't forget to update lib/phusion_passenger/admin_tools/server_instance.rb too.
	static const int DIR_STRUCTURE_MAJOR_VERSION = 1;
	static const int DIR_STRUCTURE_MINOR_VERSION = 0;
	static const int GENERATION_STRUCTURE_MAJOR_VERSION = 1;
	static const int GENERATION_STRUCTURE_MINOR_VERSION = 0;
	
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
			bool runningAsRoot = geteuid() == 0;
			uid_t defaultUid;
			gid_t defaultGid;
			
			determineLowestUserAndGroup(defaultUser, defaultUid, defaultGid);
			
			/* We set a very tight permission here: no read or write access for
			 * anybody except the owner. The individual files and subdirectories
			 * decide for themselves whether they're readable by anybody.
			 */
			makeDirTree(path, "u=wxs,g=x,o=x");
			ServerInstanceDir::createNonWritableFifo(path + "/.guard");
			
			/* Write structure version file. */
			string structureVersionFile = path + "/structure_version.txt";
			createFile(structureVersionFile,
				toString(GENERATION_STRUCTURE_MAJOR_VERSION) + "." +
				toString(GENERATION_STRUCTURE_MINOR_VERSION),
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			
			
			/* We want the upload buffer directory to be only writable by the web
			 * server's worker processs. Other users may not have any access to this
			 * directory.
			 */
			if (runningAsRoot) {
				makeDirTree(path + "/buffered_uploads", "u=wxs,g=,o=", workerUid, workerGid);
			} else {
				makeDirTree(path + "/buffered_uploads", "u=wxs,g=,o=");
			}
			ServerInstanceDir::createNonWritableFifo(path + "/buffered_uploads/.guard");
			
			if (runningAsRoot) {
				/* If we're running as root then the directory should not be writable
				 * by the owner, because we'll create the contents before lowering
				 * privileges.
				 */
				makeDirTree(path + "/webserver_shared_resources", "u=xs,g=,o=", workerUid, workerGid);
			} else {
				makeDirTree(path + "/webserver_shared_resources", "u=wxs,g=,o=");
			}
			ServerInstanceDir::createNonWritableFifo(path + "/webserver_shared_resources/.guard");
			
			if (runningAsRoot) {
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
					 * running as defaultUser, so make defaultUser the owner of the
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
			ServerInstanceDir::createNonWritableFifo(path + "/backends/.guard");
			
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
		
		void detach() {
			owner = false;
		}
	};
	
	typedef shared_ptr<Generation> GenerationPtr;
	
private:
	string path;
	bool owner;
	
	friend class Generation;
	
	/* Creates a non-writable FIFO file in order to prevent /tmp cleaners from removing
	 * passenger temp dir subdirectories. See http://code.google.com/p/phusion-passenger/issues/detail?id=365
	 * for details.
	 */
	static void
	createNonWritableFifo(const string &filename) {
		int ret, e;
		
		do {
			ret = mkfifo(filename.c_str(), 0);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1 && errno != EEXIST) {
			e = errno;
			throw FileSystemException("Cannot create FIFO file " + filename, e, filename);
		}
		
		do {
			ret = chmod(filename.c_str(), 0);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			e = errno;
			throw FileSystemException("Cannot set permissions on file " + filename, e, filename);
		}
	}
	
	void initialize(const string &path, bool owner) {
		this->path  = path;
		this->owner = owner;
		
		/* Create the server instance directory. We only need to write to this
		 * directory for these reasons:
		 * 1. Initial population of structure files (structure_version.txt, instance.pid).
		 * 2. Creating/removing a generation directory.
		 * 3. Removing the entire server instance directory (after all
		 *    generations are removed).
		 *
		 * 1 and 2 are done by the helper server during initialization and before lowering
		 * privilege. 3 is done during helper server shutdown by a cleanup process that's
		 * running as the same user the helper server was running as before privilege
		 * lowering.
		 * Therefore, we make the directory only writable by the user the helper server
		 * was running as before privilege is lowered. Everybody else has read and execute
		 * rights though, because we want admin tools to be able to list the available
		 * generations no matter what user they're running as.
		 */
		makeDirTree(path, "u=rwxs,g=rx,o=rx");
		createNonWritableFifo(path + "/.guard");
	}
	
public:
	ServerInstanceDir(pid_t webServerPid, const string &parentDir = "", bool owner = true) {
		string theParentDir;
		
		if (parentDir.empty()) {
			theParentDir = getSystemTempDir();
		} else {
			theParentDir = parentDir;
		}
		
		/* We embed the super structure version in the server instance directory name
		 * because it's possible to upgrade Phusion Passenger without changing the
		 * web server's PID. This way each incompatible upgrade will use its own
		 * server instance directory.
		 */
		initialize(theParentDir + "/passenger." +
			toString(DIR_STRUCTURE_MAJOR_VERSION) + "." +
			toString(DIR_STRUCTURE_MINOR_VERSION) + "." +
			toString<unsigned long long>(webServerPid),
			owner);
		
	}
	
	ServerInstanceDir(const string &path, bool owner = true) {
		initialize(path, owner);
	}
	
	~ServerInstanceDir() {
		if (owner) {
			GenerationPtr newestGeneration;
			try {
				newestGeneration = getNewestGeneration();
			} catch (const FileSystemException &e) {
				if (e.code() == ENOENT) {
					return;
				} else {
					throw;
				}
			}
			if (newestGeneration == NULL) {
				removeDirTree(path);
			}
		}
	}
	
	string getPath() const {
		return path;
	}
	
	void detach() {
		owner = false;
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

typedef shared_ptr<ServerInstanceDir> ServerInstanceDirPtr;

} // namespace Passenger

#endif /* _PASSENGER_SERVER_INSTANCE_DIR_H_ */
