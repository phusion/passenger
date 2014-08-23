/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
#ifndef _PASSENGER_APPLICATION_POOL2_OPTIONS_H_
#define _PASSENGER_APPLICATION_POOL2_OPTIONS_H_

#include <string>
#include <vector>
#include <utility>
#include <boost/shared_array.hpp>
#include <ApplicationPool2/AppTypes.h>
#include <Account.h>
#include <UnionStation/Core.h>
#include <UnionStation/Transaction.h>
#include <Constants.h>
#include <ResourceLocator.h>
#include <StaticString.h>
#include <Utils.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;

/**
 * This struct encapsulates information for ApplicationPool::get() and for
 * Spawner::spawn(), such as which application is to be spawned.
 *
 * ## Privilege lowering support
 *
 * If <em>user</em> is given and isn't the empty string, then the application process
 * will run as the given username. Otherwise, the owner of the application's startup
 * file (e.g. config.ru) will be used.
 *
 * If <em>group</em> is given and isn't the empty string, then the application process
 * will run as the given group name. If it's set to the special value
 * "!STARTUP_FILE!", then the startup file's group will be used. Otherwise,
 * the primary group of the user that the application process will run as,
 * will be used as group.
 *
 * If the user or group that the application process attempts to switch to
 * doesn't exist, then <em>defaultUser</em> and <em>defaultGroup</em>, respectively,
 * will be used.
 *
 * Phusion Passenger will attempt to avoid running the application process as
 * root: if <em>user</em> or <em>group</em> is set to the root user or the root group,
 * or if the startup file is owned by root, then <em>defaultUser</em> and
 * <em>defaultGroup</em> will be used instead.
 *
 * All this only happen if Phusion Passenger has root privileges. If not, then
 * these options have no effect.
 */
class Options {
private:
	shared_array<char> storage;

	template<typename OptionsClass, typename StaticStringClass>
	static vector<StaticStringClass *> getStringFields(OptionsClass &options) {
		vector<StaticStringClass *> result;
		result.reserve(20);

		result.push_back(&options.appRoot);
		result.push_back(&options.appGroupName);
		result.push_back(&options.appType);
		result.push_back(&options.startCommand);
		result.push_back(&options.startupFile);
		result.push_back(&options.processTitle);

		result.push_back(&options.environment);
		result.push_back(&options.baseURI);
		result.push_back(&options.spawnMethod);

		result.push_back(&options.user);
		result.push_back(&options.group);
		result.push_back(&options.defaultUser);
		result.push_back(&options.defaultGroup);
		result.push_back(&options.restartDir);

		result.push_back(&options.preexecChroot);
		result.push_back(&options.postexecChroot);

		result.push_back(&options.ruby);
		result.push_back(&options.python);
		result.push_back(&options.nodejs);
		result.push_back(&options.loggingAgentAddress);
		result.push_back(&options.loggingAgentUsername);
		result.push_back(&options.loggingAgentPassword);
		result.push_back(&options.groupSecret);
		result.push_back(&options.hostName);
		result.push_back(&options.uri);
		result.push_back(&options.unionStationKey);

		return result;
	}

	static inline void
	appendKeyValue(vector<string> &vec, const char *key, const StaticString &value) {
		if (!value.empty()) {
			vec.push_back(key);
			vec.push_back(value.toString());
		}
	}

	static inline void
	appendKeyValue(vector<string> &vec, const char *key, const char *value) {
		vec.push_back(key);
		vec.push_back(value);
	}

	static inline void
	appendKeyValue2(vector<string> &vec, const char *key, long value) {
		vec.push_back(key);
		vec.push_back(toString(value));
	}

	static inline void
	appendKeyValue3(vector<string> &vec, const char *key, unsigned long value) {
		vec.push_back(key);
		vec.push_back(toString(value));
	}

	static inline void
	appendKeyValue4(vector<string> &vec, const char *key, bool value) {
		vec.push_back(key);
		vec.push_back(value ? "true" : "false");
	}

public:
	/*********** Spawn options that should be set by the caller ***********
	 * These are the options that are relevant while spawning an application
	 * process. These options are only used during spawning.
	 */

	/**
	 * The root directory of the application to spawn. In case of a Ruby on Rails
	 * application, this is the folder that contains 'app/', 'public/', 'config/',
	 * etc. This must be a valid directory, but the path does not have to be absolute.
	 */
	StaticString appRoot;

