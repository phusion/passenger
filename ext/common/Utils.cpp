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

#include <cassert>
#include <libgen.h>
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

const char *
getTempDir() {
	const char *temp_dir = getenv("TMPDIR");
	if (temp_dir == NULL || *temp_dir == '\0') {
		temp_dir = "/tmp";
	}
	return temp_dir;
}

string
getPassengerTempDir(bool bypassCache) {
	if (bypassCache) {
		goto calculateResult;
	} else {
		const char *tmp = getenv("PHUSION_PASSENGER_TMP");
		if (tmp != NULL && *tmp != '\0') {
			return tmp;
		} else {
			goto calculateResult;
		}
	}

	calculateResult:
	const char *temp_dir = getTempDir();
	char buffer[PATH_MAX];
	
	snprintf(buffer, sizeof(buffer), "%s/passenger.%lu",
		temp_dir, (unsigned long) getpid());
	buffer[sizeof(buffer) - 1] = '\0';
	setenv("PHUSION_PASSENGER_TMP", buffer, 1);
	return buffer;
}

void
createPassengerTempDir() {
	makeDirTree(getPassengerTempDir().c_str(), "u=rwxs,g=wx,o=wx");
}

void
makeDirTree(const char *path, const char *mode) {
	char command[PATH_MAX + 10];
	snprintf(command, sizeof(command), "mkdir -p -m \"%s\" \"%s\"", mode, path);
	command[sizeof(command) - 1] = '\0';
	
	int result;
	do {
		result = system(command);
	} while (result == -1 && errno == EINTR);
	if (result != 0) {
		char message[1024];
		int e = errno;
		
		snprintf(message, sizeof(message) - 1, "Cannot create directory '%s'", path);
		message[sizeof(message) - 1] = '\0';
		if (result == -1) {
			throw SystemException(message, e);
		} else {
			throw IOException(message);
		}
	}
}

void
removeDirTree(const char *path) {
	char command[PATH_MAX + 10];
	snprintf(command, sizeof(command), "rm -rf \"%s\"", path);
	command[sizeof(command) - 1] = '\0';
	
	int result;
	do {
		result = system(command);
	} while (result == -1 && errno == EINTR);
	if (result == -1) {
		char message[1024];
		
		snprintf(message, sizeof(message) - 1, "Cannot remove directory '%s'", path);
		message[sizeof(message) - 1] = '\0';
		throw IOException(message);
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
