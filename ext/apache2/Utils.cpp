#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include "Utils.h"

#define SPAWN_SERVER_SCRIPT_NAME "passenger-spawn-server"

namespace Passenger {

ostream *_debugStream = NULL;

void
initDebugging(const char *logFile) {
	#ifdef PASSENGER_DEBUG
		if (logFile != NULL) {
			ostream *stream = new ofstream(logFile, ios_base::out | ios_base::app);
			if (stream->fail()) {
				delete stream;
			} else {
				if (_debugStream != NULL && _debugStream != &cerr) {
					delete _debugStream;
				}
				_debugStream = stream;
			}
		} else {
			_debugStream = &cerr;
		}
	#endif
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

} // namespace Passenger
