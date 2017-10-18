/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#include <algorithm>
#include <cstdlib>
#include <climits>

/* ap_config.h checks whether the compiler has support for C99's designated
 * initializers, and defines AP_HAVE_DESIGNATED_INITIALIZER if it does. However,
 * g++ does not support designated initializers, even when ap_config.h thinks
 * it does. Here we undefine the macro to force httpd_config.h to not use
 * designated initializers. This should fix compilation problems on some systems.
 */
#include <ap_config.h>
#undef AP_HAVE_DESIGNATED_INITIALIZER

#include "Configuration.hpp"
#include <JsonTools/Autocast.h>
#include <Utils.h>
#include <Constants.h>

/* The APR/Apache headers must come after the Passenger headers. See Hooks.cpp
 * to learn why.
 */
#include <apr_strings.h>
// In Apache < 2.4, this macro was necessary for core_dir_config and other structs
#define CORE_PRIVATE
#include <http_core.h>

using namespace Passenger;

extern "C" module AP_MODULE_DECLARE_DATA passenger_module;

namespace Passenger { ServerConfig serverConfig; }

#define DEFINE_SERVER_BOOLEAN_CONFIG_SETTER(functionName, fieldName)             \
	static const char *                                                      \
	functionName(cmd_parms *cmd, void *dummy, int arg) {                     \
		serverConfig.fieldName = arg;                                    \
		return NULL;                                                     \
	}


template<typename T> static apr_status_t
destroy_config_struct(void *x) {
	delete (T *) x;
	return APR_SUCCESS;
}

template<typename Collection, typename T> static bool
contains(const Collection &coll, const T &item) {
	typename Collection::const_iterator it;
	for (it = coll.begin(); it != coll.end(); it++) {
		if (*it == item) {
			return true;
		}
	}
	return false;
}


extern "C" {

static DirConfig *
create_dir_config_struct(apr_pool_t *pool) {
	DirConfig *config = new DirConfig();
	apr_pool_cleanup_register(pool, config, destroy_config_struct<DirConfig>, apr_pool_cleanup_null);
	return config;
}

void *
passenger_config_create_dir(apr_pool_t *p, char *dirspec) {
	DirConfig *config = create_dir_config_struct(p);

	#include "CreateDirConfig.cpp"

	/*************************************/
	return config;
}

void *
passenger_config_merge_dir(apr_pool_t *p, void *basev, void *addv) {
	DirConfig *config = create_dir_config_struct(p);
	DirConfig *base = (DirConfig *) basev;
	DirConfig *add = (DirConfig *) addv;

	#include "MergeDirConfig.cpp"

	/*************************************/
	return config;
}

static void
postprocessDirConfig(server_rec *s, core_dir_config *core_dconf,
	DirConfig *psg_dconf, bool isTopLevel = false)
{
	// Do nothing
}

#ifndef ap_get_core_module_config
	#define ap_get_core_module_config(s) ap_get_module_config(s, &core_module)
#endif

void
passenger_postprocess_config(server_rec *s) {
	core_server_config *sconf;
	core_dir_config *core_dconf;
	DirConfig *psg_dconf;
	int nelts;
    ap_conf_vector_t **elts;
    int i;

	serverConfig.finalize();

	for (; s != NULL; s = s->next) {
		sconf = (core_server_config *) ap_get_core_module_config(s->module_config);
		core_dconf = (core_dir_config *) ap_get_core_module_config(s->lookup_defaults);
		psg_dconf = (DirConfig *) ap_get_module_config(s->lookup_defaults, &passenger_module);
		postprocessDirConfig(s, core_dconf, psg_dconf, true);

		nelts = sconf->sec_dir->nelts;
		elts  = (ap_conf_vector_t **) sconf->sec_dir->elts;
		for (i = 0; i < nelts; ++i) {
			core_dconf = (core_dir_config *) ap_get_core_module_config(elts[i]);
			psg_dconf = (DirConfig *) ap_get_module_config(elts[i], &passenger_module);
			if (core_dconf != NULL && psg_dconf != NULL) {
				postprocessDirConfig(s, core_dconf, psg_dconf);
			}
		}

		nelts = sconf->sec_url->nelts;
		elts  = (ap_conf_vector_t **) sconf->sec_url->elts;
		for (i = 0; i < nelts; ++i) {
			core_dconf = (core_dir_config *) ap_get_core_module_config(elts[i]);
			psg_dconf = (DirConfig *) ap_get_module_config(elts[i], &passenger_module);
			if (core_dconf != NULL && psg_dconf != NULL) {
				postprocessDirConfig(s, core_dconf, psg_dconf);
			}
		}
	}
}


/*************************************************
 * Passenger settings
 *************************************************/

#include "ConfigurationSetters.cpp"

static const char *
cmd_passenger_ctl(cmd_parms *cmd, void *dummy, const char *name, const char *value) {
	try {
		serverConfig.ctl[name] = autocastValueToJson(value);
		return NULL;
	} catch (const Json::Reader &) {
		return "Error parsing value as JSON";
	}
}

static const char *
cmd_passenger_spawn_method(cmd_parms *cmd, void *pcfg, const char *arg) {
	DirConfig *config = (DirConfig *) pcfg;
	if (strcmp(arg, "smart") == 0 || strcmp(arg, "smart-lv2") == 0) {
		config->spawnMethod = "smart";
	} else if (strcmp(arg, "conservative") == 0 || strcmp(arg, "direct") == 0) {
		config->spawnMethod = "direct";
	} else {
		return "PassengerSpawnMethod may only be 'smart', 'direct'.";
	}
	return NULL;
}

static const char *
cmd_passenger_base_uri(cmd_parms *cmd, void *pcfg, const char *arg) {
	DirConfig *config = (DirConfig *) pcfg;
	if (strlen(arg) == 0) {
		return "PassengerBaseURI may not be set to the empty string";
	} else if (arg[0] != '/') {
		return "PassengerBaseURI must start with a slash (/)";
	} else if (strlen(arg) > 1 && arg[strlen(arg) - 1] == '/') {
		return "PassengerBaseURI must not end with a slash (/)";
	} else {
		config->baseURIs.insert(arg);
		return NULL;
	}
}


typedef const char * (*Take1Func)();
typedef const char * (*Take2Func)();
typedef const char * (*FlagFunc)();

const command_rec passenger_commands[] = {
	// Passenger settings.

	#include "ConfigurationCommands.cpp"

	{ NULL }
};

} // extern "C"
