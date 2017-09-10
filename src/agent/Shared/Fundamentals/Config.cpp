/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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

#include <Shared/Fundamentals/Config.h>

namespace Passenger {
namespace Agent {
namespace Fundamentals {


Schema::Schema() {
	using namespace ConfigKit;

	loggingKit.translator.add("log_level", "level");
	loggingKit.translator.add("log_target", "target");
	loggingKit.translator.finalize();
	addSubSchema(loggingKit.schema, loggingKit.translator);

	add();

	addWithDynamicDefault("abort_handler", BOOL_TYPE, OPTIONAL | READ_ONLY | CACHE_DEFAULT_VALUE,
		getDefaultAbortHandler);
	add("abort_handler_dump_with_crash_watch", BOOL_TYPE, OPTIONAL | READ_ONLY, true);
	add("abort_handler_beep", BOOL_TYPE, OPTIONAL | READ_ONLY, false);
	add("abort_handler_stop_process", BOOL_TYPE, OPTIONAL | READ_ONLY, false);

	finalize();
}

Json::Value
Schema::getDefaultAbortHandler(const ConfigKit::Store &config) {
	return getEnvBool("PASSENGER_ABORT_HANDLER", true);
}

ConfigRealization::ConfigRealization(const ConfigKit::Store &config) {
	abortHandler.enabled = config["abort_handler"].asBool();
	abortHandler.dumpWithCrashWatch = config["abort_handler_dump_with_crash_watch"].asBool();
	abortHandler.beep = config["abort_handler_beep"].asBool();
	abortHandler.stopProcess = config["abort_handler_stop_process"].asBool();
}


} // namespace Fundamentals
} // namespace Agent
} // namespace Passenger
