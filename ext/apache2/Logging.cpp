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
#include <iostream>
#include <fstream>
#include "Logging.h"

namespace PhusionPassenger {

unsigned int _logLevel = 0;
ostream *_logStream = &cerr;
ostream *_debugStream = &cerr;

unsigned int
getLogLevel() {
	return _logLevel;
}

void
setLogLevel(unsigned int value) {
	_logLevel = value;
}

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

} // namespace PhusionPassenger

