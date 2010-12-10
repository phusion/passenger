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
#ifndef _PASSENGER_CONFIGURATION_HPP_
#define _PASSENGER_CONFIGURATION_HPP_

#include "Utils.h"
#include "MessageChannel.h"
#include "Logging.h"
#include "ServerInstanceDir.h"
#include "Constants.h"

/* The APR headers must come after the Passenger headers. See Hooks.cpp
 * to learn why.
 *
 * MessageChannel.h must be included -- even though we don't actually use
 * MessageChannel.h in here, it's necessary to make sure that apr_want.h
 * doesn't b0rk on 'struct iovec'.
 */
#include "Configuration.h"

#include <set>
#include <string>

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

/**
 * @defgroup Configuration Apache module configuration
 * @ingroup Core
 * @{
 */

namespace Passenger {

using namespace std;

	
/**
 * Per-directory configuration information.
 *
 * Use the getter methods to query information, because those will return
 * the default value if the value is not specified.
 */
struct DirConfig {
	enum Threeway { ENABLED, DISABLED, UNSET };
	enum SpawnMethod { SM_UNSET, SM_SMART, SM_SMART_LV2, SM_CONSERVATIVE };
	
	Threeway enabled;
	
	std::set<std::string> railsBaseURIs;
	std::set<std::string> rackBaseURIs;
	
	/** Whether to autodetect Rails applications. */
	Threeway autoDetectRails;
	
	/** Whether to autodetect Rack applications. */
	Threeway autoDetectRack;
	
	/** Whether to autodetect WSGI applications. */
	Threeway autoDetectWSGI;
	
	/** The environment (RAILS_ENV/RACK_ENV/WSGI_ENV) under which
	 * applications should operate. */
	const char *environment;
	
	/** The path to the application's root (for example: RAILS_ROOT
	 * for Rails applications, directory containing 'config.ru'
	 * for Rack applications). If this value is NULL, the default
	 * autodetected path will be used.
	 */
	const char *appRoot;
	
	/** The environment (i.e. value for RACK_ENV) under which
	 * Rack applications should operate. */
	const char *rackEnv;
	
	string appGroupName;
	
	/** The spawn method to use. */
	SpawnMethod spawnMethod;
	
	/** See PoolOptions for more info. */
	const char *user;
	/** See PoolOptions for more info. */
	const char *group;
	
	/**
	 * The idle timeout, in seconds, of Rails framework spawners.
	 * May also be 0 (which indicates that the framework spawner should
	 * never idle timeout) or -1 (which means that the value is not specified).
	 */
	long frameworkSpawnerTimeout;
	
	/**
	 * The idle timeout, in seconds, of Rails application spawners.
	 * May also be 0 (which indicates that the application spawner should
	 * never idle timeout) or -1 (which means that the value is not specified).
	 */
	long appSpawnerTimeout;
	
	/**
	 * The maximum number of requests that the spawned application may process
	 * before exiting. A value of 0 means unlimited.
	 */
	unsigned long maxRequests;
	
	/** Indicates whether the maxRequests option was explicitly specified
	 * in the directory configuration. */
	bool maxRequestsSpecified;
	
	/**
	 * The minimum number of processes for a group that should be kept in
	 * the pool when cleaning idle processes. Defaults to 0.
	 */
	unsigned long minInstances;
	
	/**
	 * Indicates whether the minInstances option was explicitly specified
	 * in the directory configuration. */
	bool minInstancesSpecified;
	
	/** Whether symlinks in the document root path should be resolved.
	 * The implication of this is documented in the users guide, section
	 * "How Phusion Passenger detects whether a virtual host is a web application".
	 */
	Threeway resolveSymlinksInDocRoot;
	
	/** Whether high performance mode should be turned on. */
	Threeway highPerformance;
	
	/** Whether global queuing should be used. */
	Threeway useGlobalQueue;
	
	/**
	 * Whether encoded slashes in URLs should be supported. This however conflicts
	 * with mod_rewrite support because of a bug/limitation in Apache, so it's one
	 * or the other.
	 */
	Threeway allowEncodedSlashes;
	