	/**
	 * A name used by ApplicationPool to uniquely identify an application.
	 * If one tries to get() from the application pool with name "A", then get()
	 * again with name "B", then the latter will spawn a new application process,
	 * even if both get() requests have the same app root.
	 *
	 * If left empty, then the app root is used as the app group name.
	 */
	StaticString appGroupName;

	/** The application's type, used for determining the command to invoke to
	 * spawn an application process as well as determining the startup file's
	 * filename. It can be one of the app type names in AppType.cpp, or the
	 * empty string (default). In case of the latter, 'startCommand' and
	 * 'startupFile' (which MUST be set) will dictate the startup command
	 * and the startup file's filename. */
	StaticString appType;

	/** The command for spawning the application process. This is a list of
	 * arguments, separated by '\t', e.g. "ruby\tfoo.rb". Only used
	 * during spawning and only if appType.empty(). */
	StaticString startCommand;

	/** Filename of the application's startup file. Only actually used for
	 * determining user switching info. Only used during spawning and only
	 * if appType.empty(). */
	StaticString startupFile;

	/** The process title to assign to the application process. Only used
	 * during spawning. May be empty in which case no particular process
	 * title is assigned. Only used during spawning and only if
	 * appType.empty(). */
	StaticString processTitle;

	/**
	 * Defaults to DEFAULT_LOG_LEVEL.
	 */
	int logLevel;

	/** The maximum amount of time, in milliseconds, that may be spent
	 * on spawning the process or the preloader. */
	unsigned int startTimeout;

	/**
	 * The RAILS_ENV/RACK_ENV environment that should be used. May not be an
	 * empty string.
	 */
	StaticString environment;

	/**
	 * The base URI on which the application runs. If the application is
	 * running on the root URI, then this value must be "/".
	 *
	 * @invariant baseURI != ""
	 */
	StaticString baseURI;

	/**
	 * Spawning method, either "smart" or "direct".
	 */
	StaticString spawnMethod;

	/** See overview. */
	StaticString user;
	/** See class overview. */
	StaticString group;
	/** See class overview. Defaults to "nobody". */
	StaticString defaultUser;
	/** See class overview. Defaults to the defaultUser's primary group. */
	StaticString defaultGroup;

	/**
	 * The directory which contains restart.txt and always_restart.txt.
	 * An empty string means that the default directory should be used.
	 */
	StaticString restartDir;

	StaticString preexecChroot;
	StaticString postexecChroot;

	/**
	 * Path to the Ruby interpreter to use, in case the application to spawn
	 * is a Ruby app.
	 */
	StaticString ruby;

	/**
	 * Path to the Python interpreter to use, in case the application to spawn
	 * is a Python app.
	 */
	StaticString python;

	/**
	 * Path to the Node.js command to use, in case the application to spawn
	 * is a Node.js app.
	 */
	StaticString nodejs;

	/**
	 * Any rights that the spawned application process may have. The SpawnManager
	 * will create a new account for each spawned app, and that account will be
	 * assigned these rights.
	 */
	Account::Rights rights;

	/**
	 * Environment variables which should be passed to the spawned application
	 * process.
	 */
	vector< pair<StaticString, StaticString> > environmentVariables;

	/** Whether debugger support should be enabled. */
	bool debugger;

	/** Whether to load environment variables set in shell startup
	 * files (e.g. ~/.bashrc) during spawning.
	 */
	bool loadShellEnvvars;

	/** Whether Union Station logging should be enabled. Enabling this option will
	 * result in:
	 *
	 *  - The application enabling its Union Station support.
	 *  - Periodic tasks such as `collectAnalytics()` to log things to Union Station.
	 *
	 * It does *not* necessarily result in a request logging data to Union Station.
	 * That depends on whether the `transaction` member is set.
	 *
	 * If this is set to true, then 'loggingAgentAddress', 'loggingAgentUsername'
	 * and 'loggingAgentPassword' must be non-empty.
	 */
	bool analytics;
	StaticString loggingAgentAddress;
	StaticString loggingAgentUsername;
	StaticString loggingAgentPassword;

	/**
	 * Whether Spawner should raise an internal error when spawning. Used
	 * during unit tests.
	 */
	bool raiseInternalError;


