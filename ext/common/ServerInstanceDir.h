/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
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
#include <oxt/backtrace.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Constants.h>
#include <Logging.h>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;
using namespace boost;

/* TODO: I think we should move away from generation dirs in the future.
 * That way we can become immune to existing-directory-in-tmp denial of
 * service attacks. To achieve the same functionality as we do now, each
 * server instance directory is tagged with the control process's PID
 * and a creation timestamp. passenger-status should treat the server instance
 * directory with the most recent creation timestamp as the one to query.
 * For now, the current code does not lead to an exploit.
 */

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

		void create(bool userSwitching, const string &defaultUser,
		            const string &defaultGroup, uid_t webServerWorkerUid,
		            gid_t webServerWorkerGid)
		{
			TRACE_POINT();
			bool runningAsRoot = geteuid() == 0;
			struct passwd *defaultUserEntry;
			uid_t defaultUid;
			gid_t defaultGid;

			defaultUserEntry = getpwnam(defaultUser.c_str());
			if (defaultUserEntry == NULL) {
				throw NonExistentUserException("Default user '" + defaultUser +
					"' does not exist.");
			}
			defaultUid = defaultUserEntry->pw_uid;
			defaultGid = lookupGid(defaultGroup);
			if (defaultGid == (gid_t) -1) {
				throw NonExistentGroupException("Default group '" + defaultGroup +
					"' does not exist.");
			}

			/* We set a very tight permission here: no read or write access for
			 * anybody except the owner. The individual files and subdirectories
			 * decide for themselves whether they're readable by anybody.
			 */
			makeDirTree(path, "u=rwx,g=x,o=x");

			/* Write structure version file. */
			string structureVersionFile = path + "/structure_version.txt";
			createFile(structureVersionFile,
				toString(SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MAJOR_VERSION) + "." +
				toString(SERVER_INSTANCE_DIR_GENERATION_STRUCTURE_MINOR_VERSION),
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

			string passengerVersionFile = path + "/passenger_version.txt";
			createFile(passengerVersionFile,
				PASSENGER_VERSION "\n",
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);


			/* We want the upload buffer directory to be only writable by the web
			 * server's worker processs. Other users may not have any access to this
			 * directory.
			 */
			if (runningAsRoot) {
				makeDirTree(path + "/buffered_uploads", "u=rwx,g=,o=",
					webServerWorkerUid, webServerWorkerGid);
			} else {
				makeDirTree(path + "/buffered_uploads", "u=rwx,g=,o=");
			}

			/* The HelperAgent must be able to connect to an application. */
			if (runningAsRoot) {
				if (userSwitching) {
					/* Each application process may be running as a different user,
					 * so the backends subdirectory must be world-writable.
					 * However we don't want everybody to be able to know the
					 * sockets' filenames, so the directory is not readable.
					 */
					makeDirTree(path + "/backends", "u=rwx,g=wx,o=wx,+t");
				} else {
					/* All application processes are running as defaultUser/defaultGroup,
					 * so make defaultUser/defaultGroup the owner and group of the
					 * subdirecory.
					 *
					 * The directory is not readable as a security precaution:
					 * nobody should be able to know the sockets' filenames without
					 * having access to the application pool.
					 */
					makeDirTree(path + "/backends", "u=rwx,g=x,o=x", defaultUid, defaultGid);
				}
			} else {
				/* All application processes are running as the same user as the web server,
				 * so only allow access for this user.
				 */
				makeDirTree(path + "/backends", "u=rwx,g=,o=");
			}

			owner = true;
		}

	public:
		~Generation() {
			destroy();
		}

		void destroy() {
			if (owner) {
				removeDirTree(path);
			}
		}

		unsigned int getNumber() const {
			return number;
		}

		// The 'const strng &' here is on purpose. The AgentsStarter C
		// functions return the string pointer directly.
		const string &getPath() const {
			return path;
		}

		void detach() {
			owner = false;
		}
	};

	typedef boost::shared_ptr<Generation> GenerationPtr;

