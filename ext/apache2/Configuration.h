/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
 *
 *  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _PASSENGER_CONFIGURATION_H_
#define _PASSENGER_CONFIGURATION_H_

#include <apr_pools.h>
#include <httpd.h>
#include <http_config.h>

/**
 * @defgroup Configuration Apache module configuration
 * @ingroup Core
 * @{
 */

/** Module version number. */
#define PASSENGER_VERSION "1.1.0"

#ifdef __cplusplus
	#include <set>
	#include <string>

	namespace Passenger {
	
		using namespace std;
		
		/**
		 * Per-directory configuration information.
		 */
		struct DirConfig {
			enum Threeway { ENABLED, DISABLED, UNSET };
			
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
			
			/** The environment (i.e. value for RACK_ENV) under which
			 * Rack applications should operate. */
			const char *rackEnv;
			
			enum SpawnMethod { SM_UNSET, SM_SMART, SM_CONSERVATIVE };
			/** The Rails spawn method to use. */
			SpawnMethod spawnMethod;
		};
		
		/**
		 * Server-wide (global, not per-virtual host) configuration information.
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
