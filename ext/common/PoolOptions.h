/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_SPAWN_OPTIONS_H_
#define _PASSENGER_SPAWN_OPTIONS_H_

#include <string>
#include <vector>
#include "Account.h"
#include "Logging.h"
#include "Constants.h"
#include "StringListCreator.h"

namespace Passenger {

using namespace std;

/**
 * This struct encapsulates information for ApplicationPool::get() and for
 * SpawnManager::spawn(), such as which application is to be spawned.
 *
 * <h2>Privilege lowering support</h2>
 *
 * If <em>user</em> is given and isn't the empty string, then the application process
 * will run as the given username. Otherwise, the owner of the application's startup
 * file (e.g. config/environment.rb or config.ru) will be used.
 *
 * If <em>group</em> is given and isn't the empty string, then the application process
 * will run as the given group name. If it's set to the special value
 * "!STARTUP_FILE!", then the startup file's group will be used. Otherwise,
 * the primary group of the user that the application process will run as,
 * will be used as group.
 * 
 * If the user or group that the application process attempts to switch to
 * doesn't exist, then <em>default_user</em> and <em>default_group</em>, respectively,
 * will be used.
 * 
 * Phusion Passenger will attempt to avoid running the application process as
 * root: if <em>user</em> or <em>group</em> is set to the root user or the root group,
 * or if the startup file is owned by root, then <em>default_user</em> and
 * <em>default_group</em> will be used instead.
 * 
 * All this only happen if Phusion Passenger has root privileges. If not, then
 * these options have no effect.
 */
struct PoolOptions {
	/**
	 * The root directory of the application to spawn. In case of a Ruby on Rails
	 * application, this is the folder that contains 'app/', 'public/', 'config/',
	 * etc. This must be a valid directory, but the path does not have to be absolute.
	 */
	string appRoot;
	
	/**
	 * A name used by ApplicationPool to uniquely identify an application.
	 * If one tries to get() from the application pool with name "A", then get()
	 * again with name "B", then the latter will spawn a new application process,
	 * even if both get() requests have the same app root.
	 *
	 * If left empty (the default), then the app root is used as the app group
	 * name.
	 */
	string appGroupName;
	
	/** The application type. Either "rails" (default), "rack" or "wsgi". */
	string appType;
	
	/**
	 * The RAILS_ENV/RACK_ENV environment that should be used. May not be an
	 * empty string. The default is "production".
	 */
	string environment;
	
	/**
	 * Method with which application processes should be spawned. Different methods
	 * have different performance and compatibility properties. Available methods are
	 * "smart-lv2" (default), "smart" and "conservative". The different spawning methods
	 * are explained in the "Spawning methods explained" section of the users guide.
	 */
	string spawnMethod;
	
	/** See overview. */
	string user;
	/** See class overview. */
	string group;
	/** See class overview. Defaults to "nobody". */
	string defaultUser;
	/** See class overview. Defaults to the defaultUser's primary group. */
	string defaultGroup;
	
	/**
	 * The idle timeout, in seconds, of framework spawners. See the "Spawning methods
	 * explained" section of the users guide for information about framework spawners.
	 *
	 * A timeout of 0 means that the framework spawner should never idle timeout. A timeout
	 * of -1 means that the default timeout value should be used.
	 */
	long frameworkSpawnerTimeout;
	
	/**
	 * The idle timeout, in seconds, of application spawners. See the "Spawning methods
	 * explained" section of the users guide for information about application spawners.
	 *
	 * A timeout of 0 means that the application spawner should never idle timeout. A timeout
	 * of -1 means that the default timeout value should be used.
	 */
	long appSpawnerTimeout;
	
	/**
	 * Environment variables which should be passed to the spawned application
	 * process.
	 *
	 * If a new application process is started, then the getItems() method
	 * on this object will be called, which is to return environment
	 * variables that should be passed to the newly spawned backend process.
	 * Odd indices in the resulting array contain keys, even indices contain
	 * the value for the key in the previous index.
	 *
	 * May be set to NULL.
	 *
	 * @invariant environmentVariables.size() is an even number.
	 */
	StringListCreatorPtr environmentVariables;
	
	/**
	 * The base URI on which the application runs. If the application is
	 * running on the root URI, then this value must be "/".
	 *
	 * @invariant baseURI != ""
	 */
	string baseURI;
	
