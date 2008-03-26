#include <iostream>
#include <fstream>
#include "Logging.h"

namespace Passenger {

int _debugLevel = 3;
ostream *_logStream = &cerr;
ostream *_debugStream = &cerr;

void
setDebugFile(const char *logFile) {
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

} // namespace Passenger