	/*********** Per-group pool options that should be set by the caller ***********
	 * These options dictate how Pool will manage processes, routing, etc. within
	 * a single Group. These options are not process-specific, only group-specific.
	 */

	/**
	 * The minimum number of processes for the current group that the application
	 * pool's cleaner thread should keep around.
	 */
	unsigned int minProcesses;

	/**
	 * The maximum number of processes that may be spawned
	 * for this app root. This option only has effect if it's lower than
	 * the pool size.
	 *
	 * A value of 0 means unspecified, and has no effect.
	 */
	unsigned int maxProcesses;

	/** The number of seconds that preloader processes may stay alive idling. */
	long maxPreloaderIdleTime;

	/**
	 * The maximum number of processes inside a group that may be performing
	 * out-of-band work at the same time.
	 */
	unsigned int maxOutOfBandWorkInstances;

	/**
	 * The maximum number of requests that may live in the Group.getWaitlist queue.
	 * A value of 0 means unlimited.
	 */
	unsigned int maxRequestQueueSize;

	/**
	 * The Union Station key to use in case analytics logging is enabled.
	 * It is used by Pool::collectAnalytics() and other administrative
	 * functions which are called periodically. Because they do not belong
	 * to any request, and they may still want to log to Union Station,
	 * this key is stored in the per-group options structure.
	 *
	 * It is not used on a per-request basis. Per-request analytics logging
	 * (and Union Station logging) uses the logger object in the `logger` field
	 * instead.
	 */
	StaticString unionStationKey;

	/*-----------------*/


	/*********** Per-request pool options that should be set by the caller ***********
	 * These options also dictate how Pool will manage processes, etc. Unlike the
	 * per-group options, these options are customizable on a per-request basis.
	 * Their effects also don't persist longer than a single request.
	 */

	/** Current request host name. */
	StaticString hostName;

	/** Current request URI. */
	StaticString uri;

	/**
	 * The Union Station log transaction that this request belongs to.
	 * May be the null pointer, in which case Union Station logging is
	 * disabled for this request.
	 *
	 * When an Options object is passed to another thread (either direct or through
	 * a copy), the caller should call `detachFromUnionStationTransaction()`.
	 * Each Union Station transaction object is only supposed to be used in the same
	 * thread.
	 */
	UnionStation::TransactionPtr transaction;

	/**
	 * A sticky session ID for routing to a specific process.
	 */
	unsigned int stickySessionId;

	/**
	 * A throttling rate for file stats. When set to a non-zero value N,
	 * restart.txt and other files which are usually stat()ted on every
	 * ApplicationPool::get() call will be stat()ed at most every N seconds.
	 */
	unsigned long statThrottleRate;

	/**
	 * The maximum number of requests that the spawned application may process
	 * before exiting. A value of 0 means unlimited.
	 */
	unsigned long maxRequests;

	/** When true, Pool::get() and Pool::asyncGet() will create the necessary
	 * SuperGroup and Group structures just as normally, and will even handle
	 * restarting logic, but will not actually spawn any processes and will not
	 * open a session with an existing process. Instead, a fake Session object
	 * is returned which points to a Process object that isn't stored anywhere
	 * in the Pool structures and isn't mapped to any real OS process. It does
	 * however point to the real Group structure. Useful for unit tests.
	 * False by default.
	 */
	bool noop;

	/*-----------------*/
	/*-----------------*/


	/*********** Spawn options automatically set by Pool ***********
	 * These options are passed to the Spawner. The Pool::get() caller may not
	 * see these values.
	 */

	/** The secret key of the pool group that the spawned process is to belong to. */
	StaticString groupSecret;

	/**
	 * A UUID that's generated on Group initialization, and changes every time
	 * the Group receives a restart command. Allows Union Station to track app
	 * restarts.
	 */
	StaticString groupUuid;


	/*********************************/

