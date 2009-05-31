/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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

#include <oxt/system_calls.hpp>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <cassert>
#include <libgen.h>
#include <pwd.h>
#include "CachedFileStat.hpp"
#include "Exceptions.h"
#include "Utils.h"

#define SPAWN_SERVER_SCRIPT_NAME "passenger-spawn-server"

namespace Passenger {

static string passengerTempDir;

int
atoi(const string &s) {
	return ::atoi(s.c_str());
}

long
atol(const string &s) {
	return ::atol(s.c_str());
}

void
split(const string &str, char sep, vector<string> &output) {
	string::size_type start, pos;
	start = 0;
	output.clear();
	while ((pos = str.find(sep, start)) != string::npos) {
		output.push_back(str.substr(start, pos - start));
		start = pos + 1;
	}
	output.push_back(str.substr(start));
}

bool
fileExists(const char *filename, CachedFileStat *cstat, unsigned int throttleRate) {
	return getFileType(filename, cstat, throttleRate) == FT_REGULAR;
}

FileType
getFileType(const char *filename, CachedFileStat *cstat, unsigned int throttleRate) {
	struct stat buf;
	int ret;
	
	if (cstat != NULL) {
		ret = cstat->stat(filename, &buf, throttleRate);
	} else {
		ret = stat(filename, &buf);
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
			message.append(filename);
			message.append("'");
			throw FileSystemException(message, e, filename);
		}
	}
}

string
findSpawnServer(const char *passengerRoot) {
	if (passengerRoot != NULL) {
		string root(passengerRoot);
		if (root.at(root.size() - 1) != '/') {
			root.append(1, '/');
		}
		
		string path(root);
		path.append("bin/passenger-spawn-server");
		if (fileExists(path.c_str())) {
			return path;
		} else {
			path.assign(root);
			path.append("lib/phusion_passenger/passenger-spawn-server");
			return path;
		}
		return path;
	} else {
		const char *path = getenv("PATH");
		if (path == NULL) {
			return "";
		}
	
		vector<string> paths;
		split(getenv("PATH"), ':', paths);
		for (vector<string>::const_iterator it(paths.begin()); it != paths.end(); it++) {
			if (!it->empty() && (*it).at(0) == '/') {
				string filename(*it);
				filename.append("/" SPAWN_SERVER_SCRIPT_NAME);
				if (fileExists(filename.c_str())) {
					return filename;
				}
			}
		}
		return "";
	}
}

string
findApplicationPoolServer(const char *passengerRoot) {
	assert(passengerRoot != NULL);
	string root(passengerRoot);
	if (root.at(root.size() - 1) != '/') {
		root.append(1, '/');
	}
	
	string path(root);
	path.append("ext/apache2/ApplicationPoolServerExecutable");
	if (fileExists(path.c_str())) {
		return path;
	} else {
		path.assign(root);
		path.append("lib/phusion_passenger/ApplicationPoolServerExecutable");
		return path;
	}
}

string
canonicalizePath(const string &path) {
	#ifdef __GLIBC__
		// We're using a GNU extension here. See the 'BUGS'
		// section of the realpath(3) Linux manpage for
		// rationale.
		char *tmp = realpath(path.c_str(), NULL);
		if (tmp == NULL) {
			int e = errno;
			string message;
			
			message = "Cannot resolve the path '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		} else {
			string result(tmp);
			free(tmp);
			return result;
		}
	#else
		char tmp[PATH_MAX];
		if (realpath(path.c_str(), tmp) == NULL) {
			int e = errno;
			string message;
			
			message = "Cannot resolve the path '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		} else {
			return tmp;
		}
	#endif
}

