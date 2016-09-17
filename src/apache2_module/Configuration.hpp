/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_CONFIGURATION_HPP_
#define _PASSENGER_CONFIGURATION_HPP_

#include <Logging.h>
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
#include "ConfigurationFields.hpp"

struct DirConfig : GeneratedDirConfigPart {

	std::set<std::string> baseURIs;

	/** The path to the application's root (for example: RAILS_ROOT
	 * for Rails applications, directory containing 'config.ru'
	 * for Rack applications). If this value is NULL, the default
	 * autodetected path will be used.
	 */
	const char *appRoot;

	/** Whether symlinks in the document root path should be resolved.
	 * The implication of this is documented at
	 * https://www.phusionpassenger.com/library/indepth/ruby/app_autodetection/apache/.
	 */
	Threeway resolveSymlinksInDocRoot;

	/**
	 * Whether encoded slashes in URLs should be supported. This however conflicts
	 * with mod_rewrite support because of a bug/limitation in Apache, so it's one
	 * or the other.
	 */
	Threeway allowEncodedSlashes;

	string unionStationKey;

	vector<string> unionStationFilters;

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

	bool highPerformanceMode() const {
		return highPerformance == ENABLED;
	}

	bool allowsEncodedSlashes() const {
		return allowEncodedSlashes == ENABLED;
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
	const char *logFile;
	const char *fileDescriptorLogFile;

	/** Socket backlog for Passenger Core server socket */
	unsigned int socketBacklog;

	/** The maximum number of simultaneously alive application
	 * instances. */
	unsigned int maxPoolSize;

	/** The maximum number of seconds that an application may be
	 * idle before it gets terminated. */
	unsigned int poolIdleTime;

	unsigned int responseBufferHighWatermark;

	unsigned int statThrottleRate;

	/** Whether user switching support is enabled. */
	bool userSwitching;

	/** See PoolOptions for more info. */
	string defaultUser;
	/** See PoolOptions for more info. */
	string defaultGroup;

	string dataBufferDir;
	string instanceRegistryDir;

	bool disableSecurityUpdateCheck;
	string securityUpdateCheckProxy;

	bool unionStationSupport;
	string unionStationGatewayAddress;
	int unionStationGatewayPort;
	string unionStationGatewayCert;
	string unionStationProxyAddress;

	/** Directory in which analytics logs should be saved. */
	string analyticsLogUser;
	string analyticsLogGroup;

	bool turbocaching;

	set<string> prestartURLs;

	ServerConfig() {
		root               = NULL;
		defaultRuby        = DEFAULT_RUBY;
		logLevel           = DEFAULT_LOG_LEVEL;
		logFile            = NULL;
		fileDescriptorLogFile = NULL;
		socketBacklog      = DEFAULT_SOCKET_BACKLOG;
		maxPoolSize        = DEFAULT_MAX_POOL_SIZE;
		poolIdleTime       = DEFAULT_POOL_IDLE_TIME;
		responseBufferHighWatermark = DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK;
		statThrottleRate   = DEFAULT_STAT_THROTTLE_RATE;
		userSwitching      = true;
		disableSecurityUpdateCheck = false;
		securityUpdateCheckProxy = string();
		defaultUser        = DEFAULT_WEB_APP_USER;
		unionStationSupport        = false;
		unionStationGatewayAddress = DEFAULT_UNION_STATION_GATEWAY_ADDRESS;
		unionStationGatewayPort    = DEFAULT_UNION_STATION_GATEWAY_PORT;
		unionStationGatewayCert    = string();
		unionStationProxyAddress   = string();
		analyticsLogUser   = DEFAULT_ANALYTICS_LOG_USER;
		analyticsLogGroup  = DEFAULT_ANALYTICS_LOG_GROUP;
		turbocaching       = true;
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