	/**
	 * The maximum number of requests that the spawned application may process
	 * before exiting. A value of 0 means unlimited.
	 */
	unsigned long maxRequests;
	
	/**
	 * The minimum number of processes for the current group that the application
	 * pool's cleaner thread should keep around.
	 */
	unsigned long minProcesses;
	
	/**
	 * Whether to use a global queue instead of a per-backend process
	 * queue. This option is only used by ApplicationPool::get().
	 *
	 * If enabled, when all backend processes are active, get() will
	 * wait until there's at least one backend process that's idle, instead
	 * of queuing the request into a random process's private queue.
	 * This is especially useful if a website has one or more long-running
	 * requests.
	 */
	bool useGlobalQueue;
	
	/**
	 * Whether to show the Phusion Passenger version number in the
	 * X-Powered-By header.
	 */
	bool showVersionInHeader;
	
	/**
	 * A throttling rate for file stats. When set to a non-zero value N,
	 * restart.txt and other files which are usually stat()ted on every
	 * ApplicationPool::get() call will be stat()ed at most every N seconds.
	 */
	unsigned long statThrottleRate;
	
	/**
	 * The directory which contains restart.txt and always_restart.txt.
	 * An empty string means that the default directory should be used.
	 */
	string restartDir;
	
	/**
	 * Any rights that the spawned application process may have. The SpawnManager
	 * will create a new account for each spawned app, and that account will be
	 * assigned these rights.
	 */
	Account::Rights rights;
	
	/** Whether debugger support should be enabled. */
	bool debugger;
	
	/** In case an app process needs to be spawned, whether analytics logging
	 * should be enabled.
	 */
	bool analytics;
	
	/**
	 * An analytics log object to log things to. May be the null pointer,
	 * in which case analytics logging is disabled for this request.
	 */
	AnalyticsLogPtr log;
	
	/**
	 * Whether the session returned by ApplicationPool::Interface::get()
	 * should be automatically initiated. Defaults to true.
	 */
	bool initiateSession;
	
	/**
	 * Whether application processes should print exceptions that occurred during
	 * application initialization. Defaults to true.
	 */
	bool printExceptions;
	
	/*********************************/
	
	/**
	 * Creates a new PoolOptions object with the default values filled in.
	 * One must still set appRoot manually, after having used this constructor.
	 */
	PoolOptions() {
		appType                 = "rails";
		environment             = "production";
		spawnMethod             = "smart-lv2";
		frameworkSpawnerTimeout = -1;
		appSpawnerTimeout       = -1;
		baseURI                 = "/";
		maxRequests             = 0;
		minProcesses            = 0;
		useGlobalQueue          = false;
		showVersionInHeader     = true;
		statThrottleRate        = 0;
		rights                  = DEFAULT_BACKEND_ACCOUNT_RIGHTS;
		debugger                = false;
		analytics               = false;
		initiateSession         = true;
		printExceptions         = true;
		
		/*********************************/
	}
	
	/**
	 * Creates a new PoolOptions object with the given values.
	 */
	PoolOptions(const string &appRoot,
		string appGroupName          = "",
		const string &appType        = "rails",
		const string &environment    = "production",
		const string &spawnMethod    = "smart-lv2",
		const string &user           = "",
		const string &group          = "",
		const string &defaultUser    = "",
		const string &defaultGroup   = "",
		long frameworkSpawnerTimeout = -1,
		long appSpawnerTimeout       = -1,
		const string &baseURI        = "/",
		unsigned long maxRequests    = 0,
		unsigned long minProcesses   = 0,
		bool useGlobalQueue          = false,
		bool showVersionInHeader     = true,
		unsigned long statThrottleRate = 0,
		const string &restartDir     = "",
		Account::Rights rights       = DEFAULT_BACKEND_ACCOUNT_RIGHTS,
		bool debugger                = false,
		bool analytics               = false,
		const AnalyticsLogPtr &log   = AnalyticsLogPtr()
	) {
		this->appRoot                 = appRoot;
		this->appGroupName            = appGroupName;
		this->appType                 = appType;
		this->environment             = environment;
		this->spawnMethod             = spawnMethod;
		this->user                    = user;
		this->group                   = group;
		this->defaultUser             = defaultUser;
		this->defaultGroup            = defaultGroup;
		this->frameworkSpawnerTimeout = frameworkSpawnerTimeout;
		this->appSpawnerTimeout       = appSpawnerTimeout;
		this->baseURI                 = baseURI;
		this->maxRequests             = maxRequests;
		this->minProcesses            = minProcesses;
		this->useGlobalQueue          = useGlobalQueue;
		this->showVersionInHeader     = showVersionInHeader;
		this->statThrottleRate        = statThrottleRate;
		this->restartDir              = restartDir;
		this->rights                  = rights;
		this->debugger                = debugger;
		this->analytics               = analytics;
		this->log                     = log;
		this->initiateSession         = true;
		this->printExceptions         = true;
		
		/*********************************/
	}
	
