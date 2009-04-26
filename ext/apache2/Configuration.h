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
#ifndef _PASSENGER_CONFIGURATION_H_
#define _PASSENGER_CONFIGURATION_H_

#include "Utils.h"
#include "MessageChannel.h"

/* The APR headers must come after the Passenger headers. See Hooks.cpp
 * to learn why.
 *
 * MessageChannel.h must be included -- even though we don't actually use
 * MessageChannel.h in here, it's necessary to make sure that apr_want.h
 * doesn't b0rk on 'struct iovec'.
 */
#include <apr_pools.h>
#include <httpd.h>
#include <http_config.h>

/**
 * @defgroup Configuration Apache module configuration
 * @ingroup Core
 * @{
 */

#ifdef __cplusplus
	#include <set>
	#include <string>

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
			
			/** Whether mod_rewrite should be allowed for Rails applications. */
			Threeway allowModRewrite;
			
			/** The environment (i.e. value for RAILS_ENV) under which
			 * Rails applications should operate. */
			const char *railsEnv;
			
			/** The path to the application's root (for example: RAILS_ROOT
			 * for Rails applications, directory containing 'config.ru'
			 * for Rack applications). If this value is NULL, the default
			 * autodetected path will be used.
			 */
			const char *appRoot;
			
			/** The environment (i.e. value for RACK_ENV) under which
			 * Rack applications should operate. */
			const char *rackEnv;
			
			/** The Rails spawn method to use. */
			SpawnMethod spawnMethod;
			
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
			 * The maximum amount of memory (in MB) the spawned application may use.
			 * A value of 0 means unlimited.
			 */
			unsigned long memoryLimit;
			
			/** Indicates whether the memoryLimit option was explicitly specified
			 * in the directory configuration. */
			bool memoryLimitSpecified;
			
			Threeway highPerformance;
			
			/** Whether global queuing should be used. */
			Threeway useGlobalQueue;
			
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
			
			/*************************************/
			
			bool isEnabled() const {
				return enabled != DISABLED;
			}
			
			string getAppRoot(const char *documentRoot) const {
				if (appRoot == NULL) {
					return extractDirName(documentRoot);
				} else {
					return appRoot;
				}
			}
			
			string getAppRoot(const string &documentRoot) const {
				if (appRoot == NULL) {
					return extractDirName(documentRoot);
				} else {
					return appRoot;
				}
			}
			
			const char *getRailsEnv() const {
				if (railsEnv != NULL) {
					return railsEnv;
				} else {
					return "production";
				}
			}
			
			const char *getRackEnv() const {
				if (rackEnv != NULL) {
					return rackEnv;
				} else {
					return "production";
				}
			}
			
			const char *getSpawnMethodString() {
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
			
			unsigned long getMaxRequests() {
				if (maxRequestsSpecified) {
					return maxRequests;
				} else {
					return 0;
				}
			}
			
			unsigned long getMemoryLimit() {
				if (memoryLimitSpecified) {
					return memoryLimit;
				} else {
					return 200;
				}
			}
			
			bool highPerformanceMode() const {
				return highPerformance == ENABLED;
			}
			
			bool usingGlobalQueue() const {
				return useGlobalQueue == ENABLED;
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
			unsigned int logLevel;
			
			/** The maximum number of simultaneously alive application
			 * instances. */
			unsigned int maxPoolSize;
			
			/** Whether the maxPoolSize option was explicitly specified in
			 * this server config. */
			bool maxPoolSizeSpecified;
			
			/** The maximum number of simultaneously alive Rails application
			 * that a single Rails application may occupy. */
			unsigned int maxInstancesPerApp;
			
			/** Whether the maxInstancesPerApp option was explicitly specified in
			 * this server config. */
			bool maxInstancesPerAppSpecified;
			
			/** The maximum number of seconds that an application may be
			 * idle before it gets terminated. */
			unsigned int poolIdleTime;
			
			/** Whether the poolIdleTime option was explicitly specified in
			 * this server config. */
			bool poolIdleTimeSpecified;
			
			/** Whether user switching support is enabled. */
			bool userSwitching;
			
			/** Whether the userSwitching option was explicitly specified in
			 * this server config. */
			bool userSwitchingSpecified;

			/** The user that applications must run as if user switching
			 * fails or is disabled. NULL means the option is not specified.
			 */
			const char *defaultUser;
			
			/** The temp directory that Passenger should use. NULL
			 * means unspecified.
			 */
			const char *tempDir;
			
			const char *getDefaultUser() const {
				if (defaultUser != NULL) {
					return defaultUser;
				} else {
					return "nobody";
				}
			}
			
			const char *getTempDir() const {
				if (tempDir != NULL) {
					return tempDir;
				} else {
					return getSystemTempDir();
				}
			}
		};
	}

	extern "C" {
#endif

/** Configuration hook for per-directory configuration structure creation. */
void *passenger_config_create_dir(apr_pool_t *p, char *dirspec);

/** Configuration hook for per-directory configuration structure merging. */
void *passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv);

/** Configuration hook for per-server configuration structure creation. */
void *passenger_config_create_server(apr_pool_t *p, server_rec *s);

/** Configuration hook for per-server configuration structure merging. */
void *passenger_config_merge_server(apr_pool_t *p, void *basev, void *overridesv);

void passenger_config_merge_all_servers(apr_pool_t *pool, server_rec *main_server);

/** Apache module commands array. */
extern const command_rec passenger_commands[];

#ifdef __cplusplus
	}
#endif

/**
 * @}
 */

#endif /* _PASSENGER_CONFIGURATION_H_ */
