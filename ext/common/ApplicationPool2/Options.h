/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010, 2011 Phusion
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
#include <Account.h>
#include <Logging.h>
#include <Constants.h>
#include <ResourceLocator.h>
#include <StaticString.h>
#include <StringListCreator.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;

/**
 * This struct encapsulates information for ApplicationPool::get() and for
 * SpawnManager::spawn(), such as which application is to be spawned.
 *
 * <h2>Privilege lowering support</h2>
 *
 * If <em>user</em> is given and isn't the empty string, then the application process
 * will run as the given username. Otherwise, the owner of the application's startup
 * file (e.g. config.ru or config/environment.rb) will be used.
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
	
	vector<const StaticString *> getStringFields() const {
		vector<const StaticString *> result;
		result.reserve(18);
		
		result.push_back(&appRoot);
		result.push_back(&appGroupName);
		result.push_back(&appType);
		result.push_back(&startCommand);
		result.push_back(&startupFile);
		result.push_back(&processTitle);
		
		result.push_back(&environment);
		result.push_back(&baseURI);
		result.push_back(&spawnMethod);
		
		result.push_back(&user);
		result.push_back(&group);
		result.push_back(&defaultUser);
		result.push_back(&defaultGroup);
		result.push_back(&restartDir);
		
		result.push_back(&ruby);
		result.push_back(&groupSecret);
		result.push_back(&hostName);
		result.push_back(&uri);
		
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
	/*********** Spawn options that should be set manually ***********/
	
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
	 * filename. Either "classic-rails", "rack", "wsgi" or the empty string.
	 * In case of the latter, 'startCommand' and 'startupFile' (which MUST
	 * be set) will dictate the startup command and the startup file's
	 * filename. */
	StaticString appType;
	
	/** The command for spawning the application process. This is a list of
	 * arguments, separated by '\1', e.g. "ruby\1foo.rb". Only used
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
	
	/**
	 * Path to the Ruby interpreter to use, in case the application to spawn
	 * is a Ruby app.
	 */
	StaticString ruby;
	
	/**
	 * Any rights that the spawned application process may have. The SpawnManager
	 * will create a new account for each spawned app, and that account will be
	 * assigned these rights.
	 */
	Account::Rights rights;
	
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
	vector< pair<StaticString, StaticString> > environmentVariables;
	
	/**
	 * Whether to show the Phusion Passenger version number in the
	 * X-Powered-By header.
	 */
	bool showVersionInHeader;
	
	/** Whether debugger support should be enabled. */
	bool debugger;
	
	/** Whether to load environment variables set in shell startup
	 * files (e.g. ~/.bashrc) during spawning.
	 */
	bool loadShellEnvvars;
	
	/** Whether analytics logging should be enabled. */
	bool analytics;
	
	/**
	 * Whether application processes should print exceptions that occurred during
	 * application initialization.
	 */
	bool printExceptions;
	
	
	/*********** Per-group pool options that should be set manually ***********/
	
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
	 * A throttling rate for file stats. When set to a non-zero value N,
	 * restart.txt and other files which are usually stat()ted on every
	 * ApplicationPool::get() call will be stat()ed at most every N seconds.
	 */
	unsigned long statThrottleRate;
	
	/** In seconds. */
	long spawnerTimeout;
	
	
	/*********** Per-request options that should be set manually and that only matter to Pool ***********/
	
	/** Current request host name. */
	StaticString hostName;
	
	/** Current request URI. */
	StaticString uri;
	
	/**
	 * An analytics log object to log things to. May be the null pointer,
	 * in which case analytics logging is disabled for this request.
	 */
	AnalyticsLogPtr log;
	
	
	/*********** Spawn options automatically set by Pool ***********/
	
	/** The secret key of the pool group that the spawned process is to belong to. */
	StaticString groupSecret;
	
	
	/*********************************/
	
	/**
	 * Creates a new Options object with the default values filled in.
	 * One must still set appRoot manually, after having used this constructor.
	 */
	Options() {
		startTimeout            = 60 * 1000;
		environment             = "production";
		baseURI                 = "/";
		spawnMethod             = "smart-lv2";
		defaultUser             = "nobody";
		ruby                    = "ruby";
		rights                  = DEFAULT_BACKEND_ACCOUNT_RIGHTS;
		showVersionInHeader     = true;
		debugger                = false;
		loadShellEnvvars        = true;
		analytics               = false;
		printExceptions         = true;
		
		maxRequests             = 0;
		minProcesses            = 1;
		statThrottleRate        = 0;
		spawnerTimeout          = -1;
		
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
		const vector<const StaticString *> strings = getStringFields();
		const vector<const StaticString *> otherStrings = other.getStringFields();
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
			const StaticString *str = strings[i];
			const StaticString *otherStr = otherStrings[i];
			
			// Point current object's field to the data in the
			// internal storage area.
			*const_cast<StaticString *>(str) = StaticString(end, otherStr->size());
			
			// Copy over the string data.
			memcpy(end, otherStr->c_str(), otherStr->size());
			end += otherStr->size();
			*end = '\0';
			end++;
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
		hostName = string();
		uri      = string();
		log.reset();
		return *this;
	}
	
	/**
	 * Append the information in this Options object to the given
	 * string vector, except for environmentVariables.
	 *
	 * @param vec The vector to store the information in.
	 */
	void toVector(vector<string> &vec) const {
		if (vec.capacity() < vec.size() + 40) {
			vec.reserve(vec.size() + 40);
		}
		
		appendKeyValue (vec, "app_root",           appRoot);
		appendKeyValue (vec, "app_group_name",     getAppGroupName());
		appendKeyValue (vec, "app_type",           appType);
		if (appType.empty()) {
			appendKeyValue (vec, "start_command", startCommand);
			appendKeyValue (vec, "process_title", processTitle);
		}
		appendKeyValue3(vec, "start_timeout",      startTimeout);
		appendKeyValue (vec, "environment",        environment);
		appendKeyValue (vec, "base_uri",           baseURI);
		appendKeyValue (vec, "spawn_method",       spawnMethod);
		appendKeyValue (vec, "user",               user);
		appendKeyValue (vec, "group",              group);
		appendKeyValue (vec, "default_user",       defaultUser);
		appendKeyValue (vec, "default_group",      defaultGroup);
		appendKeyValue (vec, "restart_dir",        restartDir);
		appendKeyValue (vec, "ruby",               ruby);
		appendKeyValue3(vec, "rights",             rights);
		appendKeyValue4(vec, "show_version_in_header", showVersionInHeader);
		appendKeyValue4(vec, "debugger",           debugger);
		appendKeyValue4(vec, "analytics",          analytics);
		appendKeyValue4(vec, "print_exceptions",   printExceptions);
		
		appendKeyValue3(vec, "max_requests",       maxRequests);
		appendKeyValue3(vec, "min_processes",      minProcesses);
		appendKeyValue3(vec, "stat_throttle_rate", statThrottleRate);
		appendKeyValue2(vec, "spawner_timeout",    spawnerTimeout);
		
		appendKeyValue (vec, "group_secret",       groupSecret);
		
		/*********************************/
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
		if (appType == "classic-rails") {
			return ruby + "\1" + resourceLocator.getHelperScriptsDir() + "/classic-rails-loader.rb";
		} else if (appType == "rack") {
			return ruby + "\1" + resourceLocator.getHelperScriptsDir() + "/rack-loader.rb";
		} else if (appType == "wsgi") {
			return "python\1" + resourceLocator.getHelperScriptsDir() + "/wsgi-loader.py";
		} else {
			return startCommand;
		}
	}
	
	StaticString getStartupFile() const {
		if (appType == "classic-rails") {
			return "config/environment.rb";
		} else if (appType == "rack") {
			return "config.ru";
		} else if (appType == "wsgi") {
			return "passenger_wsgi.py";
		} else {
			return startupFile;
		}
	}
	
	StaticString getProcessTitle() const {
		if (appType == "classic-rails") {
			return "Passenger RailsApp";
		} else if (appType == "rack") {
			return "Passenger RackApp";
		} else if (appType == "wsgi") {
			return "Passenger WsgiApp";
		} else {
			return processTitle;
		}
	}
	
	unsigned long getSpawnerTimeout() const {
		if (spawnerTimeout == -1) {
			return 5 * 60;
		} else {
			return spawnerTimeout;
		}
	}
};

} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_OPTIONS_H_ */

