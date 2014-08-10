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
#ifndef _PASSENGER_CONFIGURATION_HPP_
#define _PASSENGER_CONFIGURATION_HPP_

#include <Logging.h>
#include <ServerInstanceDir.h>
#include <Constants.h>
#include <Utils.h>
#include <Utils/VariantMap.h>

/* The APR headers must come after the Passenger headers. See Hooks.cpp
 * to learn why.
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

#define UNSET_INT_VALUE INT_MIN


/**
 * Per-directory configuration information.
 *
 * Use the getter methods to query information, because those will return
 * the default value if the value is not specified.
 */
struct DirConfig {
	enum Threeway { ENABLED, DISABLED, UNSET };
	enum SpawnMethod { SM_UNSET, SM_SMART, SM_DIRECT };

	#include "ConfigurationFields.hpp"

	std::set<std::string> baseURIs;

	/** The path to the application's root (for example: RAILS_ROOT
	 * for Rails applications, directory containing 'config.ru'
	 * for Rack applications). If this value is NULL, the default
	 * autodetected path will be used.
	 */
	const char *appRoot;

	string appGroupName;

	/** The spawn method to use. */
	SpawnMethod spawnMethod;

	/**
	 * The idle timeout, in seconds, of preloader processes.
	 * May also be 0 (which indicates that the application spawner should
	 * never idle timeout) or -1 (which means that the value is not specified,
	 * and the default value should be used).
	 */
	long maxPreloaderIdleTime;

	/** Whether symlinks in the document root path should be resolved.
	 * The implication of this is documented in the users guide, section
	 * "How Phusion Passenger detects whether a virtual host is a web application".
	 */
	Threeway resolveSymlinksInDocRoot;

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

	vector<string> unionStationFilters;

	/**
	 * Whether Phusion Passenger should show friendly error pages.
	 */
	Threeway friendlyErrorPages;

	/**
	 * Whether analytics logging should be enabled.
	 */
	Threeway unionStationSupport;

	/**
	 * Whether response buffering support is enabled.
	 */
	Threeway bufferResponse;

	/*************************************/
	/*************************************/

	bool isEnabled() const {
		return enabled != DISABLED;
	}

	StaticString getAppGroupName(const StaticString &appRoot) const {
		if (appGroupName.empty()) {
			return appRoot;
		} else {
			return appGroupName;
		}
	}

	StaticString getSpawnMethodString() const {
		switch (spawnMethod) {
		case SM_SMART:
			return "smart";
		case SM_DIRECT:
			return "direct";
		default:
			return "smart";
		}
	}

	bool highPerformanceMode() const {
		return highPerformance == ENABLED;
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

	StaticString getRestartDir() const {
		if (restartDir != NULL) {
			return restartDir;
		} else {
			return "";
		}
	}

	string getUploadBufferDir(const ServerInstanceDir::Generation *generation) const {
		if (uploadBufferDir != NULL) {
			return uploadBufferDir;
		} else if (generation != NULL) {
			return generation->getPath() + "/buffered_uploads";
		} else {
			return getSystemTempDir();
		}
	}

	bool showFriendlyErrorPages() const {
		return friendlyErrorPages != DISABLED;
	}

	bool useUnionStation() const {
		return unionStationSupport == ENABLED;
	}

	bool getBufferResponse() const {
		return bufferResponse == ENABLED;
	}

	string getUnionStationFilterString() const {
		if (unionStationFilters.empty()) {
			return string();
		} else {
			string result;
			vector<string>::const_iterator it;

			for (it = unionStationFilters.begin(); it != unionStationFilters.end(); it++) {
				if (it != unionStationFilters.begin()) {
					result.append(1, '\1');
				}
				result.append(*it);
			}
			return result;
		}
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
	/** The Passenger root folder. */
	const char *root;

	VariantMap ctl;

	/** The default Ruby interpreter to use. */
	const char *defaultRuby;

	/** The log verbosity. */
	int logLevel;

	/** A file to print debug messages to, or NULL to just use STDERR. */
	const char *debugLogFile;

	/** The maximum number of simultaneously alive application
	 * instances. */
	unsigned int maxPoolSize;

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
	string unionStationProxyAddress;

	/** Directory in which analytics logs should be saved. */
	string analyticsLogUser;
	string analyticsLogGroup;

	set<string> prestartURLs;

	ServerConfig() {
		root               = NULL;
		defaultRuby        = DEFAULT_RUBY;
		logLevel           = DEFAULT_LOG_LEVEL;
		debugLogFile       = NULL;
		maxPoolSize        = DEFAULT_MAX_POOL_SIZE;
		poolIdleTime       = DEFAULT_POOL_IDLE_TIME;
		userSwitching      = true;
		defaultUser        = DEFAULT_WEB_APP_USER;
		tempDir            = getSystemTempDir();
		unionStationGatewayAddress = DEFAULT_UNION_STATION_GATEWAY_ADDRESS;
		unionStationGatewayPort    = DEFAULT_UNION_STATION_GATEWAY_PORT;
		unionStationGatewayCert    = string();
		unionStationProxyAddress   = string();
		analyticsLogUser   = DEFAULT_ANALYTICS_LOG_USER;
		analyticsLogGroup  = DEFAULT_ANALYTICS_LOG_GROUP;
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
	}
};

extern ServerConfig serverConfig;


} // namespace Passenger

/**
 * @}
 */

#endif /* _PASSENGER_CONFIGURATION_HPP_ */
