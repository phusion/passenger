/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_HOOKS_H_
#define _PASSENGER_HOOKS_H_

#include <string>
#include <vector>
#include <utility>

#include <boost/foreach.hpp>
#include <oxt/backtrace.hpp>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cctype>
#include <stdlib.h>
#include <sys/wait.h>

#include <jsoncpp/json.h>

#include <LoggingKit/LoggingKit.h>
#include <ProcessManagement/Spawn.h>
#include <ProcessManagement/Utils.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {

using namespace std;
using namespace oxt;


struct HookScriptOptions {
	// Required.
	string name;
	string spec;

	// Optional.
	Json::Value agentConfig;
	vector< pair<string, string> > environment;
};

namespace {
	inline vector< pair<string, string> >
	agentConfigToEnvVars(const Json::Value &config) {
		vector< pair<string, string> > result;
		Json::Value::const_iterator it, end = config.end();

		result.reserve(config.size());
		for (it = config.begin(); it != end; it++) {
			string key = "PASSENGER_";
			const char *end;
			const char *data = it.memberName(&end);

			while (data < end) {
				key.append(1, toupper(*data));
				data++;
			}
			switch (it->type()) {
			case Json::nullValue:
			case Json::stringValue:
				result.push_back(make_pair(key, it->asString()));
				break;
			default:
				result.push_back(make_pair(key, it->toStyledString()));
				break;
			}
		}

		return result;
	}

	inline void
	setEnvVarsFromVector(const vector< pair<string, string> > &envvars) {
		vector< pair<string, string> >::const_iterator it;

		for (it = envvars.begin(); it != envvars.end(); it++) {
			setenv(it->first.c_str(), it->second.c_str(), 1);
		}
	}

	inline void
	createHookScriptEnvironment(const HookScriptOptions &options, vector< pair<string, string> > &envvars) {
		vector< pair<string, string> >::const_iterator it, end = options.environment.end();
		envvars = agentConfigToEnvVars(options.agentConfig);
		for (it = options.environment.begin(); it != end; it++) {
			envvars.push_back(*it);
		}
		envvars.push_back(make_pair("PASSENGER_HOOK_NAME", options.name));
	}

	inline void
	parseHookScriptSpec(const HookScriptOptions &options, vector<string> &commands) {
		split(options.spec, ';', commands);

		vector<string>::iterator it, end = commands.end();
		for (it = commands.begin(); it != end; it++) {
			*it = strip(*it);
		}
	}
}

inline bool
runSingleHookScript(HookScriptOptions &options, const string &command,
	const vector< pair<string, string> > &envvars)
{
	TRACE_POINT_WITH_DATA(command.c_str());
	const char *commandArray[] = {
		command.c_str(),
		NULL
	};
	SubprocessInfo info;

	P_INFO("Running " << options.name << " hook script: " << command);
	try {
		runCommand(commandArray, info, true, true, boost::bind(setEnvVarsFromVector, envvars));
	} catch (const SystemException &e) {
		P_ERROR("Error running hook script " << command << ": " << e.what());
		return false;
	}
	if (info.status != 0 && info.status != -2) {
		P_INFO("Hook script " << command << " (PID " << info.pid <<
			") exited with status " << WEXITSTATUS(info.status));
	}
	return info.status == 0 || info.status == -2;
}

inline bool
runHookScripts(HookScriptOptions &options) {
	TRACE_POINT();
	if (options.spec.empty()) {
		return true;
	}

	vector<string> commands;
	vector< pair<string, string> > envvars;

	parseHookScriptSpec(options, commands);
	if (commands.empty()) {
		return true;
	}
	createHookScriptEnvironment(options, envvars);

	foreach (const string command, commands) {
		if (!runSingleHookScript(options, command, envvars)) {
			return false;
		}
	}
	return true;
}


} // namespace Passenger

#endif /* _PASSENGER_HOOKS_H_ */