	/**
	 * Creates a new PoolOptions object from the given string vector.
	 * This vector contains information that's written to by toVector().
	 *
	 * For example:
	 * @code
	 *   PoolOptions options(...);
	 *   vector<string> vec;
	 *
	 *   vec.push_back("my");
	 *   vec.push_back("data");
	 *   options.toVector(vec);  // PoolOptions information will start at index 2.
	 *
	 *   PoolOptions copy(vec, 2);
	 * @endcode
	 *
	 * @param vec The vector containing spawn options information.
	 * @param startIndex The index in vec at which the information starts.
	 * @param analyticsLogger If given, and the vector contains logging information,
	 *                        then the 'log' member will be constructed using this logger.
	 */
	PoolOptions(const vector<string> &vec, unsigned int startIndex = 0,
	            AnalyticsLoggerPtr analyticsLogger = AnalyticsLoggerPtr()
	) {
		int offset = 1;
		bool hasEnvVars;
		
		appRoot          = vec[startIndex + offset];                 offset += 2;
		appGroupName     = vec[startIndex + offset];                 offset += 2;
		appType          = vec[startIndex + offset];                 offset += 2;
		environment      = vec[startIndex + offset];                 offset += 2;
		spawnMethod      = vec[startIndex + offset];                 offset += 2;
		user             = vec[startIndex + offset];                 offset += 2;
		group            = vec[startIndex + offset];                 offset += 2;
		defaultUser      = vec[startIndex + offset];                 offset += 2;
		defaultGroup     = vec[startIndex + offset];                 offset += 2;
		frameworkSpawnerTimeout = atol(vec[startIndex + offset]);    offset += 2;
		appSpawnerTimeout       = atol(vec[startIndex + offset]);    offset += 2;
		baseURI          = vec[startIndex + offset];                 offset += 2;
		maxRequests      = atol(vec[startIndex + offset]);           offset += 2;
		minProcesses     = atol(vec[startIndex + offset]);           offset += 2;
		useGlobalQueue   = vec[startIndex + offset] == "true";       offset += 2;
		showVersionInHeader = vec[startIndex + offset] == "true";    offset += 2;
		statThrottleRate = atol(vec[startIndex + offset]);           offset += 2;
		restartDir       = vec[startIndex + offset];                 offset += 2;
		rights           = (Account::Rights) atol(vec[startIndex + offset]);
		                                                             offset += 2;
		debugger         = vec[startIndex + offset] == "true";       offset += 2;
		analytics        = vec[startIndex + offset] == "true";       offset += 2;
		if (vec[startIndex + offset - 1] == "analytics_log_txn_id") {
			if (analyticsLogger != NULL) {
				string txnId     = vec[startIndex + offset];
				string groupName = vec[startIndex + offset + 2];
				string category  = vec[startIndex + offset + 4];
				string unionStationKey = vec[startIndex + offset + 6];
				log = analyticsLogger->continueTransaction(txnId,
					groupName, category, unionStationKey);
			}
			offset += 8;
		}
		initiateSession  = vec[startIndex + offset] == "true";       offset += 2;
		printExceptions  = vec[startIndex + offset] == "true";       offset += 2;
		hasEnvVars       = vec[startIndex + offset] == "true";       offset += 2;
		if (hasEnvVars) {
			environmentVariables = ptr(new SimpleStringListCreator(vec[startIndex + offset]));
		}
		offset += 2;
		
		/*********************************/
	}
	
