#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <climits>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include "Utils.h"

#define SPAWN_SERVER_SCRIPT_NAME "passenger-spawn-server"

namespace Passenger {

int
atoi(const string &s) {
	return ::atoi(s.c_str());
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

bool fileExists(const char *filename) {
	struct stat buf;
	
	if (stat(filename, &buf) == 0) {
		return S_ISREG(buf.st_mode);
	} else {
		return false;
	}
}

string
findSpawnServer() {
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

string
canonicalizePath(const string &path) {
	#ifdef __GLIBC__
		// We're using a GNU extension here. See the 'BUGS'
		// section of the realpath(3) Linux manpage for
		// rationale.
		char *tmp = realpath(path.c_str(), NULL);
		if (tmp == NULL) {
			return "";
		} else {
			string result(tmp);
			free(tmp);
			return result;
		}
	#else
		char tmp[PATH_MAX];
		if (realpath(path.c_str(), tmp) == NULL) {
			return "";
		} else {
			return tmp;
		}
	#endif
}

bool
verifyRailsDir(const string &dir) {
	string temp(dir);
	temp.append("/../config/environment.rb");
	return fileExists(temp.c_str());
}

} // namespace Passenger