private:
	string path;
	bool owner;

	friend class Generation;

	void initialize(const string &path, bool owner) {
		TRACE_POINT();
		struct stat buf;
		int ret;

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

		do {
			ret = lstat(path.c_str(), &buf);
		} while (ret == -1 && errno == EAGAIN);
		if (owner) {
			if (ret == 0) {
				if (S_ISDIR(buf.st_mode)) {
					verifyDirectoryPermissions(path, buf);
				} else {
					throw RuntimeException("'" + path + "' already exists, and is not a directory");
				}
			} else if (errno == ENOENT) {
				createDirectory(path);
			} else {
				int e = errno;
				throw FileSystemException("Cannot lstat '" + path + "'",
					e, path);
			}
		} else if (!S_ISDIR(buf.st_mode)) {
			throw RuntimeException("Server instance directory '" + path +
				"' does not exist");
		}
	}

	void createDirectory(const string &path) const {
		// We do not use makeDirTree() here. If an attacker creates a directory
		// just before we do, then we want to abort because we want the directory
		// to have specific permissions.
		if (mkdir(path.c_str(), parseModeString("u=rwx,g=rx,o=rx")) == -1) {
			int e = errno;
			throw FileSystemException("Cannot create server instance directory '" +
				path + "'", e, path);
		}
		// Explicitly chmod the directory in case the umask is interfering.
		if (chmod(path.c_str(), parseModeString("u=rwx,g=rx,o=rx")) == -1) {
			int e = errno;
			throw FileSystemException("Cannot set permissions on server instance directory '" +
				path + "'", e, path);
		}
		// verifyDirectoryPermissions() checks for the owner/group so we must make
		// sure the server instance directory has that owner/group, even when the
		// parent directory has setgid on.
		if (chown(path.c_str(), geteuid(), getegid()) == -1) {
			int e = errno;
			throw FileSystemException("Cannot change the permissions of the server "
				"instance directory '" + path + "'", e, path);
		}
	}

	/**
	 * When reusing an existing server instance directory, check permissions
	 * so that an attacker cannot pre-create a directory with too liberal
	 * permissions.
	 */
	void verifyDirectoryPermissions(const string &path, struct stat &buf) {
		TRACE_POINT();

		if (buf.st_mode != (S_IFDIR | parseModeString("u=rwx,g=rx,o=rx"))) {
			throw RuntimeException("Tried to reuse existing server instance directory " +
				path + ", but it has wrong permissions");
		} else if (buf.st_uid != geteuid() || buf.st_gid != getegid()) {
			/* The server instance directory is always created by the Watchdog. Its UID/GID never
			 * changes because:
			 * 1. Disabling user switching only lowers the privilege of the HelperAgent.
			 * 2. For the UID/GID to change, the web server must be completely restarted
			 *    (not just graceful reload) so that the control process can change its UID/GID.
			 *    This causes the PID to change, so that an entirely new server instance
			 *    directory is created.
			 */
			throw RuntimeException("Tried to reuse existing server instance directory " +
				path + ", but it has wrong owner and group");
		}
	}

	bool isDirectory(const string &dir, struct dirent *entry) const {
		#ifdef DT_DIR
			if (entry->d_type == DT_DIR) {
				return true;
			} else if (entry->d_type != DT_UNKNOWN) {
				return false;
			}
			// If DT_UNKNOWN, use normal check.
		#endif
		string path = dir;
		path.append("/");
		path.append(entry->d_name);
		return getFileType(path) == FT_DIRECTORY;
	}

public:
	ServerInstanceDir(const string &path, bool owner = true) {
		initialize(path, owner);
	}

	~ServerInstanceDir() {
		destroy();
	}

	void destroy() {
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

	// The 'const strng &' here is on purpose. The AgentsStarter C
	// functions return the string pointer directly.
	const string &getPath() const {
		return path;
	}

	void detach() {
		owner = false;
	}

	GenerationPtr newGeneration(bool userSwitching, const string &defaultUser,
	                            const string &defaultGroup, uid_t webServerWorkerUid,
	                            gid_t webServerWorkerGid)
	{
		GenerationPtr newestGeneration = getNewestGeneration();
		unsigned int newNumber;
		if (newestGeneration != NULL) {
			newNumber = newestGeneration->getNumber() + 1;
		} else {
			newNumber = 0;
		}

		GenerationPtr generation(new Generation(path, newNumber));
		generation->create(userSwitching, defaultUser, defaultGroup,
			webServerWorkerUid, webServerWorkerGid);
		return generation;
	}

	GenerationPtr getGeneration(unsigned int number) const {
		// Must not used boost::make_shared() here because Watchdog.cpp
		// deletes the raw pointer in cleanupAgentsInBackground().
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
			if (isDirectory(path, entry)
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

typedef boost::shared_ptr<ServerInstanceDir> ServerInstanceDirPtr;

} // namespace Passenger

#endif /* _PASSENGER_SERVER_INSTANCE_DIR_H_ */
