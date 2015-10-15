/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
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

#include <sys/types.h>
#include <sys/wait.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cctype>
#include <stdlib.h>
#include <unistd.h>

#include <Logging.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/VariantMap.h>

namespace Passenger {

using namespace std;
using namespace oxt;


struct HookScriptOptions {
	// Required.
	string name;
	string spec;

	// Optional.
	const VariantMap *agentsOptions;
	vector< pair<string, string> > environment;

	HookScriptOptions()
		: agentsOptions(NULL)
		{ }
};

namespace {
	inline vector< pair<string, string> >
	agentsOptionsToEnvVars(const VariantMap &agentsOptions) {
		vector< pair<string, string> > result;
		VariantMap::ConstIterator it, end = agentsOptions.end();

		result.reserve(agentsOptions.size());
		for (it = agentsOptions.begin(); it != end; it++) {
			string key = "PASSENGER_";
			const char *data = it->first.data();
			const char *end = it->first.data() + it->first.size();

			while (data < end) {
				key.append(1, toupper(*data));
				data++;
			}
			result.push_back(make_pair(key, it->second));
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
		if (options.agentsOptions) {
			envvars = agentsOptionsToEnvVars(*options.agentsOptions);
		}
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
	pid_t pid;
	int e, status;

	P_INFO("Running " << options.name << " hook script: " << command);

	pid = fork();
	if (pid == 0) {
		resetSignalHandlersAndMask();
		disableMallocDebugging();
		closeAllFileDescriptors(2);
		setEnvVarsFromVector(envvars);

		execlp(command.c_str(), command.c_str(), (const char * const) 0);
		e = errno;
		fprintf(stderr, "*** ERROR: Cannot execute %s hook script %s: %s (errno=%d)\n",
			options.name.c_str(), command.c_str(), strerror(e), e);
		fflush(stderr);
		_exit(1);
		return true; // Never reached.

	} else if (pid == -1) {
		e = errno;
		P_ERROR("Cannot fork a process for hook script " << command <<
			": " << strerror(e) << " (errno=" << e << ")");
		return false;

	} else if (waitpid(pid, &status, 0) == -1) {
		e = errno;
		P_ERROR("Unable to wait for hook script " << command <<
			" (PID " << pid << "): " << strerror(e) << " (errno=" <<
			e << ")");
		return false;

	} else {
		P_INFO("Hook script " << command << " (PID " << pid <<
			") exited with status " << WEXITSTATUS(status));
		return WEXITSTATUS(status) == 0;
	}
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
