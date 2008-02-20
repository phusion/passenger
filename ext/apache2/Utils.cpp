#include <fstream>
#include <iostream>
#include "Utils.h"

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

} // namespace Passenger
