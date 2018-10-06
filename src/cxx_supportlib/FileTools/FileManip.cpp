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

#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cerrno>
#include <limits>

#include <FileTools/FileManip.h>
#include <FileDescriptor.h>
#include <Exceptions.h>
#include <FileTools/PathManip.h>
#include <ProcessManagement/Spawn.h>
#include <ProcessManagement/Utils.h>
#include <Utils.h> // parseModeString
#include <Utils/CachedFileStat.hpp>
#include <IOTools/IOUtils.h>
#include <Utils/ScopeGuard.h>

namespace Passenger {

using namespace std;
using namespace oxt;


FileGuard::FileGuard(const StaticString &_filename)
	: filename(_filename.data(), _filename.size()),
	  committed(false)
{
	// Do nothing
}

FileGuard::~FileGuard() {
	if (!committed) {
		int ret;
		do {
			ret = unlink(filename.c_str());
		} while (ret == -1 && errno == EINTR);
	}
}

void
FileGuard::commit() {
	committed = true;
}


bool
fileExists(const StaticString &filename, CachedFileStat *cstat, boost::mutex *cstatMutex,
	unsigned int throttleRate)
{
	return getFileType(filename, cstat, cstatMutex, throttleRate) == FT_REGULAR;
}

FileType
getFileType(const StaticString &filename, CachedFileStat *cstat, boost::mutex *cstatMutex,
	unsigned int throttleRate)
{
	struct stat buf;
	int ret;

	if (cstat != NULL) {
		boost::unique_lock<boost::mutex> l;
		if (cstatMutex != NULL) {
			l = boost::unique_lock<boost::mutex>(*cstatMutex);
		}
		ret = cstat->stat(filename, &buf, throttleRate);
	} else {
		ret = stat(string(filename.data(), filename.size()).c_str(), &buf);
	}
	if (ret == 0) {
		if (S_ISREG(buf.st_mode)) {
			return FT_REGULAR;
		} else if (S_ISDIR(buf.st_mode)) {
			return FT_DIRECTORY;
		} else {
			return FT_OTHER;
		}
	} else {
		if (errno == ENOENT) {
			return FT_NONEXISTANT;
		} else {
			int e = errno;
			string message("Cannot stat '");
			message.append(filename.data(), filename.size());
			message.append("'");
			throw FileSystemException(message, e, filename);
		}
	}
}

void
createFile(const string &filename, const StaticString &contents, mode_t permissions, uid_t owner,
	gid_t group, bool overwrite, const char *callerFile, unsigned int callerLine)
{
	FileDescriptor fd;
	int ret, e, options;

	options = O_WRONLY | O_CREAT | O_TRUNC;
	if (!overwrite) {
		options |= O_EXCL;
	}
	do {
		fd.assign(open(filename.c_str(), options, permissions),
			(callerFile == NULL) ? __FILE__ : callerFile,
			(callerLine == 0) ? __LINE__ : callerLine);
	} while (fd == -1 && errno == EINTR);
	if (fd != -1) {
		FileGuard guard(filename);

		// The file permission may not be as expected because of the active
		// umask, so fchmod() it here to ensure correct permissions.
		do {
			ret = fchmod(fd, permissions);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			e = errno;
			throw FileSystemException("Cannot set permissions on " + filename,
				e, filename);
		}

		if (owner != USER_NOT_GIVEN && group != GROUP_NOT_GIVEN) {
			if (owner == USER_NOT_GIVEN) {
				owner = (uid_t) -1; // Don't let fchown change file owner.
			}
			if (group == GROUP_NOT_GIVEN) {
				group = (gid_t) -1; // Don't let fchown change file group.
			}
			do {
				ret = fchown(fd, owner, group);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				e = errno;
				throw FileSystemException("Cannot set ownership for " + filename,
					e, filename);
			}
		}

		try {
			writeExact(fd, contents);
			fd.close();
		} catch (const SystemException &e) {
			throw FileSystemException("Cannot write to file " + filename,
				e.code(), filename);
		}
		guard.commit();
	} else {
		e = errno;
		if (overwrite || e != EEXIST) {
			throw FileSystemException("Cannot create file " + filename,
				e, filename);
		}
	}
}

string
unsafeReadFile(const string &path) {
	int fd = open(path.c_str(), O_RDONLY);
	if (fd != -1) {
		FdGuard guard(fd, __FILE__, __LINE__);
		return readAll(fd, std::numeric_limits<size_t>::max()).first;
	} else {
		int e = errno;
		throw FileSystemException("Cannot open '" + path + "' for reading",
			e, path);
	}
}

pair<string, bool>
safeReadFile(int dirfd, const string &basename, size_t maxSize) {
	if (basename.find('/') != string::npos) {
		throw ArgumentException("basename may not contain slashes");
	}

	int fd = openat(dirfd, basename.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
	if (fd != -1) {
		FdGuard guard(fd, __FILE__, __LINE__);
		return readAll(fd, maxSize);
	} else {
		int e = errno;
		throw FileSystemException("Cannot open '" + basename + "' for reading",
			e, basename);
	}
}

void
makeDirTree(const string &path, const StaticString &mode, uid_t owner, gid_t group) {
	struct stat buf;
	vector<string> paths;
	vector<string>::reverse_iterator rit;
	string current = path;
	mode_t modeBits;
	int ret;

	if (stat(path.c_str(), &buf) == 0) {
		return;
	}

	modeBits = parseModeString(mode);

	/* Create a list of parent paths that don't exist. For example, given
	 * path == "/a/b/c/d/e" and that only /a exists, the list will become
	 * as follows:
	 *
	 * /a/b/c/d
	 * /a/b/c
	 * /a/b
	 */
	while (current != "/" && current != "." && getFileType(current) == FT_NONEXISTANT) {
		paths.push_back(current);
		current = extractDirName(current);
	}

	/* Now traverse the list in reverse order and create directories that don't exist. */
	for (rit = paths.rbegin(); rit != paths.rend(); rit++) {
		current = *rit;

		do {
			ret = mkdir(current.c_str(), modeBits);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			if (errno == EEXIST) {
				// Ignore error and don't chmod/chown.
				continue;
			} else {
				int e = errno;
				throw FileSystemException("Cannot create directory '" + current + "'",
					e, current);
			}
		}

		/* Chmod in order to override the umask. */
		do {
			ret = chmod(current.c_str(), modeBits);
		} while (ret == -1 && errno == EINTR);

		if (owner != USER_NOT_GIVEN && group != GROUP_NOT_GIVEN) {
			if (owner == USER_NOT_GIVEN) {
				owner = (uid_t) -1; // Don't let chown change file owner.
			}
			if (group == GROUP_NOT_GIVEN) {
				group = (gid_t) -1; // Don't let chown change file group.
			}
			do {
				ret = lchown(current.c_str(), owner, group);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				char message[1024];
				int e = errno;

				snprintf(message, sizeof(message) - 1,
					"Cannot change the directory '%s' its UID to %lld and GID to %lld",
					current.c_str(), (long long) owner, (long long) group);
				message[sizeof(message) - 1] = '\0';
				throw FileSystemException(message, e, path);
			}
		}
	}
}

static void
redirectStderrToDevNull() {
	int devnull = open("/dev/null", O_RDONLY);
	if (devnull > 2) {
		dup2(devnull, 2);
		close(devnull);
	}
}

void
removeDirTree(const string &path) {
	{
		const char *command[] = {
			"chmod",
			"-R",
			"u+rwx",
			path.c_str(),
			NULL
		};
		SubprocessInfo info;
		runCommand(command, info, true, true, redirectStderrToDevNull);
	}
	{
		const char *command[] = {
			"rm",
			"-rf",
			path.c_str(),
			NULL
		};
		SubprocessInfo info;
		runCommand(command, info, true, true, redirectStderrToDevNull);
		if (info.status != 0 && info.status != -2) {
			throw RuntimeException("Cannot remove directory '" + path + "'");
		}
	}
}


} // namespace Passenger