	/**
	 * Creates a new Options object with the default values filled in.
	 * One must still set appRoot manually, after having used this constructor.
	 */
	Options() {
		logLevel                = DEFAULT_LOG_LEVEL;
		startTimeout            = 90 * 1000;
		environment             = P_STATIC_STRING("production");
		baseURI                 = P_STATIC_STRING("/");
		spawnMethod             = P_STATIC_STRING("smart");
		defaultUser             = P_STATIC_STRING("nobody");
		ruby                    = P_STATIC_STRING(DEFAULT_RUBY);
		python                  = P_STATIC_STRING(DEFAULT_PYTHON);
		nodejs                  = P_STATIC_STRING(DEFAULT_NODEJS);
		rights                  = DEFAULT_BACKEND_ACCOUNT_RIGHTS;
		debugger                = false;
		loadShellEnvvars        = true;
		analytics               = false;
		raiseInternalError      = false;

		minProcesses            = 1;
		maxProcesses            = 0;
		maxPreloaderIdleTime    = -1;
		maxOutOfBandWorkInstances = 1;
		maxRequestQueueSize     = 100;

		stickySessionId         = 0;
		statThrottleRate        = 0;
		maxRequests             = 0;
		noop                    = false;

		/*********************************/
	}

	Options copy() const {
		return *this;
	}

	Options copyAndPersist() const {
		Options cpy(*this);
		cpy.persist(*this);
		return cpy;
	}

	/**
	 * Assign <em>other</em>'s string fields' values into this Option
	 * object, and store the data in this Option object's internal storage
	 * area.
	 */
	Options &persist(const Options &other) {
		vector<StaticString *> strings = getStringFields<Options, StaticString>(*this);
		const vector<const StaticString *> otherStrings =
			getStringFields<const Options, const StaticString>(other);
		unsigned int i;
		size_t otherLen = 0;
		char *end;

		assert(strings.size() == otherStrings.size());

		// Calculate the desired length of the internal storage area.
		// All strings are NULL-terminated.
		for (i = 0; i < otherStrings.size(); i++) {
			otherLen += otherStrings[i]->size() + 1;
		}
		for (i = 0; i < other.environmentVariables.size(); i++) {
			otherLen += environmentVariables[i].first.size() + 1;
			otherLen += environmentVariables[i].second.size() + 1;
		}

		shared_array<char> data(new char[otherLen]);
		end = data.get();

		// Copy string fields into the internal storage area.
		for (i = 0; i < otherStrings.size(); i++) {
			const char *pos = end;
			StaticString *str = strings[i];
			const StaticString *otherStr = otherStrings[i];

			// Copy over the string data.
			memcpy(end, otherStr->c_str(), otherStr->size());
			end += otherStr->size();
			*end = '\0';
			end++;

			// Point current object's field to the data in the
			// internal storage area.
			*str = StaticString(pos, end - pos - 1);
		}

		// Copy environmentVariables names and values into the internal storage area.
		for (i = 0; i < other.environmentVariables.size(); i++) {
			const pair<StaticString, StaticString> &p = other.environmentVariables[i];

			environmentVariables[i] = make_pair(
				StaticString(end, p.first.size()),
				StaticString(end + p.first.size() + 1, p.second.size())
			);

			// Copy over string data.
			memcpy(end, p.first.data(), p.first.size());
			end += p.first.size();
			*end = '\0';
			end++;

			// Copy over value data.
			memcpy(end, p.second.data(), p.second.size());
			end += p.second.size();
			*end = '\0';
			end++;
		}

		storage = data;

		return *this;
	}

	Options &clearPerRequestFields() {
		hostName = StaticString();
		uri      = StaticString();
		stickySessionId = 0;
		noop     = false;
		return detachFromUnionStationTransaction();
	}

	Options &detachFromUnionStationTransaction() {
		transaction.reset();
		return *this;
	}

	enum FieldSet {
		SPAWN_OPTIONS = 1 << 0,
		PER_GROUP_POOL_OPTIONS = 1 << 1,
		ALL_OPTIONS = ~0
	};

