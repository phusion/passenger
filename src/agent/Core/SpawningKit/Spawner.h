/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_SPAWNER_H_
#define _PASSENGER_SPAWNING_KIT_SPAWNER_H_

#include <boost/shared_ptr.hpp>
#include <oxt/system_calls.hpp>

#include <modp_b64.h>

#include <LoggingKit/Logging.h>
#include <SystemTools/SystemTime.h>
#include <Core/SpawningKit/Context.h>
#include <Core/SpawningKit/Result.h>
#include <Core/SpawningKit/UserSwitchingRules.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace boost;
using namespace oxt;


class Spawner {
private:
	StringKeyTable<StaticString> decodeEnvironmentVariables(const StaticString &envvarsData) {
		StringKeyTable<StaticString> result;
		string::size_type keyStart = 0;

		while (keyStart < envvarsData.size()) {
			string::size_type keyEnd = envvarsData.find('\0', keyStart);
			string::size_type valueStart = keyEnd + 1;
			if (valueStart >= envvarsData.size()) {
				break;
			}

			string::size_type valueEnd = envvarsData.find('\0', valueStart);
			if (valueEnd >= envvarsData.size()) {
				break;
			}

			StaticString key = envvarsData.substr(keyStart, keyEnd - keyStart);
			StaticString value = envvarsData.substr(valueStart, valueEnd - valueStart);
			result.insert(key, value, true);
			keyStart = valueEnd + 1;
		}

		result.compact();
		return result;
	}

protected:
	Context *context;

	void setConfigFromAppPoolOptions(Config *config, Json::Value &extraArgs,
		const AppPoolOptions &options)
	{
		string startCommand = options.getStartCommand(*context->resourceLocator,
			*context->wrapperRegistry);
		string envvarsData;
		try {
			envvarsData = modp::b64_decode(options.environmentVariables.data(),
				options.environmentVariables.size());
		} catch (const std::runtime_error &) {
			P_WARN("Unable to decode base64-encoded environment variables: " <<
				options.environmentVariables);
			envvarsData.clear();
		}

		config->appGroupName = options.getAppGroupName();
		config->appRoot = options.appRoot;
		config->logLevel = options.logLevel;
		config->genericApp = false;
		config->startsUsingWrapper = true;
		config->wrapperSuppliedByThirdParty = false;
		config->findFreePort = false;
		config->loadShellEnvvars = options.loadShellEnvvars;
		config->startCommand = startCommand;
		config->startupFile = options.getStartupFile(*context->wrapperRegistry);
		config->appType = options.appType;
		config->appEnv = options.environment;
		config->baseURI = options.baseURI;
		config->environmentVariables = decodeEnvironmentVariables(
			envvarsData);
		config->logFile = options.appLogFile;
		config->apiKey = options.apiKey;
		config->groupUuid = options.groupUuid;
		config->lveMinUid = options.lveMinUid;
		config->fileDescriptorUlimit = options.fileDescriptorUlimit;
		config->startTimeoutMsec = options.startTimeout;

		UserSwitchingInfo info = prepareUserSwitching(options,
			*context->wrapperRegistry);
		config->user = info.username;
		config->group = info.groupname;

		extraArgs["spawn_method"] = options.spawnMethod.toString();

		/******************/
		/******************/

		config->internStrings();
	}

	static void nonInterruptableKillAndWaitpid(pid_t pid) {
		boost::this_thread::disable_syscall_interruption dsi;
		syscalls::kill(pid, SIGKILL);
		syscalls::waitpid(pid, NULL, 0);
	}

	static void possiblyRaiseInternalError(const AppPoolOptions &options) {
		if (options.raiseInternalError) {
			throw RuntimeException("An internal error!");
		}
	}

public:
	/**
	 * Timestamp at which this Spawner was created. Microseconds resolution.
	 */
	const unsigned long long creationTime;

	Spawner(Context *_context)
		: context(_context),
		  creationTime(SystemTime::getUsec())
		{ }

	virtual ~Spawner() { }

	virtual Result spawn(const AppPoolOptions &options) = 0;

	virtual bool cleanable() const {
		return false;
	}

	virtual void cleanup() {
		// Do nothing.
	}

	virtual unsigned long long lastUsed() const {
		return 0;
	}

	Context *getContext() const {
		return context;
	}
};
typedef boost::shared_ptr<Spawner> SpawnerPtr;


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_SPAWNER_H_ */