string
resolveSymlink(const string &path) {
	char buf[PATH_MAX];
	ssize_t size;
	
	size = readlink(path.c_str(), buf, sizeof(buf) - 1);
	if (size == -1) {
		if (errno == EINVAL) {
			return path;
		} else {
			int e = errno;
			string message = "Cannot resolve possible symlink '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		}
	} else {
		buf[size] = '\0';
		if (buf[0] == '\0') {
			string message = "The file '";
			message.append(path);
			message.append("' is a symlink, and it refers to an empty filename. This is not allowed.");
			throw FileSystemException(message, ENOENT, path);
		} else if (buf[0] == '/') {
			// Symlink points to an absolute path.
			return buf;
		} else {
			return extractDirName(path) + "/" + buf;
		}
	}
}

string
extractDirName(const string &path) {
	char *path_copy = strdup(path.c_str());
	char *result = dirname(path_copy);
	string result_string(result);
	free(path_copy);
	return result_string;
}

string
escapeForXml(const string &input) {
	string result(input);
	string::size_type input_pos = 0;
	string::size_type input_end_pos = input.size();
	string::size_type result_pos = 0;
	
	while (input_pos < input_end_pos) {
		const unsigned char ch = input[input_pos];
		
		if ((ch >= 'A' && ch <= 'z')
		 || (ch >= '0' && ch <= '9')
		 || ch == '/' || ch == ' ' || ch == '_' || ch == '.') {
			// This is an ASCII character. Ignore it and
			// go to next character.
			result_pos++;
		} else {
			// Not an ASCII character; escape it.
			char escapedCharacter[sizeof("&#255;") + 1];
			int size;
			
			size = snprintf(escapedCharacter,
				sizeof(escapedCharacter) - 1,
				"&#%d;",
				(int) ch);
			if (size < 0) {
				throw std::bad_alloc();
			}
			escapedCharacter[sizeof(escapedCharacter) - 1] = '\0';
			
			result.replace(result_pos, 1, escapedCharacter, size);
			result_pos += size;
		}
		input_pos++;
	}
	
	return result;
}

string
getProcessUsername() {
	struct passwd pwd, *result;
	char strings[1024];
	int ret;
	
	result = (struct passwd *) NULL;
	do {
		ret = getpwuid_r(getuid(), &pwd, strings, sizeof(strings), &result);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		result = (struct passwd *) NULL;
	}
	
	if (result == (struct passwd *) NULL) {
		snprintf(strings, sizeof(strings), "UID %lld", (long long) getuid());
		strings[sizeof(strings) - 1] = '\0';
		return strings;
	} else {
		return result->pw_name;
	}
}

void
determineLowestUserAndGroup(const string &user, uid_t &uid, gid_t &gid) {
	struct passwd *ent;
	
	ent = getpwnam(user.c_str());
	if (ent == NULL) {
		ent = getpwnam("nobody");
	}
	if (ent == NULL) {
		uid = (uid_t) -1;
		gid = (gid_t) -1;
	} else {
		uid = ent->pw_uid;
		gid = ent->pw_gid;
	}
}

const char *
getSystemTempDir() {
	const char *temp_dir = getenv("TMPDIR");
	if (temp_dir == NULL || *temp_dir == '\0') {
		temp_dir = "/tmp";
	}
	return temp_dir;
}

string
getPassengerTempDir(bool bypassCache, const string &parentDir) {
	if (!bypassCache && !passengerTempDir.empty()) {
		return passengerTempDir;
	} else {
		string theParentDir;
		char buffer[PATH_MAX];
		
		if (parentDir.empty()) {
			theParentDir = getSystemTempDir();
		} else {
			theParentDir = parentDir;
		}
		snprintf(buffer, sizeof(buffer), "%s/passenger.%lu",
			theParentDir.c_str(), (unsigned long) getpid());
		buffer[sizeof(buffer) - 1] = '\0';
		passengerTempDir = buffer;
		return passengerTempDir;
	}
}

void
setPassengerTempDir(const string &dir) {
	passengerTempDir = dir;
}

void
createPassengerTempDir(const string &parentDir, bool userSwitching,
                       const string &lowestUser, uid_t workerUid, gid_t workerGid) {
	string tmpDir(getPassengerTempDir(false, parentDir));
	uid_t lowestUid;
	gid_t lowestGid;
	
	determineLowestUserAndGroup(lowestUser, lowestUid, lowestGid);
	
	/* Create the temp directory with the current user as owner (which
	 * is root if the web server was started as root). Only the owner
	 * may write to this directory. Everybody else may only access the
	 * directory. The permissions on the subdirectories will determine
	 * whether a user may access that specific subdirectory.
	 */
	makeDirTree(tmpDir, "u=wxs,g=x,o=x");
	
	/* We want this upload buffer directory to be only accessible by the web server's
	 * worker processs.
	 *
	 * It only makes sense to chown webserver_private to workerUid and workerGid if the web server
	 * is actually able to change the user of the worker processes. That is, if the web server
	 * is running as root.
	 */
	if (geteuid() == 0) {
		makeDirTree(tmpDir + "/webserver_private", "u=wxs,g=,o=", workerUid, workerGid);
	} else {
		makeDirTree(tmpDir + "/webserver_private", "u=wxs,g=,o=");
	}
	
	/* If the web server is running as root (i.e. user switching is possible to begin with)
	 * but user switching is off...
	 */
	if (geteuid() == 0 && !userSwitching) {
		/* ...then the 'info' subdirectory must be owned by lowestUser, so that only root
		 * or lowestUser can query Phusion Passenger information.
		 */
		makeDirTree(tmpDir + "/info", "u=rwxs,g=,o=", lowestUid, lowestGid);
	} else {
		/* Otherwise just use the current user and the directory's owner.
		 * This way, only the user that the web server's control process
		 * is running as will be able to query information.
		 */
		makeDirTree(tmpDir + "/info", "u=rwxs,g=,o=");
	}
	
	if (geteuid() == 0) {
		if (userSwitching) {
			/* When Apache is installed with mpm-itk, there may be multiple
			 * users under which worker processes are running. To ensure that
			 * all worker processes can connect to the server socket, we make
			 * this subdirectory world-executable.
			 */
			makeDirTree(tmpDir + "/master", "u=wxs,g=x,o=x", workerUid, workerGid);
		} else {
			makeDirTree(tmpDir + "/master", "u=wxs,g=x,o=x", lowestUid, lowestGid);
		}
	} else {
		makeDirTree(tmpDir + "/master", "u=wxs,g=,o=");
	}
	
	if (geteuid() == 0) {
		if (userSwitching) {
			/* If user switching is possible and turned on, then each backend
			 * process may be running as a different user, so the backends
			 * subdirectory must be world-writable. However we don't want
			 * everybody to be able to know the sockets' filenames, so
			 * the directory is not readable, not even by its owner.
			 */
			makeDirTree(tmpDir + "/backends", "u=wxs,g=wx,o=wx");
		} else {
			/* If user switching is off then all backend processes will be
			 * running as lowestUser, so make lowestUser the owner of the
			 * directory. Nobody else (except root) may access this directory.
			 *
			 * The directory is not readable as a security precaution:
			 * nobody should be able to know the sockets' filenames without
			 * having access to the application pool.
			 */
			makeDirTree(tmpDir + "/backends", "u=wxs,g=,o=", lowestUid, lowestGid);
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
		makeDirTree(tmpDir + "/backends", "u=wxs,g=,o=");
	}
}

void
makeDirTree(const string &path, const char *mode, uid_t owner, gid_t group) {
	char command[PATH_MAX + 10];
	struct stat buf;
	
	if (stat(path.c_str(), &buf) == 0) {
		return;
	}
	
	snprintf(command, sizeof(command), "mkdir -p -m \"%s\" \"%s\"", mode, path.c_str());
	command[sizeof(command) - 1] = '\0';
	
	int result;
	do {
		result = system(command);
	} while (result == -1 && errno == EINTR);
	if (result != 0) {
		char message[1024];
		int e = errno;
		
		snprintf(message, sizeof(message) - 1, "Cannot create directory '%s'",
			path.c_str());
		message[sizeof(message) - 1] = '\0';
		if (result == -1) {
			throw SystemException(message, e);
		} else {
			throw IOException(message);
		}
	}
	
	if (owner != (uid_t) -1 && group != (gid_t) -1) {
		do {
			result = chown(path.c_str(), owner, group);
		} while (result == -1 && errno == EINTR);
		if (result != 0) {
			char message[1024];
			int e = errno;
			
			snprintf(message, sizeof(message) - 1,
				"Cannot change the directory '%s' its UID to %lld and GID to %lld",
				path.c_str(), (long long) owner, (long long) group);
			message[sizeof(message) - 1] = '\0';
			throw FileSystemException(message, e, path);
		}
	}
}

void
removeDirTree(const string &path) {
	char command[PATH_MAX + 30];
	int result;
	
	snprintf(command, sizeof(command), "chmod -R u+rwx \"%s\" 2>/dev/null", path.c_str());
	command[sizeof(command) - 1] = '\0';
	do {
		result = system(command);
	} while (result == -1 && errno == EINTR);
	
	snprintf(command, sizeof(command), "rm -rf \"%s\"", path.c_str());
	command[sizeof(command) - 1] = '\0';
	do {
		result = system(command);
	} while (result == -1 && errno == EINTR);
	if (result == -1) {
		char message[1024];
		int e = errno;
		
		snprintf(message, sizeof(message) - 1, "Cannot remove directory '%s'", path.c_str());
		message[sizeof(message) - 1] = '\0';
		throw FileSystemException(message, e, path);
	}
}

bool
verifyRailsDir(const string &dir, CachedFileStat *cstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/config/environment.rb");
	return fileExists(temp.c_str(), cstat, throttleRate);
}

bool
verifyRackDir(const string &dir, CachedFileStat *cstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/config.ru");
	return fileExists(temp.c_str(), cstat, throttleRate);
}

bool
verifyWSGIDir(const string &dir, CachedFileStat *cstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/passenger_wsgi.py");
	return fileExists(temp.c_str(), cstat, throttleRate);
}

void
generateSecureToken(void *buf, unsigned int size) {
	FILE *f;
	
	f = syscalls::fopen("/dev/urandom", "r");
	if (f == NULL) {
		throw FileSystemException("Cannot open /dev/urandom",
			errno, "/dev/urandom");
	}
	
	this_thread::disable_syscall_interruption dsi;
	size_t ret = syscalls::fread(buf, 1, size, f);
	syscalls::fclose(f);
	if (ret != size) {
		throw IOException("Cannot read sufficient data from /dev/urandom");
	}
}

string
fillInMiddle(unsigned int max, const string &prefix, const string &middle, const string &postfix) {
	if (max <= prefix.size() + postfix.size()) {
		throw ArgumentException("Impossible to build string with the given size constraint.");
	}
	
	unsigned int fillSize = max - (prefix.size() + postfix.size());
	if (fillSize > middle.size()) {
		return prefix + middle + postfix;
	} else {
		return prefix + middle.substr(0, fillSize) + postfix;
	}
}

string
toHex(const StaticString &data) {
	static const char chars[] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'a', 'b', 'c', 'd', 'e', 'f'
	};
	string result(data.size() * 2, '\0');
	string::size_type i;
	
	for (i = 0; i < data.size(); i++) {
		result[i * 2] = chars[(unsigned char) data.at(i) / 16];
		result[i * 2 + 1] = chars[(unsigned char) data.at(i) % 16];
	}
	return result;
}

int
createUnixServer(const char *filename, unsigned int backlogSize, bool autoDelete) {
	struct sockaddr_un addr;
	int fd, ret;
	
	if (strlen(filename) > sizeof(addr.sun_path) - 1) {
		string message = "Cannot create Unix socket '";
		message.append(filename);
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	fd = syscalls::socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		throw SystemException("Cannot create a Unix socket file descriptor", errno);
	}
	
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
	
	if (autoDelete) {
		do {
			ret = unlink(filename);
		} while (ret == -1 && errno == EINTR);
	}
	
	try {
		ret = syscalls::bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot bind Unix socket '";
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	if (backlogSize == 0) {
		#ifdef SOMAXCONN
			backlogSize = SOMAXCONN;
		#else
			backlogSize = 128;
		#endif
	}
	try {
		ret = syscalls::listen(fd, backlogSize);
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot listen on Unix socket '";
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

int
connectToUnixServer(const char *filename) {
	int fd, ret;
	struct sockaddr_un addr;
	
	if (strlen(filename) > sizeof(addr.sun_path) - 1) {
		string message = "Cannot connect to Unix socket '";
		message.append(filename);
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	do {
		fd = syscalls::socket(PF_UNIX, SOCK_STREAM, 0);
	} while (fd == -1 && errno == EINTR);
	if (fd == -1) {
		throw SystemException("Cannot create a Unix socket file descriptor", errno);
	}
	
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
	
	try {
		ret = syscalls::connect(fd, (const sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message("Cannot connect to Unix socket '");
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

} // namespace Passenger
