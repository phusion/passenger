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
#ifndef _PASSENGER_LOGGING_KIT_CONFIG_H_
#define _PASSENGER_LOGGING_KIT_CONFIG_H_

#include <boost/config.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/noncopyable.hpp>

#include <string>
#include <vector>

#include <LoggingKit/Forward.h>
#include <ConfigKit/Schema.h>

#include <jsoncpp/json.h>

namespace Passenger {
namespace LoggingKit {

using namespace std;


/*
 * BEGIN ConfigKit schema: Passenger::LoggingKit::Schema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   app_output_log_level         string    -   default("notice")
 *   buffer_logs                  boolean   -   default(false)
 *   file_descriptor_log_target   any       -   -
 *   level                        string    -   default("notice")
 *   redirect_stderr              boolean   -   default(true)
 *   target                       any       -   default({"stderr": true})
 *
 * END
 */
class Schema: public ConfigKit::Schema {
private:
	static Json::Value createStderrTarget();
	static void validateLogLevel(const string &key, const ConfigKit::Store &store,
		vector<ConfigKit::Error> &errors);
	static void validateTarget(const string &key, const ConfigKit::Store &store,
		vector<ConfigKit::Error> &errors);

public:
	Schema();
};

struct ConfigRealization {
	enum FdClosePolicy {
		NEVER_CLOSE,
		ALWAYS_CLOSE,
		CLOSE_WHEN_FINALIZED
	};

	Level level;
	Level appOutputLogLevel;

	TargetType targetType;
	TargetType fileDescriptorLogTargetType;
	int targetFd;
	bool saveLog;
	int fileDescriptorLogTargetFd;
	FdClosePolicy targetFdClosePolicy;
	FdClosePolicy fileDescriptorLogTargetFdClosePolicy;
	bool finalized;

	ConfigRealization(const ConfigKit::Store &store);
	~ConfigRealization();

	void apply(const ConfigKit::Store &config, ConfigRealization *oldConfigRlz)
		BOOST_NOEXCEPT_OR_NOTHROW;
	void finalize();
};

struct ConfigChangeRequest: public boost::noncopyable {
	boost::scoped_ptr<ConfigKit::Store> config;
	ConfigRealization *configRlz;

	ConfigChangeRequest();
	~ConfigChangeRequest();
};


} // namespace LoggingKit
} // namespace Passenger

#endif /* _PASSENGER_LOGGING_KIT_CONFIG_H_ */
