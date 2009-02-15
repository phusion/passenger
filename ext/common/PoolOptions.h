/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2008, 2009 Phusion
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
#include "Utils.h"

namespace Passenger {

using namespace std;

/**
 * This struct encapsulates information for ApplicationPool::get() and for
 * SpawnManager::spawn(), such as which application is to be spawned.
 *
 * <h2>Notes on privilege lowering support</h2>
 *
 * If <tt>lowerPrivilege</tt> is true, then it will be attempt to
 * switch the spawned application instance to the user who owns the
 * application's <tt>config/environment.rb</tt>, and to the default
 * group of that user.
 *
 * If that user doesn't exist on the system, or if that user is root,
 * then it will be attempted to switch to the username given by
 * <tt>lowestUser</tt> (and to the default group of that user).
 * If <tt>lowestUser</tt> doesn't exist either, or if switching user failed
 * (because the spawn server process does not have the privilege to do so),
 * then the application will be spawned anyway, without reporting an error.
 *
 * It goes without saying that lowering privilege is only possible if
 * the spawn server is running as root (and thus, by induction, that
 * Phusion Passenger and Apache's control process are also running as root).
 * Note that if Apache is listening on port 80, then its control process must
 * be running as root. See "doc/Security of user switching.txt" for
 * a detailed explanation.
 */
struct PoolOptions {
	/**
	 * The root directory of the application to spawn. In case of a Ruby on Rails
	 * application, this is the folder that contains 'app/', 'public/', 'config/',
	 * etc. This must be a valid directory, but the path does not have to be absolute.
	 */
	string appRoot;
	
	/** Whether to lower the application's privileges. */
	bool lowerPrivilege;
	
	/**
	 * The user to fallback to if lowering privilege fails.
	 */
	string lowestUser;
	
	/**
	 * The RAILS_ENV/RACK_ENV environment that should be used. May not be an
	 * empty string.
	 */
	string environment;
	
	/**
	 * The spawn method to use. Either "smart" or "conservative". See the Ruby
	 * class <tt>SpawnManager</tt> for details.
	 */
	string spawnMethod;
	
	/** The application type. Either "rails", "rack" or "wsgi". */
	string appType;
	
	/**
	 * The idle timeout, in seconds, of Rails framework spawners.
	 * A timeout of 0 means that the framework spawner should never idle timeout. A timeout
	 * of -1 means that the default timeout value should be used.
	 *
	 * For more details about Rails framework spawners, please
	 * read the documentation on the Railz::FrameworkSpawner
	 * Ruby class.
	 */
	long frameworkSpawnerTimeout;
	
	/**
	 * The idle timeout, in seconds, of Rails application spawners.
	 * A timeout of 0 means that the application spawner should never idle timeout. A timeout
	 * of -1 means that the default timeout value should be used.
	 *
	 * For more details about Rails application spawners, please
	 * read the documentation on the Railz::ApplicationSpawner
	 * Ruby class.
	 */
	long appSpawnerTimeout;
	
	/**
	 * The maximum number of requests that the spawned application may process
	 * before exiting. A value of 0 means unlimited.
	 */
	unsigned long maxRequests;
	
	/**
	 * The maximum amount of memory (in MB) the spawned application may use.
	 * A value of 0 means unlimited.
	 */
	unsigned long memoryLimit;
	
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
	 * Creates a new PoolOptions object with the default values filled in.
	 * One must still set appRoot manually, after having used this constructor.
	 */
	PoolOptions() {
		lowerPrivilege = true;
		lowestUser     = "nobody";
		environment    = "production";
		spawnMethod    = "smart";
		appType        = "rails";
		frameworkSpawnerTimeout = -1;
		appSpawnerTimeout       = -1;
		maxRequests    = 0;
		memoryLimit    = 0;
		useGlobalQueue = false;
		statThrottleRate        = 0;
	}
	
	/**
	 * Creates a new PoolOptions object with the given values.
	 */
	PoolOptions(const string &appRoot,
		bool lowerPrivilege       = true,
		const string &lowestUser  = "nobody",
		const string &environment = "production",
		const string &spawnMethod = "smart",
		const string &appType     = "rails",
		long frameworkSpawnerTimeout = -1,
		long appSpawnerTimeout       = -1,
		unsigned long maxRequests    = 0,
		unsigned long memoryLimit    = 0,
		bool useGlobalQueue          = false,
		unsigned long statThrottleRate = 0,
		const string &restartDir  = ""
	) {
		this->appRoot        = appRoot;
		this->lowerPrivilege = lowerPrivilege;
		this->lowestUser     = lowestUser;
		this->environment    = environment;
		this->spawnMethod    = spawnMethod;
		this->appType        = appType;
		this->frameworkSpawnerTimeout = frameworkSpawnerTimeout;
		this->appSpawnerTimeout       = appSpawnerTimeout;
		this->maxRequests    = maxRequests;
		this->memoryLimit    = memoryLimit;
		this->useGlobalQueue = useGlobalQueue;
		this->statThrottleRate        = statThrottleRate;
		this->restartDir     = restartDir;
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
	 */
	PoolOptions(const vector<string> &vec, unsigned int startIndex = 0) {
		appRoot        = vec[startIndex + 1];
		lowerPrivilege = vec[startIndex + 3] == "true";
		lowestUser     = vec[startIndex + 5];
		environment    = vec[startIndex + 7];
		spawnMethod    = vec[startIndex + 9];
		appType        = vec[startIndex + 11];
		frameworkSpawnerTimeout = atol(vec[startIndex + 13]);
		appSpawnerTimeout       = atol(vec[startIndex + 15]);
		maxRequests    = atol(vec[startIndex + 17]);
		memoryLimit    = atol(vec[startIndex + 19]);
		useGlobalQueue = vec[startIndex + 21] == "true";
		statThrottleRate = atol(vec[startIndex + 23]);
		restartDir     = vec[startIndex + 25];
	}
	
	/**
	 * Append the information in this PoolOptions object to the given
	 * string vector. The resulting array could, for example, be used
	 * as a message to be sent to the spawn server.
	 */
	void toVector(vector<string> &vec) const {
		if (vec.capacity() < vec.size() + 10) {
			vec.reserve(vec.size() + 10);
		}
		appendKeyValue (vec, "app_root",        appRoot);
		appendKeyValue (vec, "lower_privilege", lowerPrivilege ? "true" : "false");
		appendKeyValue (vec, "lowest_user",     lowestUser);
		appendKeyValue (vec, "environment",     environment);
		appendKeyValue (vec, "spawn_method",    spawnMethod);
		appendKeyValue (vec, "app_type",        appType);
		appendKeyValue2(vec, "framework_spawner_timeout", frameworkSpawnerTimeout);
		appendKeyValue2(vec, "app_spawner_timeout",       appSpawnerTimeout);
		appendKeyValue3(vec, "max_requests",    maxRequests);
		appendKeyValue3(vec, "memory_limit",    memoryLimit);
		appendKeyValue (vec, "use_global_queue", useGlobalQueue ? "true" : "false");
		appendKeyValue3(vec, "stat_throttle_rate", statThrottleRate);
		appendKeyValue (vec, "restart_dir",     restartDir);
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
};

} // namespace Passenger

#endif /* _PASSENGER_SPAWN_OPTIONS_H_ */

