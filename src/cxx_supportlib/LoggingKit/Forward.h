/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_LOGGING_KIT_FORWARD_H_
#define _PASSENGER_LOGGING_KIT_FORWARD_H_

#include <StaticString.h>
#include <jsoncpp/json.h>

namespace Passenger {
	#ifndef _PASSENGER_FAST_STRING_STREAM_FORWARD_DECLARED_
		#define _PASSENGER_FAST_STRING_STREAM_FORWARD_DECLARED_
		template<size_t staticCapacity = 1024>
		class FastStringStream;
	#endif

	namespace ConfigKit {
		class Schema;
		class Store;
		class Error;
	}
}

namespace Passenger {
namespace LoggingKit {


class Schema;
struct ConfigRealization;
class Context;

enum Level {
	CRIT   = 0,
	ERROR  = 1,
	WARN   = 2,
	NOTICE = 3,
	INFO   = 4,
	DEBUG  = 5,
	DEBUG2 = 6,
	DEBUG3 = 7,

	UNKNOWN_LEVEL = 99
};

enum TargetType {
	STDERR_TARGET,
	FILE_TARGET,
	NO_TARGET,
	UNKNOWN_TARGET
};

extern Context *context;

void shutdown();

const char *_strdupFastStringStream(const FastStringStream<> &stream);
bool _passesLogLevel(const Context *context, Level level, const ConfigRealization **outputConfigRlz);
bool _shouldLogFileDescriptors(const Context *context, const ConfigRealization **outputConfigRlz);
void _prepareLogEntry(FastStringStream<> &sstream, Level level, const char *file, unsigned int line);
void _writeLogEntry(const ConfigRealization *configRlz, const char *str, unsigned int size);
void _writeFileDescriptorLogEntry(const ConfigRealization *configRlz, const char *str, unsigned int size);

Level getLevel();
void setLevel(Level level);
Level parseLevel(const StaticString &name);
StaticString levelToString(Level level);


} // namespace LoggingKit
} // namespace Passenger

#endif /* _PASSENGER_LOGGING_KIT_FORWARD_H_ */
