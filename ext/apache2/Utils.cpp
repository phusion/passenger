/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
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

#include <cassert>
#include <pwd.h>
#include "CachedFileStat.h"
#include "Utils.h"

#define SPAWN_SERVER_SCRIPT_NAME "passenger-spawn-server"

namespace Passenger {

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
fileExists(const char *filename, CachedMultiFileStat *mstat, unsigned int throttleRate) {
	return getFileType(filename, mstat, throttleRate) == FT_REGULAR;
}

FileType
getFileType(const char *filename, CachedMultiFileStat *mstat, unsigned int throttleRate) {
	struct stat buf;
	int ret;
	
	if (mstat != NULL) {
		ret = cached_multi_file_stat_perform(mstat, filename, &buf, throttleRate);
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
getPassengerTempDir(bool bypassCache, const string &systemTempDir) {
	if (bypassCache) {
		goto calculateResult;
	} else {
		const char *tmp = getenv("PASSENGER_INSTANCE_TEMP_DIR");
		if (tmp != NULL && *tmp != '\0') {
			return tmp;
		} else {
			goto calculateResult;
		}
	}

	calculateResult:
	const char *temp_dir;
	char buffer[PATH_MAX];
	
	if (systemTempDir.empty()) {
		temp_dir = getSystemTempDir();
	} else {
		temp_dir = systemTempDir.c_str();
	}
	snprintf(buffer, sizeof(buffer), "%s/passenger.%lu",
		temp_dir, (unsigned long) getpid());
	buffer[sizeof(buffer) - 1] = '\0';
	setenv("PASSENGER_INSTANCE_TEMP_DIR", buffer, 1);
	return buffer;
}

void
createPassengerTempDir(const string &systemTempDir, bool userSwitching,
                       const string &lowestUser, uid_t workerUid, gid_t workerGid) {
	string tmpDir(getPassengerTempDir(false, systemTempDir));
	uid_t lowestUid;
	gid_t lowestGid;
	
	determineLowestUserAndGroup(lowestUser, lowestUid, lowestGid);
	
	makeDirTree(tmpDir, "u=wxs,g=x,o=x");
	
	/* It only makes sense to chown webserver_private to workerUid and workerGid the web server
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
		// Then the 'info' subfolder must be owned by lowestUser.
		makeDirTree(tmpDir + "/info", "u=rwxs,g=,o=", lowestUid, lowestGid);
	} else {
		// Otherwise just use current user.
		makeDirTree(tmpDir + "/info", "u=rwxs,g=,o=");
	}
	
	if (geteuid() == 0) {
		if (userSwitching) {
			makeDirTree(tmpDir + "/backends", "u=rwxs,g=wx,o=wx");
		} else {
			makeDirTree(tmpDir + "/backends", "u=rwxs,g=x,o=x", lowestUid, lowestGid);
		}
	} else {
		makeDirTree(tmpDir + "/backends", "u=rwxs,g=x,o=x");
	}
	
	if (geteuid() == 0) {
		if (userSwitching) {
			makeDirTree(tmpDir + "/var", "u=wxs,g=wx,o=wx");
		} else {
			makeDirTree(tmpDir + "/var", "u=wxs,g=,o=", lowestUid, lowestGid);
		}
	} else {
		makeDirTree(tmpDir + "/var", "u=wxs,g=,o=");
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
verifyRailsDir(const string &dir, CachedMultiFileStat *mstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/config/environment.rb");
	return fileExists(temp.c_str(), mstat, throttleRate);
}

bool
verifyRackDir(const string &dir, CachedMultiFileStat *mstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/config.ru");
	return fileExists(temp.c_str(), mstat, throttleRate);
}

bool
verifyWSGIDir(const string &dir, CachedMultiFileStat *mstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/passenger_wsgi.py");
	return fileExists(temp.c_str(), mstat, throttleRate);
}

} // namespace Passenger