	/**
	 * Append information in this Options object to the given string vector, except
	 * for environmentVariables. You can customize what information you want through
	 * the `elements` argument.
	 */
	void toVector(vector<string> &vec, const ResourceLocator &resourceLocator,
		int fields = ALL_OPTIONS) const
	{
		if (fields & SPAWN_OPTIONS) {
			appendKeyValue (vec, "app_root",           appRoot);
			appendKeyValue (vec, "app_group_name",     getAppGroupName());
			appendKeyValue (vec, "app_type",           appType);
			appendKeyValue (vec, "start_command",      getStartCommand(resourceLocator));
			appendKeyValue (vec, "startup_file",       getStartupFile());
			appendKeyValue (vec, "process_title",      getProcessTitle());
			appendKeyValue2(vec, "log_level",          logLevel);
			appendKeyValue3(vec, "start_timeout",      startTimeout);
			appendKeyValue (vec, "environment",        environment);
			appendKeyValue (vec, "base_uri",           baseURI);
			appendKeyValue (vec, "spawn_method",       spawnMethod);
			appendKeyValue (vec, "user",               user);
			appendKeyValue (vec, "group",              group);
			appendKeyValue (vec, "default_user",       defaultUser);
			appendKeyValue (vec, "default_group",      defaultGroup);
			appendKeyValue (vec, "restart_dir",        restartDir);
			appendKeyValue (vec, "preexec_chroot",     preexecChroot);
			appendKeyValue (vec, "postexec_chroot",    postexecChroot);
			appendKeyValue (vec, "ruby",               ruby);
			appendKeyValue (vec, "python",             python);
			appendKeyValue (vec, "nodejs",             nodejs);
			appendKeyValue (vec, "logging_agent_address",  loggingAgentAddress);
			appendKeyValue (vec, "logging_agent_username", loggingAgentUsername);
			appendKeyValue (vec, "logging_agent_password", loggingAgentPassword);
			appendKeyValue4(vec, "debugger",           debugger);
			appendKeyValue4(vec, "analytics",          analytics);

			appendKeyValue (vec, "group_secret",       groupSecret);

			/*********************************/
		}
		if (fields & PER_GROUP_POOL_OPTIONS) {
			appendKeyValue3(vec, "min_processes",       minProcesses);
			appendKeyValue3(vec, "max_processes",       maxProcesses);
			appendKeyValue2(vec, "max_preloader_idle_time", maxPreloaderIdleTime);
			appendKeyValue3(vec, "max_out_of_band_work_instances", maxOutOfBandWorkInstances);
			appendKeyValue (vec, "union_station_key",   unionStationKey);
		}

		/*********************************/
	}

	template<typename Stream>
	void toXml(Stream &stream, const ResourceLocator &resourceLocator,
		int fields = ALL_OPTIONS) const
	{
		vector<string> args;
		unsigned int i;

		toVector(args, resourceLocator, fields);
		for (i = 0; i < args.size(); i += 2) {
			stream << "<" << args[i] << ">";
			stream << escapeForXml(args[i + 1]);
			stream << "</" << args[i] << ">";
		}
	}

	/**
	 * Returns the app group name. If there is no explicitly set app group name
	 * then the app root is considered to be the app group name.
	 */
	StaticString getAppGroupName() const {
		if (appGroupName.empty()) {
			return appRoot;
		} else {
			return appGroupName;
		}
	}

	string getStartCommand(const ResourceLocator &resourceLocator) const {
		if (appType == P_STATIC_STRING("classic-rails")) {
			return ruby + "\t" + resourceLocator.getHelperScriptsDir() + "/classic-rails-loader.rb";
		} else if (appType == P_STATIC_STRING("rack")) {
			return ruby + "\t" + resourceLocator.getHelperScriptsDir() + "/rack-loader.rb";
		} else if (appType == P_STATIC_STRING("wsgi")) {
			return python + "\t" + resourceLocator.getHelperScriptsDir() + "/wsgi-loader.py";
		} else if (appType == P_STATIC_STRING("node")) {
			return nodejs + "\t" + resourceLocator.getHelperScriptsDir() + "/node-loader.js";
		} else if (appType == P_STATIC_STRING("meteor")) {
			return ruby + "\t" + resourceLocator.getHelperScriptsDir() + "/meteor-loader.rb";
		} else {
			return startCommand;
		}
	}

	StaticString getStartupFile() const {
		if (startupFile.empty()) {
			const char *result = getAppTypeStartupFile(getAppType(appType));
			if (result == NULL) {
				return P_STATIC_STRING("");
			} else {
				return result;
			}
		} else {
			return startupFile;
		}
	}

	StaticString getProcessTitle() const {
		const char *result = getAppTypeProcessTitle(getAppType(appType));
		if (result == NULL) {
			return processTitle;
		} else {
			return result;
		}
	}

	unsigned long getMaxPreloaderIdleTime() const {
		if (maxPreloaderIdleTime == -1) {
			return 5 * 60;
		} else {
			return maxPreloaderIdleTime;
		}
	}
};

} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_OPTIONS_H_ */

