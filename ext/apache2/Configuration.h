/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (C) 2008  Phusion
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
#define PASSENGER_VERSION "1.0.4"

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
			
			std::set<std::string> base_uris;
			Threeway autoDetect;
			Threeway allowModRewrite;
		};
		
		/**
		 * Server-wide configuration information.
		 */
		struct ServerConfig {
			/** The filename of the Ruby interpreter to use. */
			const char *ruby;
			
			/** The environment (i.e. value for RAILS_ENV) under which the
			 * Rails application should operate. */
			const char *env;
			
			/** The filename of the spawn server to use. */
			const char *spawnServer;
			
			/** The maximum number of simultaneously alive Rails application
			 * instances. */
			unsigned int maxPoolSize;
			
			/** Whether the maxPoolSize option was explicitly specified in
			 * this server config. */
			bool maxPoolSizeSpecified;
			
			/** The maximum number of seconds that a Rails application may be
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

			/** User that Rails applications must run as if user switching
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