	/**
	 * Throttle the number of stat() calls on files like
	 * restart.txt to the once per given number of seconds.
	 */
	unsigned long statThrottleRate;
	
	/** Indicates whether the statThrottleRate option was
	 * explicitly specified in the directory configuration. */
	bool statThrottleRateSpecified;
	
	/** The directory in which Passenger should look for
	 * restart.txt. NULL means that the default directory
	 * should be used.
	 */
	const char *restartDir;
	
	/**
	 * The directory in which Passenger should place upload buffer
	 * files. NULL means that the default directory should be used.
	 */
	const char *uploadBufferDir;
	
	string unionStationKey;
	
	/**
	 * Whether Phusion Passenger should show friendly error pages.
	 */
	Threeway friendlyErrorPages;
	
	/**
	 * Whether analytics logging should be enabled.
	 */
	Threeway analytics;
	
	/*************************************/
	/*************************************/
	
	bool isEnabled() const {
		return enabled != DISABLED;
	}
	
	string getAppRoot(const char *documentRoot) const {
		if (appRoot == NULL) {
			if (resolveSymlinksInDocRoot == DirConfig::ENABLED) {
				return extractDirName(resolveSymlink(documentRoot));
			} else {
				return extractDirName(documentRoot);
			}
		} else {
			return appRoot;
		}
	}
	
	string getAppRoot(const string &documentRoot) const {
		if (appRoot == NULL) {
			if (resolveSymlinksInDocRoot == DirConfig::ENABLED) {
				return extractDirName(resolveSymlink(documentRoot));
			} else {
				return extractDirName(documentRoot);
			}
		} else {
			return appRoot;
		}
	}
	
	const char *getUser() const {
		if (user != NULL) {
			return user;
		} else {
			return "";
		}
	}
	
	const char *getGroup() const {
		if (group != NULL) {
			return group;
		} else {
			return "";
		}
	}
	
	const char *getEnvironment() const {
		if (environment != NULL) {
			return environment;
		} else {
			return "production";
		}
	}
	
	string getAppGroupName(const string &appRoot) const {
		if (appGroupName.empty()) {
			return appRoot;
		} else {
			return appGroupName;
		}
	}
	
	const char *getSpawnMethodString() const {
		switch (spawnMethod) {
		case SM_SMART:
			return "smart";
		case SM_SMART_LV2:
			return "smart-lv2";
		case SM_CONSERVATIVE:
			return "conservative";
		default:
			return "smart-lv2";
		}
	}
	
	unsigned long getMaxRequests() const {
		if (maxRequestsSpecified) {
			return maxRequests;
		} else {
			return 0;
		}
	}
	
	unsigned long getMinInstances() const {
		if (minInstancesSpecified) {
			return minInstances;
		} else {
			return 1;
		}
	}
	
	bool highPerformanceMode() const {
		return highPerformance == ENABLED;
	}
	
	bool usingGlobalQueue() const {
		return useGlobalQueue != DISABLED;
	}
	
	bool allowsEncodedSlashes() const {
		return allowEncodedSlashes == ENABLED;
	}
	
	unsigned long getStatThrottleRate() const {
		if (statThrottleRateSpecified) {
			return statThrottleRate;
		} else {
			return 0;
		}
	}
	
	const char *getRestartDir() const {
		if (restartDir != NULL) {
			return restartDir;
		} else {
			return "";
		}
	}
	
	string getUploadBufferDir(const ServerInstanceDir::GenerationPtr &generation) const {
		if (uploadBufferDir != NULL) {
			return uploadBufferDir;
		} else {
			return generation->getPath() + "/buffered_uploads";
		}
	}
	
	bool showFriendlyErrorPages() const {
		return friendlyErrorPages != DISABLED;
	}
	
	bool analyticsEnabled() const {
		return analytics == ENABLED;
	}
	