	/**
	 * Append the information in this PoolOptions object to the given
	 * string vector. The resulting array could, for example, be used
	 * as a message to be sent to the spawn server.
	 *
	 * @param vec The vector to store the information in.
	 * @param storeEnvVars Whether to store environment variable information into vec as well.
	 * @throws Anything thrown by environmentVariables->getItems().
	 */
	void toVector(vector<string> &vec, bool storeEnvVars = true) const {
		if (vec.capacity() < vec.size() + 40) {
			vec.reserve(vec.size() + 40);
		}
		appendKeyValue (vec, "app_root",           appRoot);
		appendKeyValue (vec, "app_group_name",     getAppGroupName());
		appendKeyValue (vec, "app_type",           appType);
		appendKeyValue (vec, "environment",        environment);
		appendKeyValue (vec, "spawn_method",       spawnMethod);
		appendKeyValue (vec, "user",               user);
		appendKeyValue (vec, "group",              group);
		appendKeyValue (vec, "default_user",       defaultUser);
		appendKeyValue (vec, "default_group",      defaultGroup);
		appendKeyValue2(vec, "framework_spawner_timeout", frameworkSpawnerTimeout);
		appendKeyValue2(vec, "app_spawner_timeout",       appSpawnerTimeout);
		appendKeyValue (vec, "base_uri",           baseURI);
		appendKeyValue3(vec, "max_requests",       maxRequests);
		appendKeyValue3(vec, "min_processes",      minProcesses);
		appendKeyValue4(vec, "use_global_queue",   useGlobalQueue);
		appendKeyValue4(vec, "show_version_in_header", showVersionInHeader);
		appendKeyValue3(vec, "stat_throttle_rate", statThrottleRate);
		appendKeyValue (vec, "restart_dir",        restartDir);
		appendKeyValue3(vec, "rights",             rights);
		appendKeyValue4(vec, "debugger",           debugger);
		appendKeyValue4(vec, "analytics",          analytics);
		if (log) {
			appendKeyValue(vec, "analytics_log_txn_id", log->getTxnId());
			appendKeyValue(vec, "analytics_log_group_name", log->getGroupName());
			appendKeyValue(vec, "analytics_log_category", log->getCategory());
			appendKeyValue(vec, "union_station_key", log->getUnionStationKey());
		}
		appendKeyValue4(vec, "initiate_session",   initiateSession);
		appendKeyValue4(vec, "print_exceptions",   printExceptions);
		if (storeEnvVars) {
			appendKeyValue(vec, "has_environment_variables", "true");
			appendKeyValue(vec, "environment_variables", serializeEnvironmentVariables());
		} else {
			appendKeyValue(vec, "has_environment_variables", "false");
			appendKeyValue(vec, "environment_variables", "");
		}
		
		/*********************************/
	}
	
	PoolOptions own() const {
		PoolOptions copy = *this;
		if (copy.environmentVariables != NULL) {
			copy.environmentVariables->getItems(); // Prefetch items now while we still can.
		}
		copy.log.reset();
		return copy;
	}
	
	/**
	 * Returns the app group name. If there is no explicitly set app group name
	 * then the app root is considered to be the app group name.
	 */
	string getAppGroupName() const {
		if (appGroupName.empty()) {
			return appRoot;
		} else {
			return appGroupName;
		}
	}
	
	/**
	 * Serializes the items in environmentVariables into a string, which
	 * can be used to create a SimpleStringListCreator object.
	 *
	 * @throws Anything thrown by environmentVariables->getItems().
	 */
	string serializeEnvironmentVariables() const {
		vector<string>::const_iterator it, end;
		string result;
		
		if (environmentVariables) {
			result.reserve(1024);
			
			StringListPtr items = environmentVariables->getItems();
			end = items->end();
			
			for (it = items->begin(); it != end; it++) {
				result.append(*it);
				result.append(1, '\0');
				it++;
				result.append(*it);
				result.append(1, '\0');
			}
		}
		return Base64::encode(result);
	}

private:
	static inline void
	appendKeyValue(vector<string> &vec, const char *key, const string &value) {
		vec.push_back(key);
		vec.push_back(const_cast<string &>(value));
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
};

} // namespace Passenger

#endif /* _PASSENGER_SPAWN_OPTIONS_H_ */

