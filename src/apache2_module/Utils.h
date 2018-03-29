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
#ifndef _PASSENGER_APACHE2_MODULE_UTILS_H_
#define _PASSENGER_APACHE2_MODULE_UTILS_H_

#include <string>
#include <boost/function.hpp>
#include <StaticString.h>

// The APR headers must come after the Passenger headers.
// See Hooks.cpp to learn why.
#include <httpd.h>
#include <http_config.h>
#include <apr_pools.h>
#include <apr_strings.h>
// In Apache < 2.4, this macro was necessary for core_dir_config and other structs
#define CORE_PRIVATE
#include <http_core.h>


extern "C" module AP_MODULE_DECLARE_DATA passenger_module;

#ifndef ap_get_core_module_config
	#define ap_get_core_module_config(s) ap_get_module_config(s, &core_module)
#endif


namespace Passenger {
namespace Apache2Module {

using namespace std;


enum DirConfigContext {
	DCC_GLOBAL,
	DCC_VHOST,
	DCC_DIRECTORY,
	DCC_LOCATION
};

typedef boost::function<void (
	server_rec *serverRec, core_server_config *csconfig, core_dir_config *cdconfig,
	DirConfig *pdconfig, DirConfigContext context
)> DirConfigTraverser;


inline void
addHeader(string &headers, const StaticString &name, const char *value) {
	if (value != NULL) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		headers.append(value);
		headers.append("\r\n", 2);
	}
}

inline void
addHeader(string &headers, const StaticString &name, const StaticString &value) {
	if (!value.empty()) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		headers.append(value.data(), value.size());
		headers.append("\r\n", 2);
	}
}

inline void
addHeader(request_rec *r, string &headers, const StaticString &name, int value) {
	if (value != UNSET_INT_VALUE) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		headers.append(apr_psprintf(r->pool, "%d", value));
		headers.append("\r\n", 2);
	}
}

inline void
addHeader(string &headers, const StaticString &name, Apache2Module::Threeway value) {
	if (value != Apache2Module::UNSET) {
		headers.append(name.data(), name.size());
		headers.append(": ", 2);
		if (value == Apache2Module::ENABLED) {
			headers.append("t", 1);
		} else {
			headers.append("f", 1);
		}
		headers.append("\r\n", 2);
	}
}

template<typename Collection, typename String>
inline Json::Value
strCollectionToJson(const Collection &collection) {
	Json::Value result(Json::arrayValue);
	typename Collection::const_iterator it, end = collection.end();
	for (it = collection.begin(); it != end; it++) {
		const String &val = *it;
		result.append(Json::Value(val.data(), val.data() + val.size()));
	}
	return result;
}

inline void
traverseAllDirConfigs(server_rec *serverRec, apr_pool_t *temp_pool, const DirConfigTraverser &traverser) {
	/*
	 * The server_rec linked list provided by Apache begins with the global context,
	 * but after that it contains the VirtualHost context in reverse order of how
	 * they are parsed. We turn the list back into the original order.
	 */
	vector<server_rec *> sortedServerRecs;
	while (serverRec != NULL) {
		sortedServerRecs.push_back(serverRec);
		serverRec = serverRec->next;
	}
	std::reverse(sortedServerRecs.begin() + 1, sortedServerRecs.end());

	/*
	 * Lookup the module struct for the Apache core module
	 * so that we can access its directory config merging
	 * function.
	 */
	module *coreModule = ap_find_linked_module("core.c");

	vector<server_rec *>::iterator it, end = sortedServerRecs.end();
	for (it = sortedServerRecs.begin(); it != end; it++) {
		server_rec *currentServerRec = *it;
		core_server_config *csconf;
		core_dir_config *cdconf, *subCdconf;
		DirConfig *pdconf, *subPdconf;
		int nelts;
		int i;
		ap_conf_vector_t **elts;

		csconf = (core_server_config *) ap_get_core_module_config(
			currentServerRec->module_config);
		cdconf = (core_dir_config *) ap_get_core_module_config(
			currentServerRec->lookup_defaults);
		pdconf = (DirConfig *) ap_get_module_config(
			currentServerRec->lookup_defaults, &passenger_module);
		traverser(currentServerRec, csconf, cdconf, pdconf,
			currentServerRec->is_virtual ? DCC_VHOST : DCC_GLOBAL);

		/*
		 * Apache does not merge <Directory> and <Location> configs with
		 * global or <VirtualHost> during config load time, but instead merges
		 * during the first request, as per the rules documented at
		 * https://httpd.apache.org/docs/2.4/sections.html#merging
		 * So in the following two loops we perform our own merging.
		 * This merging does not perfectly mimic Apache's real merging
		 * behavior because the real behavior depends on the request, but
		 * c'est la vie, users will just have to put up with this.
		 */

		nelts = csconf->sec_dir->nelts;
		elts  = (ap_conf_vector_t **) csconf->sec_dir->elts;
		for (i = 0; i < nelts; ++i) {
			subCdconf = (core_dir_config *) ap_get_core_module_config(elts[i]);
			subPdconf = (DirConfig *) ap_get_module_config(elts[i], &passenger_module);
			if (subCdconf != NULL && subPdconf != NULL) {
				if (coreModule != NULL) {
					subCdconf = (core_dir_config *) coreModule->merge_dir_config(
						temp_pool, cdconf, subCdconf);
				}
				subPdconf = (DirConfig *) mergeDirConfig(temp_pool, pdconf, subPdconf);

				traverser(currentServerRec, csconf, subCdconf, subPdconf, DCC_DIRECTORY);
			}
		}

		nelts = csconf->sec_url->nelts;
		elts  = (ap_conf_vector_t **) csconf->sec_url->elts;
		for (i = 0; i < nelts; ++i) {
			subCdconf = (core_dir_config *) ap_get_core_module_config(elts[i]);
			subPdconf = (DirConfig *) ap_get_module_config(elts[i], &passenger_module);
			if (subCdconf != NULL && subPdconf != NULL) {
				if (coreModule != NULL) {
					subCdconf = (core_dir_config *) coreModule->merge_dir_config(
						temp_pool, cdconf, subCdconf);
				}
				subPdconf = (DirConfig *) mergeDirConfig(temp_pool, pdconf, subPdconf);

				traverser(currentServerRec, csconf, subCdconf, subPdconf, DCC_LOCATION);
			}
		}
	}
}


} // namespace Apache2Module
} // namespace Passenger

#endif /* _PASSENGER_APACHE2_MODULE_UTILS_H_ */