	/*************************************/
};


/**
 * Server-wide (global, not per-virtual host) configuration information.
 *
 * Use the getter methods to query information, because those will return
 * the default value if the value is not specified.
 */
struct ServerConfig {
	/** The filename of the Ruby interpreter to use. */
	const char *ruby;
	
	/** The Passenger root folder. */
	const char *root;
	
	/** The log verbosity. */
	int logLevel;
	
	/** A file to print debug messages to, or NULL to just use STDERR. */
	const char *debugLogFile;
	
	/** The maximum number of simultaneously alive application
	 * instances. */
	unsigned int maxPoolSize;
	
	/** The maximum number of simultaneously alive Rails application
	 * that a single Rails application may occupy. */
	unsigned int maxInstancesPerApp;
	
	/** The maximum number of seconds that an application may be
	 * idle before it gets terminated. */
	unsigned int poolIdleTime;
	
	/** Whether user switching support is enabled. */
	bool userSwitching;
	
	/** See PoolOptions for more info. */
	string defaultUser;
	/** See PoolOptions for more info. */
	string defaultGroup;
	
	/** The temp directory that Passenger should use. */
	string tempDir;
	
	string unionStationGatewayAddress;
	int unionStationGatewayPort;
	string unionStationGatewayCert;
	
	/** Directory in which analytics logs should be saved. */
	string analyticsLogDir;
	string analyticsLogUser;
	string analyticsLogGroup;
	string analyticsLogPermissions;
	
	set<string> prestartURLs;
	
	ServerConfig() {
		ruby               = "ruby";
		root               = NULL;
		logLevel           = DEFAULT_LOG_LEVEL;
		debugLogFile       = NULL;
		maxPoolSize        = DEFAULT_MAX_POOL_SIZE;
		maxInstancesPerApp = DEFAULT_MAX_INSTANCES_PER_APP;
		poolIdleTime       = DEFAULT_POOL_IDLE_TIME;
		userSwitching      = true;
		defaultUser        = DEFAULT_WEB_APP_USER;
		tempDir            = getSystemTempDir();
		unionStationGatewayAddress = DEFAULT_UNION_STATION_GATEWAY_ADDRESS;
		unionStationGatewayPort    = DEFAULT_UNION_STATION_GATEWAY_PORT;
		unionStationGatewayCert    = "";
		analyticsLogUser   = DEFAULT_ANALYTICS_LOG_USER;
		analyticsLogGroup  = DEFAULT_ANALYTICS_LOG_GROUP;
		analyticsLogPermissions = DEFAULT_ANALYTICS_LOG_PERMISSIONS;
	}
	
	/** Called after the configuration files have been loaded, inside
	 * the control process.
	 */
	void finalize() {
		if (defaultGroup.empty()) {
			struct passwd *userEntry = getpwnam(defaultUser.c_str());
			if (userEntry == NULL) {
				throw ConfigurationException(
					string("The user that PassengerDefaultUser refers to, '") +
					defaultUser + "', does not exist.");
			}
			
			struct group *groupEntry = getgrgid(userEntry->pw_gid);
			if (groupEntry == NULL) {
				throw ConfigurationException(
					string("The option PassengerDefaultUser is set to '" +
					defaultUser + "', but its primary group doesn't exist. "
					"In other words, your system's user account database "
					"is broken. Please fix it."));
			}
			
			defaultGroup = groupEntry->gr_name;
		}
		
		if (analyticsLogDir.empty() && geteuid() == 0) {
			analyticsLogDir = "/var/log/passenger-analytics";
		} else if (analyticsLogDir.empty()) {
			struct passwd *user = getpwuid(geteuid());
			string username;
			
			if (user == NULL) {
				username = user->pw_name;
			} else {
				username = "user-" + toString(geteuid());
			}
			analyticsLogDir = string(getSystemTempDir()) +
				"/passenger-analytics-logs." +
				username;
		}
	}
};

extern ServerConfig serverConfig;


} // namespace Passenger

/**
 * @}
 */

#endif /* _PASSENGER_CONFIGURATION_HPP_ */
